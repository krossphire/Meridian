# Roadmap

1.0.0 is the first public release. It connects to a Green Heron RT-21 or RT-21D, shows the heading, and turns the antenna from the map.

The published download is the Windows zip. Linux builds from source and is what the tests run on. macOS builds from source and has not been tried on a Mac, so it is not packaged.

Setup reads the controller. On firmware 4.13.2 the individual Write buttons store one setting. Grouped Apply, calibration, and the pulse-heading command stay compiled out. The network module is read over USB or serial. Only the gateway is written. Factory reset and the bootloader are not sent.
