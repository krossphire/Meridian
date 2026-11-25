# Meridian

Meridian is a rotator controller for the Green Heron RT-21 and RT-21D. Those two speak the same protocol. Other rotors are not supported.

The window is an azimuthal map. The heading is a number over the globe. Click the number to type a heading, or click the map to turn to that bearing. Esc or Space stops the rotor. A gear in the corner opens settings.

![Main window](docs/screenshots/main-large.png)

Written by Andrew Holmes, K5PO. Licensed under the GNU General Public License, version 3 or any later version. See [LICENSE](LICENSE).

Meridian was tested with RT-21 firmware 4.13.2, the current public release ([Green Heron software page](https://www.greenheronengineering.com/rotator-controllers-software/)). 4.14 is a pre-release. On 4.14.g1 the screen saver and register writes hang the serial port until the controller is power-cycled. Setup will not write unless a read reports 4.13.2, and it says so when it sees 4.14.

On 4.13.2 the screen saver blanks the panel and serial keeps answering. Polls do not count as activity, and a turn command wakes the display. OLED units should run 4.13.2. Brightness has no visible effect on an OLED RT-21 (LCD units only; that has not been checked).

## Download and run

The Windows download is one zip, `Meridian-1.0.0-win64.zip`. Unzip it anywhere and run `meridian.exe`. There is no installer.

The zip does not contain `meridian.ini`. Meridian keeps settings in `meridian.ini` in the same folder as `meridian.exe`. It does not use the Windows registry. Copy that file to back the settings up or move them to another computer.

If the folder next to the program cannot be written, Meridian uses `%APPDATA%\Meridian` instead. Settings shows the folder in use. An unzipped copy is writable, so the file stays next to the program.

Logs and setup backups use that same folder: `logs/` and `backups/`.

## First run

The home location starts at grid **EM26**. No host, COM port, or TCP port is filled in. The status line says **Set up your RT-21 in settings**.

**Find RT-21** checks USB and COM ports first, in numeric order (COM1, COM2, COM10), at 4800 and then 9600. It stops at the first port that answers. If none do, it scans the computer's local network. A hit is a button. The address is saved when that button is clicked. If a saved IP connection is down, Find saves the hit it confirmed and connects. If nothing answers, the line says **No RT-21 found.**

A USB serial chip with no COM port is only called a possible RT-21, with the driver page for that chip. Older units use FTDI (serial numbers under 2000 need Green Heron's INF; 2000 and up use the FTDI driver from Windows Update). Units shipped from November 2022 use a Silicon Labs CP210x. A USB device that failed its descriptor request is not called an RT-21.

**Scan the network** skips the serial ports and looks on the local network. You can type a host and a custom port under the button. Find tries TCP 6555, then 6665, plus a custom port when one was typed. A network hit is listed only after both a heading and a version answer. No AT command is sent during Find.

## Serial and network

A serial connection is USB or a COM port, 8 data bits, no parity, 1 stop bit. The baud choices are 4800 and 9600. 4800 is the RT-21's usual speed.

A network connection is the same commands over TCP, on the controller's serial TCP port. That port is **6665** unless the network module was set to something else. The Green Heron setup utility writes 6555 when the module is reset, which is why Find tries 6555 and then 6665. Meridian does not fill in a host or a port on its own.

**Network Connection** is **No** or **Yes**.

- **No** is for a serial RT-21 with no network board. Network read and write stay hidden, so Meridian does not send `AT`, `T11`, or `T01`.
- **Yes** is set when you use an IP connection, or when a network read succeeds. You can set it yourself in Settings.

## Setup

The Setup page reads the controller: version, the pot, and the stored settings. On firmware 4.13.2, each **Write** button stores that one setting and says **Saved** when the read-back matches. There is no factory reset and no bootloader command.

These are shown and not written: min speed, rotor type, mode, security, the advanced options, the limit switch, and magnetic variation.

The network module (address, mask, gateway, ports, Wi-Fi, and the rest) can be read or changed only over USB or serial, and only while **Network Connection** is **Yes**. On an IP-only link the page says network settings need USB or serial, and the fields stay grey: changing them over IP can drop the link. **Read network** talks to the module and does not write it. After that read has succeeded on the serial connection, the gateway can be written. Other network settings stay read-only. The web password is reported as set or not set. The Wi-Fi key is not shown in clear text.

**Save backup** and **Save a copy** write a text copy under `backups/`. **Guide me** only suggests a port and an address. It does not send them.

The command bytes are in [docs/RT21_PROTOCOL.md](docs/RT21_PROTOCOL.md). How the Setup page lines up with Green Heron's utility is in [docs/RT21_SETUP_PARITY.md](docs/RT21_SETUP_PARITY.md).

## Logs

**Record rotor traffic** is on by default. Each session writes `logs/meridian-YYYYMMDD-HHMMSS.log` next to the program (or under `%APPDATA%\Meridian\logs` when that folder is the one in use). Lines are written as they happen. Network reads and Find probes are still logged when the checkbox is off. Passwords are redacted.

## Using the map

- Click the globe to turn to that bearing. The orange line is where the rotor is now. A short tick on the rim is the heading it is moving toward.
- Click the heading number. Enter, or clicking elsewhere, sends it. Esc cancels the edit.
- Esc or Space, while not editing, stops the rotor.
- The gear opens settings. On a very small window it stays hidden until the pointer is over the map. Ctrl+, opens settings too.
- A rotor menu appears only when more than one rotor is saved.
- The dot is the link: green connected, amber moving or connecting, red failed. The tooltip has the reason.
- N, NE, E, SE, S, SW, W, and NW sit along the bottom. The pill nearest the heading is green, and amber while the rotor is moving. Settings can hide the row.
- When a station is active, a small yellow button in the upper right reads “Beam toward” and the call. One click turns the short path. Settings can hide it. Callsigns, QRZ, and ports stay in Settings. QRZ XML is optional, on the Sources page. The password stays in the system keychain.
- **Show sun & day/night** is on by default. The sun and the day/night tint are not clickable.

Idle polls go out every 30 seconds. Settings can set that from 30 to 60 seconds. A TCP connect waits at most 3 seconds. If the link drops, Meridian tries again on its own. A click, a typed heading, opening the program, or leaving settings tries immediately. A goto is not sent again on a later reconnect.

The home location accepts any of:

- Maidenhead, 4, 6, or 8 characters (`EM26`, `EM26UL`)
- A US ZIP or ZIP+4. Centroids come from the Census Bureau 2020 ZCTA gazetteer. ZCTAs leave out some PO-box-only codes.
- Coordinates: `36.47, -94.23`, `36.47N 94.23W`, `N36.47 W94.23`, `36°28'12"N 94°13'48"W`, `36 28 12 N 94 13 48 W`, `36°28.2'N 94°13.8'W`

The resolved latitude, longitude, and grid are shown under the field before you save.

## Build

Qt 6 (Widgets, Network, SerialPort, Svg), a C++17 compiler, and CMake 3.16 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/meridian
```

The version in the window title, `meridian --version`, and the traffic-log header is the CMake project version.

Ubuntu packages: `qt6-base-dev`, `qt6-serialport-dev`, `qt6-svg-dev`, `cmake`, `ninja-build`, `g++`.

On Windows, install Qt 6 with MinGW or MSVC and add its `bin` directory to `PATH` before configuring. On macOS, the Qt online installer or `brew install qt` both work. Pass `-DCMAKE_PREFIX_PATH` to the Qt prefix if CMake does not find it. macOS builds, and it has not been tried on a Mac. The release workflow does not package it.

Linux is what the tests run on. A tag `v*` (for example `v1.0.0`) builds a draft GitHub release whose asset is `Meridian-1.0.0-win64.zip`, plus a SHA-256 file for that zip. The release stays a draft until it is published by hand. The zip is `meridian.exe`, `meridian-mock.exe`, and the Qt files `windeployqt` copies. It does not contain `meridian.ini`.

`meridian-mock` is a stand-in controller for tests and for trying the window with no hardware. Its `--tcp` option defaults to port 6555. That is the mock's listen port, not a port Meridian fills in for a real rotor. `meridian --mock` starts the mock and points the session at it without rewriting saved settings.

## Credits

- Written by Andrew Holmes, K5PO.
- Click-to-bearing and the Maidenhead conversion are derived from [QLog](https://github.com/foldynl/QLog) `ui/RotatorWidget` and `data/Gridsquare` (GPL-3.0-or-later). Copyright Thomas Gatzweiler 2020 and Ladislav Foldyna 2021–2026. Meridian does not use QLog's logger, profiles, or database.
- Coastlines and country borders are [Natural Earth](https://www.naturalearthdata.com/) 1:110m and 1:50m land and admin-0 countries, public domain.
- ZIP centroids are the [US Census Bureau 2020 Gazetteer](https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2020_Gazetteer/2020_Gaz_zcta_national.zip) ZCTA file, public domain.
- Inter is © The Inter Project Authors, SIL Open Font License 1.1. See `third_party/inter/OFL.txt`.
- RT-21 behavior follows the Green Heron RT-21 manual (Appendix F) and the Version 4 addendum, plus the Hamlib RT-21 backend (`rotators/rotorez/rotorez.c`, model 405) for the tenths goto frame. Green Heron Engineering is not involved with this project.
