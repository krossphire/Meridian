#!/usr/bin/env python3
"""Pack DXCC hover names and the split-entity outlines.

Polygons are Natural Earth 1:50m admin-0 map subunits, public domain.
Primary prefixes follow the Big CTY entity list. See third_party/cty/LICENSE.CTY.
The shapefile is not committed. Pass its directory:

  python3 tools/pack_dxcc.py /tmp/research/ne
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Natural Earth ADMIN (or SUBUNIT) name -> (hover name, primary prefix).
PREFIX = {
    "Afghanistan": ("Afghanistan", "YA"),
    "Aland": ("Aland Islands", "OH0"),
    "Albania": ("Albania", "ZA"),
    "Algeria": ("Algeria", "7X"),
    "American Samoa": ("American Samoa", "KH8"),
    "Andorra": ("Andorra", "C3"),
    "Angola": ("Angola", "D2"),
    "Anguilla": ("Anguilla", "VP2E"),
    "Antarctica": ("Antarctica", "CE9"),
    "Antigua and Barbuda": ("Antigua & Barbuda", "V2"),
    "Argentina": ("Argentina", "LU"),
    "Armenia": ("Armenia", "EK"),
    "Aruba": ("Aruba", "P4"),
    "Australia": ("Australia", "VK"),
    "Austria": ("Austria", "OE"),
    "Azerbaijan": ("Azerbaijan", "4J"),
    "Bahrain": ("Bahrain", "A9"),
    "Bangladesh": ("Bangladesh", "S2"),
    "Barbados": ("Barbados", "8P"),
    "Belarus": ("Belarus", "EU"),
    "Belgium": ("Belgium", "ON"),
    "Belize": ("Belize", "V3"),
    "Benin": ("Benin", "TY"),
    "Bermuda": ("Bermuda", "VP9"),
    "Bhutan": ("Bhutan", "A5"),
    "Bolivia": ("Bolivia", "CP"),
    "Bosnia and Herzegovina": ("Bosnia-Herzegovina", "E7"),
    "Botswana": ("Botswana", "A2"),
    "Brazil": ("Brazil", "PY"),
    "British Indian Ocean Territory": ("Chagos", "VQ9"),
    "British Virgin Islands": ("British Virgin Is.", "VP2V"),
    "Brunei": ("Brunei", "V8"),
    "Bulgaria": ("Bulgaria", "LZ"),
    "Burkina Faso": ("Burkina Faso", "XT"),
    "Burundi": ("Burundi", "9U"),
    "Cabo Verde": ("Cape Verde", "D4"),
    "Cambodia": ("Cambodia", "XU"),
    "Cameroon": ("Cameroon", "TJ"),
    "Canada": ("Canada", "VE"),
    "Cayman Islands": ("Cayman Islands", "ZF"),
    "Central African Republic": ("Central African Rep.", "TL"),
    "Chad": ("Chad", "TT"),
    "Chile": ("Chile", "CE"),
    "China": ("China", "BY"),
    "Colombia": ("Colombia", "HK"),
    "Comoros": ("Comoros", "D6"),
    "Cook Islands": ("Cook Islands", "E5"),
    "Costa Rica": ("Costa Rica", "TI"),
    "Croatia": ("Croatia", "9A"),
    "Cuba": ("Cuba", "CO"),
    "Cyprus": ("Cyprus", "5B"),
    "Czechia": ("Czech Republic", "OK"),
    "Democratic Republic of the Congo": ("DR Congo", "9Q"),
    "Denmark": ("Denmark", "OZ"),
    "Djibouti": ("Djibouti", "J2"),
    "Dominica": ("Dominica", "J7"),
    "Dominican Republic": ("Dominican Rep.", "HI"),
    "East Timor": ("Timor-Leste", "4W"),
    "Ecuador": ("Ecuador", "HC"),
    "Egypt": ("Egypt", "SU"),
    "El Salvador": ("El Salvador", "YS"),
    "Equatorial Guinea": ("Equatorial Guinea", "3C"),
    "Eritrea": ("Eritrea", "E3"),
    "Estonia": ("Estonia", "ES"),
    "Ethiopia": ("Ethiopia", "ET"),
    "Falkland Islands": ("Falkland Islands", "VP8"),
    "Faroe Islands": ("Faroe Islands", "OY"),
    "Federated States of Micronesia": ("Micronesia", "V6"),
    "Fiji": ("Fiji", "3D2"),
    "Finland": ("Finland", "OH"),
    "France": ("France", "F"),
    "French Polynesia": ("French Polynesia", "FO"),
    "French Southern and Antarctic Lands": ("Crozet", "FT"),
    "Gabon": ("Gabon", "TR"),
    "Gambia": ("The Gambia", "C5"),
    "Georgia": ("Georgia", "4L"),
    "Germany": ("Germany", "DL"),
    "Ghana": ("Ghana", "9G"),
    "Greece": ("Greece", "SV"),
    "Greenland": ("Greenland", "OX"),
    "Grenada": ("Grenada", "J3"),
    "Guam": ("Guam", "KH2"),
    "Guatemala": ("Guatemala", "TG"),
    "Guernsey": ("Guernsey", "GU"),
    "Guinea": ("Guinea", "3X"),
    "Guinea-Bissau": ("Guinea-Bissau", "J5"),
    "Guyana": ("Guyana", "8R"),
    "Haiti": ("Haiti", "HH"),
    "Honduras": ("Honduras", "HR"),
    "Hong Kong S.A.R.": ("Hong Kong", "VR"),
    "Hungary": ("Hungary", "HA"),
    "Iceland": ("Iceland", "TF"),
    "India": ("India", "VU"),
    "Indonesia": ("Indonesia", "YB"),
    "Iran": ("Iran", "EP"),
    "Iraq": ("Iraq", "YI"),
    "Ireland": ("Ireland", "EI"),
    "Isle of Man": ("Isle of Man", "GD"),
    "Israel": ("Israel", "4X"),
    "Italy": ("Italy", "I"),
    "Ivory Coast": ("Cote d'Ivoire", "TU"),
    "Jamaica": ("Jamaica", "6Y"),
    "Japan": ("Japan", "JA"),
    "Jersey": ("Jersey", "GJ"),
    "Jordan": ("Jordan", "JY"),
    "Kazakhstan": ("Kazakhstan", "UN"),
    "Kenya": ("Kenya", "5Z"),
    "Kiribati": ("Kiribati", "T30"),
    "Kuwait": ("Kuwait", "9K"),
    "Kyrgyzstan": ("Kyrgyzstan", "EX"),
    "Laos": ("Laos", "XW"),
    "Latvia": ("Latvia", "YL"),
    "Lebanon": ("Lebanon", "OD"),
    "Lesotho": ("Lesotho", "7P"),
    "Liberia": ("Liberia", "EL"),
    "Libya": ("Libya", "5A"),
    "Liechtenstein": ("Liechtenstein", "HB0"),
    "Lithuania": ("Lithuania", "LY"),
    "Luxembourg": ("Luxembourg", "LX"),
    "Madagascar": ("Madagascar", "5R"),
    "Malawi": ("Malawi", "7Q"),
    "Malaysia": ("West Malaysia", "9M2"),
    "Maldives": ("Maldives", "8Q"),
    "Mali": ("Mali", "TZ"),
    "Malta": ("Malta", "9H"),
    "Marshall Islands": ("Marshall Islands", "V7"),
    "Mauritania": ("Mauritania", "5T"),
    "Mauritius": ("Mauritius", "3B8"),
    "Mexico": ("Mexico", "XE"),
    "Moldova": ("Moldova", "ER"),
    "Monaco": ("Monaco", "3A"),
    "Mongolia": ("Mongolia", "JT"),
    "Montenegro": ("Montenegro", "4O"),
    "Montserrat": ("Montserrat", "VP2M"),
    "Morocco": ("Morocco", "CN"),
    "Mozambique": ("Mozambique", "C9"),
    "Myanmar": ("Myanmar", "XZ"),
    "Namibia": ("Namibia", "V5"),
    "Nauru": ("Nauru", "C2"),
    "Nepal": ("Nepal", "9N"),
    "Netherlands": ("Netherlands", "PA"),
    "New Caledonia": ("New Caledonia", "FK"),
    "New Zealand": ("New Zealand", "ZL"),
    "Nicaragua": ("Nicaragua", "YN"),
    "Niger": ("Niger", "5U"),
    "Nigeria": ("Nigeria", "5N"),
    "Niue": ("Niue", "E6"),
    "Norfolk Island": ("Norfolk Island", "VK9N"),
    "North Korea": ("DPR Korea", "P5"),
    "North Macedonia": ("North Macedonia", "Z3"),
    "Northern Mariana Islands": ("Mariana Islands", "KH0"),
    "Norway": ("Norway", "LA"),
    "Oman": ("Oman", "A4"),
    "Pakistan": ("Pakistan", "AP"),
    "Palau": ("Palau", "T8"),
    "Palestine": ("Palestine", "E4"),
    "Panama": ("Panama", "HP"),
    "Papua New Guinea": ("Papua New Guinea", "P2"),
    "Paraguay": ("Paraguay", "ZP"),
    "Peru": ("Peru", "OA"),
    "Philippines": ("Philippines", "DU"),
    "Pitcairn Islands": ("Pitcairn Island", "VP6"),
    "Poland": ("Poland", "SP"),
    "Portugal": ("Portugal", "CT"),
    "Puerto Rico": ("Puerto Rico", "KP4"),
    "Qatar": ("Qatar", "A7"),
    "Republic of Serbia": ("Serbia", "YU"),
    "Republic of the Congo": ("Republic of the Congo", "TN"),
    "Romania": ("Romania", "YO"),
    "Russia": ("European Russia", "UA"),
    "Rwanda": ("Rwanda", "9X"),
    "Saint Helena": ("St. Helena", "ZD7"),
    "Saint Kitts and Nevis": ("St. Kitts & Nevis", "V4"),
    "Saint Lucia": ("St. Lucia", "J6"),
    "Saint Pierre and Miquelon": ("St. Pierre & Miquelon", "FP"),
    "Saint Vincent and the Grenadines": ("St. Vincent", "J8"),
    "Samoa": ("Samoa", "5W"),
    "San Marino": ("San Marino", "T7"),
    "Saudi Arabia": ("Saudi Arabia", "HZ"),
    "Senegal": ("Senegal", "6W"),
    "Seychelles": ("Seychelles", "S7"),
    "Sierra Leone": ("Sierra Leone", "9L"),
    "Singapore": ("Singapore", "9V"),
    "Slovakia": ("Slovakia", "OM"),
    "Slovenia": ("Slovenia", "S5"),
    "Solomon Islands": ("Solomon Islands", "H4"),
    "Somalia": ("Somalia", "T5"),
    "South Africa": ("South Africa", "ZS"),
    "South Georgia and the Islands": ("South Georgia", "VP8"),
    "South Korea": ("Republic of Korea", "HL"),
    "South Sudan": ("South Sudan", "Z8"),
    "Spain": ("Spain", "EA"),
    "Sri Lanka": ("Sri Lanka", "4S"),
    "Sudan": ("Sudan", "ST"),
    "Suriname": ("Suriname", "PZ"),
    "Sweden": ("Sweden", "SM"),
    "Switzerland": ("Switzerland", "HB"),
    "Syria": ("Syria", "YK"),
    "Taiwan": ("Taiwan", "BV"),
    "Tajikistan": ("Tajikistan", "EY"),
    "Thailand": ("Thailand", "HS"),
    "The Bahamas": ("Bahamas", "C6"),
    "Togo": ("Togo", "5V"),
    "Tonga": ("Tonga", "A3"),
    "Trinidad and Tobago": ("Trinidad & Tobago", "9Y"),
    "Tunisia": ("Tunisia", "3V"),
    "Turkey": ("Turkey", "TA"),
    "Turkmenistan": ("Turkmenistan", "EZ"),
    "Turks and Caicos Islands": ("Turks & Caicos", "VP5"),
    "Tuvalu": ("Tuvalu", "T2"),
    "Uganda": ("Uganda", "5X"),
    "Ukraine": ("Ukraine", "UR"),
    "United Arab Emirates": ("United Arab Emirates", "A6"),
    "United Kingdom": ("England", "G"),
    "United Republic of Tanzania": ("Tanzania", "5H"),
    "United States Virgin Islands": ("US Virgin Islands", "KP2"),
    "United States of America": ("United States", "K"),
    "Uruguay": ("Uruguay", "CX"),
    "Uzbekistan": ("Uzbekistan", "UK"),
    "Vanuatu": ("Vanuatu", "YJ"),
    "Vatican": ("Vatican", "HV"),
    "Venezuela": ("Venezuela", "YV"),
    "Vietnam": ("Vietnam", "3W"),
    "Wallis and Futuna": ("Wallis & Futuna", "FW"),
    "Western Sahara": ("Western Sahara", "S0"),
    "Yemen": ("Yemen", "7O"),
    "Zambia": ("Zambia", "9J"),
    "Zimbabwe": ("Zimbabwe", "Z2"),
    "eSwatini": ("Eswatini", "3DA"),
    "Alaska": ("Alaska", "KL7"),
    "Hawaii": ("Hawaii", "KH6"),
    "Azores": ("Azores", "CU"),
    "Madeira": ("Madeira", "CT3"),
    "Canary Islands": ("Canary Islands", "EA8"),
    "Canary Is.": ("Canary Islands", "EA8"),
    "Sardinia": ("Sardinia", "IS0"),
    "Sicily": ("Sicily", "IT9"),
    "Corsica": ("Corsica", "TK"),
    "French Guiana": ("French Guiana", "FY"),
    "Guadeloupe": ("Guadeloupe", "FG"),
    "Martinique": ("Martinique", "FM"),
    "Mayotte": ("Mayotte", "FH"),
    "Reunion": ("Reunion", "FR"),
    "Saint Barthelemy": ("St. Barthelemy", "FJ"),
    "Saint Martin": ("Saint Martin", "FS"),
    "Sint Maarten": ("Sint Maarten", "PJ7"),
}

# Subunits drawn and hit-tested separately from their parent country.
SPLIT = {
    "Alaska",
    "Hawaii",
    "Puerto Rico",
    "Azores",
    "Madeira",
    "Canary Islands",
    "Canary Is.",
    "Sardinia",
    "Sicily",
    "Corsica",
    "French Guiana",
    "Guadeloupe",
    "Martinique",
    "Mayotte",
    "Reunion",
    "French Polynesia",
    "New Caledonia",
    "Guam",
    "Greenland",
    "United States Virgin Islands",
    "Saint Pierre and Miquelon",
    "Wallis and Futuna",
    "Saint Barthelemy",
    "Saint Martin",
}


def read_shp(path: Path):
    data = path.read_bytes()
    pos = 100
    out = []
    while pos + 8 <= len(data):
        _rec, words = struct.unpack_from(">II", data, pos)
        pos += 8
        body = data[pos : pos + words * 2]
        pos += words * 2
        if len(body) < 44:
            out.append([])
            continue
        shape = struct.unpack_from("<i", body, 0)[0]
        if shape == 0:
            out.append([])
            continue
        if shape != 5:
            out.append([])
            continue
        nparts, npoints = struct.unpack_from("<ii", body, 36)
        parts = list(struct.unpack_from(f"<{nparts}i", body, 44))
        coords = struct.unpack_from(f"<{npoints * 2}d", body, 44 + 4 * nparts)
        rings = []
        for i, start in enumerate(parts):
            end = parts[i + 1] if i + 1 < nparts else npoints
            ring = [(coords[j * 2 + 1], coords[j * 2]) for j in range(start, end)]
            if len(ring) >= 2 and ring[0] == ring[-1]:
                ring = ring[:-1]
            step = 1 if len(ring) < 80 else max(1, len(ring) // 80)
            ring = ring[::step]
            if len(ring) >= 3:
                rings.append(ring)
        out.append(rings)
    return out


def read_names(path: Path):
    data = path.read_bytes()
    nrec, hlen, recsize = struct.unpack_from("<IHH", data, 4)
    fields = []
    pos = 32
    while data[pos] != 0x0D:
        name = data[pos : pos + 11].split(b"\x00", 1)[0].decode()
        fields.append((name, data[pos + 16]))
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
            return rec[a : a + length].replace(b"\x00", b"").decode("latin1").strip()

        rows.append(get("SUBUNIT") or get("NAME"))
    return rows


def main() -> int:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/research/ne")
    stem = src / "ne_50m_admin_0_map_subunits"
    rings = read_shp(stem.with_suffix(".shp"))
    names = read_names(stem.with_suffix(".dbf"))
    entities = []
    for name, polys in zip(names, rings):
        if name not in SPLIT or name not in PREFIX or not polys:
            continue
        label, prefix = PREFIX[name]
        entities.append((label, prefix, polys))
    out = bytearray(b"MRDX001\0")
    out += struct.pack("<I", len(entities))
    for label, prefix, polys in entities:
        raw_name = label.encode("utf-8")
        raw_prefix = prefix.encode("utf-8")
        out += struct.pack("<H", len(raw_name)) + raw_name
        out += struct.pack("<H", len(raw_prefix)) + raw_prefix
        out += struct.pack("<I", len(polys))
        for ring in polys:
            out += struct.pack("<I", len(ring))
            for lat, lon in ring:
                out += struct.pack("<ff", lat, lon)
    dest = ROOT / "data" / "dxcc.bin"
    dest.write_bytes(out)
    lines = ["# name<TAB>hover name<TAB>prefix. Natural Earth admin name in column 1."]
    for key, (label, prefix) in sorted(PREFIX.items()):
        lines.append(f"{key}\t{label}\t{prefix}")
    (ROOT / "data" / "dxcc-names.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    pts = sum(len(r) for _a, _b, polys in entities for r in polys)
    print(f"{dest.name}: {len(entities)} entities, {pts} points, {len(out)/1e3:.1f} KB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
