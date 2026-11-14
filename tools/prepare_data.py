#!/usr/bin/env python3
"""Build Meridian's offline map and ZIP tables.

Sources (not committed; downloaded when regenerating):
  Natural Earth 1:110m and 1:50m land + admin-0 countries, public domain.
    https://www.naturalearthdata.com/
  US Census Bureau 2020 Gazetteer ZCTA national file, public domain.
    https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2020_Gazetteer/2020_Gaz_zcta_national.zip

Output (little-endian):
  data/map110.bin  data/map50.bin  data/zcta.bin
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NE = Path("/tmp/research/ne")
GAZ = Path("/tmp/research/2020_Gaz_zcta_national.txt")


def read_shp_records(path: Path):
    data = path.read_bytes()
    pos = 100
    n = len(data)
    while pos + 8 <= n:
        _rec, words = struct.unpack_from(">II", data, pos)
        pos += 8
        body = data[pos : pos + words * 2]
        pos += words * 2
        if len(body) < 4:
            continue
        shape_type = struct.unpack_from("<i", body, 0)[0]
        if shape_type == 0:
            yield []
            continue
        if shape_type != 5:
            raise SystemExit(f"unexpected shape {shape_type} in {path}")
        nparts, npoints = struct.unpack_from("<ii", body, 36)
        parts = list(struct.unpack_from(f"<{nparts}i", body, 44))
        pts_off = 44 + 4 * nparts
        coords = struct.unpack_from(f"<{npoints * 2}d", body, pts_off)
        rings = []
        for i, start in enumerate(parts):
            end = parts[i + 1] if i + 1 < nparts else npoints
            ring = [(coords[j * 2 + 1], coords[j * 2]) for j in range(start, end)]  # lat, lon
            if len(ring) >= 2 and ring[0] == ring[-1]:
                ring = ring[:-1]
            if len(ring) >= 3:
                rings.append(ring)
        yield rings


def read_dbf(path: Path):
    data = path.read_bytes()
    nrec, hlen, recsize = struct.unpack_from("<IHH", data, 4)
    fields = []
    pos = 32
    while data[pos] != 0x0D:
        name = data[pos : pos + 11].split(b"\x00", 1)[0].decode("ascii")
        flen = data[pos + 16]
        fields.append((name, flen))
        pos += 32
    off = {}
    cursor = 1
    for name, flen in fields:
        off[name] = (cursor, flen)
        cursor += flen
    rows = []
    for i in range(nrec):
        rec = data[hlen + i * recsize : hlen + (i + 1) * recsize]

        def get(field: str, rec=rec) -> str:
            a, length = off[field]
            raw = rec[a : a + length].replace(b"\x00", b"").strip()
            try:
                return raw.decode("utf-8")
            except UnicodeDecodeError:
                return raw.decode("latin1")

        rows.append(get)
    return rows


def pack_map(land_shp: Path, country_shp: Path, country_dbf: Path, dest: Path):
    land = list(read_shp_records(land_shp))
    countries = list(read_shp_records(country_shp))
    rows = read_dbf(country_dbf)
    if len(rows) != len(countries):
        raise SystemExit(f"dbf/shp mismatch {len(rows)} vs {len(countries)}")
    out = bytearray()
    out += b"MRDMP002"
    out += struct.pack("<I", len(land))
    out += struct.pack("<I", len(countries))
    for rings in land:
        out += struct.pack("<I", len(rings))
        for ring in rings:
            out += struct.pack("<I", len(ring))
            for lat, lon in ring:
                out += struct.pack("<ff", lat, lon)
    long_names = []
    for rings, get in zip(countries, rows):
        name = get("NAME")
        if len(name) > 22:
            long_names.append(name)
        label_lat = float(get("LABEL_Y") or "0")
        label_lon = float(get("LABEL_X") or "0")
        try:
            pop = int(float(get("POP_EST") or "0"))
        except ValueError:
            pop = 0
        raw = name.encode("utf-8")
        out += struct.pack("<H", len(raw))
        out += raw
        out += struct.pack("<ffI", label_lat, label_lon, pop & 0xFFFFFFFF)
        out += struct.pack("<I", len(rings))
        for ring in rings:
            out += struct.pack("<I", len(ring))
            for lat, lon in ring:
                out += struct.pack("<ff", lat, lon)
    dest.write_bytes(out)
    pts = sum(len(r) for rec in land for r in rec) + sum(len(r) for rec in countries for r in rec)
    print(f"{dest.name}: {len(out)/1e6:.2f} MB  land {len(land)}  countries {len(countries)}  points {pts}")
    if long_names:
        print("  long names:", ", ".join(sorted(set(long_names))[:30]))


def pack_zcta(src: Path, dest: Path):
    rows = []
    text = src.read_text(encoding="latin1").splitlines()
    for line in text[1:]:
        parts = line.split("\t")
        if len(parts) < 7:
            continue
        geo = parts[0].strip()
        if not geo.isdigit():
            continue
        try:
            lat = float(parts[5])
            lon = float(parts[6])
        except ValueError:
            continue
        rows.append((int(geo), lat, lon))
    rows.sort()
    # unique by zip, keep first
    uniq = []
    seen = set()
    for z, lat, lon in rows:
        if z in seen:
            continue
        seen.add(z)
        uniq.append((z, lat, lon))
    out = bytearray(b"MZCTA001")
    out += struct.pack("<I", len(uniq))
    for z, lat, lon in uniq:
        out += struct.pack("<Iii", z, int(round(lat * 1e6)), int(round(lon * 1e6)))
    dest.write_bytes(out)
    print(f"{dest.name}: {len(uniq)} ZCTAs, {len(out)/1e6:.2f} MB")
    want = {601, 10001, 90210, 72714, 72715, 94107}
    index = {z: (lat, lon) for z, lat, lon in uniq}
    for z in sorted(want):
        print(f"  {z:05d} -> {index.get(z)}")


def main():
    data = ROOT / "data"
    data.mkdir(exist_ok=True)
    pack_map(
        NE / "ne_110m_land.shp",
        NE / "ne_110m_admin_0_countries.shp",
        NE / "ne_110m_admin_0_countries.dbf",
        data / "map110.bin",
    )
    pack_map(
        NE / "ne_50m_land.shp",
        NE / "ne_50m_admin_0_countries.shp",
        NE / "ne_50m_admin_0_countries.dbf",
        data / "map50.bin",
    )
    pack_zcta(GAZ, data / "zcta.bin")


if __name__ == "__main__":
    sys.exit(main())
