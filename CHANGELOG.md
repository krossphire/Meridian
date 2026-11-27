# Changelog

## 1.0.0

First public release of Meridian, a controller for the Green Heron RT-21 and RT-21D.

The window is an azimuthal map. The heading is shown on the map. Click the map or the heading number to turn the antenna. Esc or Space stops the rotor.

Find RT-21 looks on USB and COM ports, in numeric order, at 4800 and then 9600, and then on the local network. A serial connection is the COM or USB port. A network connection is TCP to the controller's serial port, 6665 unless that port was changed. Find also tries 6555.

Settings are stored in `meridian.ini` next to the program. The Windows zip does not include that file, and Meridian does not use the registry. The only location filled in on a new install is grid EM26, the center of that square. A 6-character grid, or latitude and longitude saved beside it, is what a nearby bearing needs. When both latitude and longitude are set, those degrees are used and the 6-character square is only a label.

### Test builds

Windows test downloads through delta were named `Meridian-1.0.0-<nato>-<shortsha>-win64`. Each download is one zip whose top folder holds `meridian.exe` and the Qt DLLs. There is no zip inside the zip, and no `meridian.ini`.

## 1.1.0

The window title and the About box read 1.1.0.

Callsign DXCC lookup uses the Big CTY prefix file, vendored as `third_party/cty/cty.dat` and loaded from `data/dxcc-prefixes.tsv`. That covers ordinary prefixes, exact-call overrides, and a short portable indicator. K5PO is the United States, KH6 is Hawaii, KL7 is Alaska, KP4 is Puerto Rico, VP8 is the Falkland Islands, and VP8/G is South Georgia. An exact call such as K6KO stays with its override.

A saved latitude and longitude in `meridian.ini` round-trips and is used as degrees. A 4-character home grid is the center of that square. The same offline tests cover the `AP1` point-and-shoot string and a WSJT-X status datagram.

The Windows test download is one zip named `Meridian-1.1.0-<sha>-win64`. The top folder holds `meridian.exe` and the Qt DLLs. There is no zip inside the zip, and no `meridian.ini`. There is no tag and no release.

A station target (a typed callsign, QLog current contact, or a WSJT-X beam target) draws a dashed cyan line from the center of the disc to Meridian's own bearing. The Beam toward button uses that same cyan. The line is display only. **Show target bearing line** is on by default and is saved in `meridian.ini`. The Sources page explains the QLog Current Contact port. That port stays empty until it is typed.

Setup reads the controller. On firmware 4.13.2, each Write button stores that one setting. Firmware 4.14 stays locked: on 4.14.g1 the screen saver and register writes hang serial until a power cycle. The network module can be read over USB or serial. The gateway can be written after that read. Other network settings stay read-only.

Traffic logs go in `logs/` next to the program. Tested with RT-21 firmware 4.13.2.
