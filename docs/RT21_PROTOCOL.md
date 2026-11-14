# RT-21 command map

Sources, both from Green Heron Engineering:

- RT-21 manual 3.05 (8 August 2014), Appendix F, “Communications Protocol”
- RT-21 Version 4 addendum, rev 4 (November 2022), “New Protocol commands”

The protocol is a superset of Hy-Gain DCU-1. Commands are ASCII, case-insensitive, and end with `;`. There is no inter-character timeout. A bare `;` also clears the receive buffer. Serial defaults are 4800 8N1 (9600 is an advanced-option bit and a SETUP Utility choice). USB and the LAN/Wi-Fi serial-TCP port carry the same byte stream. `n` below is a digit 0–9; Meridian always sends `1`.

Commands that return a value, other than `AI1;`, are documented to put SOH (`0x01`) ahead of the payload. Meridian accepts an optional SOH on every reply.

## Motion and heading

| Command | Manual meaning | Meridian |
| --- | --- | --- |
| `;` | Stop, short ramp down | Sent for Esc / Space |
| `STn;` | Stop, short ramp down | Recognized by the mock; the app sends `;` |
| `AIn;` | Heading, 3 digits, reply `xxx;` | Available; normal poll uses `BI1;` |
| `BIn;` | Heading in tenths, reply `xxx.y;` | Position poll |
| `APnxxx(.y);` | Preset the next `AMn;` target. Optional tenths | Supported |
| `APnxxx(.y)<CR>;` | Go to that heading immediately, no `AMn;` | What the app sends. Same bytes as Hamlib: `AP1%05.1f\r;` |
| `AMn;` | Move to the last `AP` heading | Supported |
| `R2n;` | 2014 manual: heading and a running/stopped flag. v4 addendum: `SOH '0' <status> '>' nnn.n ';'` with status 0 idle, 1 moving, 2 error/no motion, 4 pot out of range, 5 counter range | Polled a few times a second. v4 layout is parsed; a plain heading is accepted too |

Azimuth sent to the rotor is clamped to one tenth of a degree in `[0.0, 359.9]`, matching the Hamlib RT-21 `max_az`. Values that round to 360 become `0.0`.

The app does not know whether the TCP bridge needs the CR. It sends Hamlib’s `AP1xxx.y\r;` on serial and on TCP. The mock accepts the command with or without CR. A second TCP client is refused; the driver says so when the connect fails.

## Queries Meridian reads

| Command | Reply | Use |
| --- | --- | --- |
| `R0n;` | SOH + pot A/D, 0–1023 | Shown on the Controller page |
| `R1n;` | SOH + `RT-21 Version X.Y` | Shown on the Controller page |
| `R4n;` | SOH + last abnormal reset code, then cleared | Shown on the Controller page |
| `RXn;` | SOH + EEPROM value, `X` = `A`–`Z` | Every location is read |

## EEPROM locations (Appendix F)

Defaults in parentheses are the manual’s. Meridian displays the raw reply next to each field. A field is editable only when that reply is a plain integer.

| ID | Manual name | Default | Written |
| --- | --- | --- | --- |
| A | CCW cal | 0 | yes, if numeric |
| B | CW cal | 950 | yes, if numeric |
| C | Reserved | | read only |
| D | Cal offset | 180 | yes |
| E | Delays | 3 | yes. The manual does not split brake delay out of this field |
| F | Min speed | 3 | yes |
| G | Max speed | 10 | yes |
| H | CCW limit | 180 | yes |
| I | CW limit | 180 | yes |
| J | Option | Pot | no. The manual gives the word, not the code |
| K | Pulse divider | 3960 | yes |
| L | Cal range | 360 | yes |
| M | Mode | Normal | no |
| N | Ramp | 3 | yes |
| O | Brightness | 8 | yes |
| P | Rotor fail timeout | 4 | yes |
| Q | Security | Admin | no |
| R | Alt offset 1 | 361 | yes |
| S | Alt offset 2 | 361 | yes |
| T | Advanced options | 0 | yes, as the bitfield below |
| U | Overtravel, degrees | 90 | yes |
| V | Counter-rotate delay | 10 | yes |
| W | Reserved | | read only |
| X | Limit switch position | | read only |
| Y | Knob time | 40 | yes |
| Z | Not used | | read only |

`T` bits from the same page: 1 freeze display, 2 fast brake release, 4 auto go-back, 8 elevation mode, 16 auto dim, 32 analog heading out, 64 EME, 128 point-and-shoot shortest route, 256 port = 9600, 512 2-bit encoder, 1024 demo, 2048 allow cal to the CCW limit.

## Writes Meridian will send

Appendix F writes `WXn(value)`. `n = 0` reboots after the write; any other digit stores it. Meridian sends `n = 1` and the value as decimal ASCII with no extra separator, for example offset 180 is `WD1180;`. That concatenation is the natural reading of `WXn(value)` and **has not been checked on a real RT-21**. Apply refuses to run until a backup file has been written, and the Controller page says so.

Also behind an explicit confirmation:

| Command | Meaning |
| --- | --- |
| `C0n;` | Calibrate CCW at the current antenna position |
| `C1n;` | Calibrate CW at the current antenna position |
| `AWnxxx(.y);` | Set the pulse-counter heading. Sent as `AW1` + `xxx.y` + `;` |

## Documented, and not sent

| Command | Why it stays off the wire |
| --- | --- |
| `W00;` | Loads default EEPROM and resets the controller |
| `W0n(callsign);` | Vanity boot line. The manual’s parentheses don’t say whether they are literal |
| `AVn;` | Counter-rotation traffic between controllers |
| `AAn;` / `ABn;` | Jog CCW / CW for 1.5 s. The mock understands them; the window doesn’t send them |
| `R3n;` | CPU reset (v4) |
| `B2n;` / `BUn;` | Enter the bootloader |
| `ATn;`, `T0n;`, `T1n;`, `<esc>` | Wi-Fi / LAN module passthrough |

`R3n;` and the bootloader commands are the sort of thing a mis-click should not be able to do.
