# Changelog

## 1.0.0

First public release of Meridian, a controller for the Green Heron RT-21 and RT-21D.

The window is an azimuthal map. The heading is shown on the map. Click the map or the heading number to turn the antenna. Esc or Space stops the rotor.

Find RT-21 looks on USB and COM ports, in numeric order, at 4800 and then 9600, and then on the local network. A serial connection is the COM or USB port. A network connection is TCP to the controller's serial port, 6665 unless that port was changed. Find also tries 6555.

Settings are stored in `meridian.ini` next to the program. The Windows zip does not include that file, and Meridian does not use the registry. The only location filled in on a new install is grid EM26.

Setup reads the controller. On firmware 4.13.2, each Write button stores that one setting. Firmware 4.14 stays locked: on 4.14.g1 the screen saver and register writes hang serial until a power cycle. The network module can be read over USB or serial. The gateway can be written after that read. Other network settings stay read-only.

Traffic logs go in `logs/` next to the program. Tested with RT-21 firmware 4.13.2.
