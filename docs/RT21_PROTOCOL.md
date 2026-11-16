# RT-21 command map

Sources, both from Green Heron Engineering:

- RT-21 manual 3.05 (8 August 2014), Appendix F, “Communications Protocol”
- RT-21 Version 4 addendum, rev 4 (November 2022), “New Protocol commands”

The protocol is a superset of Hy-Gain DCU-1. Commands are ASCII, case-insensitive, and end with `;`. There is no inter-character timeout. A bare `;` also clears the receive buffer. Serial defaults are 4800 8N1 (9600 is an advanced-option bit and a SETUP Utility choice). USB and the LAN/Wi-Fi serial-TCP port carry the same byte stream. `n` below is a digit 0–9; Meridian always sends `1`.

## USB chips

Older RT-21s use an FTDI USB chip. Serial numbers 2000 and up use the standard FTDI driver, which current Windows already has through Windows Update. Serial numbers under 2000 need Green Heron's INF from the [software page](https://www.greenheronengineering.com/rotator-controllers-software/). Units shipped from November 2022 use a Silicon Labs CP210x instead, because FTDI parts were short. Andrew's unit is one of those. The CP210x driver is the [Silicon Labs VCP package](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers); Green Heron hosts a copy on the same software page. The port enumerates even when the rotor is off. Find RT-21 probes an FTDI COM port and a CP210x COM port the same way. A chip with no COM port is only a possible RT-21, and the hint names the driver that chip needs. An unknown device whose descriptor request failed (Windows problem code 43, often `VID_0000&PID_0002`) is not identified as an RT-21.

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

Appendix F writes `WXn(value)`. `n = 0` reboots after the write; any other digit stores it. Meridian sends `n = 1` and the value as decimal ASCII with no extra separator, for example offset 180 is `WD1180;`. That concatenation is the natural reading of `WXn(value)`. Only brightness is sent by this build. Every other location stays compiled out.

### Brightness, the one unlocked write

Register O. Read is `RO1;`. Setup Utility 4.B12's brightness combo calls `GetRotorData` with the concatenation of the literal `WO0` and the combo text. `GetRotorData` sends that string with `SerialPort.WriteLine`, and that port's `NewLine` is `\n`. A change to 1 is therefore the bytes `WO01` plus a newline (`57 4F 30 31 0A`). The `0` is `n = 0`, which Appendix F says reboots the controller. That matches the 16 November session: the value persisted, then serial died and the utility sat there. Those exact bytes were not captured on the wire.

Meridian does not send the reboot form. The Write button sends one command, `WO1` plus the decimal plus `;`, then immediately `RO1;`, then ten seconds of the usual heading polls (`BI1;` or `AI1;`). Brightness 1 is `WO11;` (`57 4F 31 31 3B`). Brightness 10 is `WO110;` (`57 4F 31 31 30 3B`). The allowed range in the control is 0 through 15, so 0 is `WO10;`. This `n = 1` form is the utility's file-restore prefix `WO1` plus the semicolon every other Meridian command uses. It has not been confirmed on hardware. Has no visible effect on OLED RT-21 units (LCD units only, unverified).

The button never writes by itself. A read must already have reported 4.14.g1. The traffic log records the set and the read-back as `WRITE-TX` and `WRITE-RX` with timestamps, then a note of how many heading replies arrived in the next ten seconds. The status line is one of: `Brightness written and verified`, `Brightness read-back mismatch: wrote X, read Y`, or `RT-21 stopped answering after write. Power-cycle it.`

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

## Firmware this program is built against

Meridian is designed and tested with Andrew's RT-21, which reports `RT-21 4.14.g1`. That build is not on the public download page. Fetched 15 Nov 2026, [greenheronengineering.com/rotator-controllers-software](https://www.greenheronengineering.com/rotator-controllers-software/) lists RT-21 firmware 4.13.2 as current (version 4, serial numbers 4000 and up). 4.14 is not a download. Setup Utility 4.B12 (beta, December 2023) is the program that mentions it: "Supports 4.14 w/Serial Sensor feedback", "Magnetic Variation with pre-release 4.d14", and "Fixes bug that always cleared the Screen Saver function in RT-21". Utility 4.11 (December 2022) is the non-beta. 4.13.x is expected to speak the same protocol and has not been tested here. A read that is not 4.14.g1 gets a status-line note. The only write in this build is display brightness, and only after a read that reports 4.14.g1. Every other write stays compiled out.

## Observed on hardware

On 15 November 2026, firmware 4.14.g1 on COM6 through a Digi Edgeport/8, the controller's own screen saver was set to 1 minute. About a minute after the rotor went idle it stopped answering serial. Meridian, Find RT-21, and the Green Heron setup utility all received zero bytes until the unit was power-cycled. Pressing Setup or Cancel woke the display and did not wake the serial port. With the saver off, Meridian polled for 22 minutes with no timeouts. Treat the hardware screen saver as unsafe on this firmware. Leave it Off.

On 16 November 2026 the same firmware, on an OLED unit, had its brightness changed from 10 to 1 by Setup Utility 4.B12. Meridian's next read was 1, and the value was still 1 after a power cycle, so the write was stored. The OLED panel did not look any different. Brightness on this hardware is treated as an LCD-only control; that is unverified on an LCD unit. After the utility wrote the value it froze, and the RT-21 stopped answering serial until it was power-cycled. It is not clear whether the write itself or the utility's wait caused that.

## Earlier hardware notes

A healthy session on 14 November 2026, from 14:06:18 to about 14:12, stayed on one TCP connection the whole time. There were no reconnects and 646 RX frames. The earlier dead session (13:44–14:03, zero RX, then a reconnect that resent the same `AP`) came right after a rapid reconnect loop. That matches the quiet-backoff theory: the network module wedges when Meridian drops the socket and dials straight back.

On that healthy session, replies to `BI1;` were plain ASCII `NNN.N;` with no leading space and no CR/LF. One captured frame:

```text
31 33 31 2E 39 3B    131.9;
```

About every 4th to 6th frame arrived with a 4-byte prefix. Idle:

```text
01 30 00 3E 31 33 32 2E 34 3B    <01> '0' <00> '>' 132.4;
```

While the rotor was turning:

```text
01 30 01 3E 31 32 32 2E 36 3B    <01> '0' <01> '>' 122.6;
```

The byte after `01 30` behaves as a moving flag: `00` stopped, `01` rotating, then `>` (`3E`) and the same `NNN.N;` heading. It is not requested by `R21;`. It looks like an unsolicited status frame from the network module, mixed into the `BI1;` replies. Meridian strips leading non-numeric bytes, treats `01 30 xx 3E` as that status header, and uses `xx` as the rotating flag when it is 0–5. A stopped flag is the settled signal.

Replies on that session arrived about every 2 seconds while idle and about every 250 ms while the rotor was moving. Those are the rates the controller was actually answering, not a reason to reconnect.

A setup read on 15 Nov 2026, firmware 4.14.g1, one TCP session to 192.168.1.106 port 6555, answered EEPROM queries as `<letter><SOH><value>;`. The letter is the echo of the register, then `0x01`, then the decimal text. The same shape covers the version and the pot:

```text
41 01 35                A <01> 5
42 01 37 30 35          B <01> 705
46 01 31 32 35          F <01> 125
4A 01 31 30             J <01> 10
4B 01 33 39 36 30       K <01> 3960
54 01 32 35 37 36       T <01> 2576
31 01 52 54 2D 32 31 20 34 2E 31 34 2E 67 31
                        1 <01> RT-21 4.14.g1
30 01 30 33 33 34       0 <01> 0334
```

`R11;` is the version (firmware `RT-21 4.14.g1`). `R01;` is the pot (`0334`). `R41;`, the last abnormal reset, produced no semicolon-terminated frame inside the reply window. The driver's "no reply" text is that timeout, not a payload. The next frame was a clean `A<01>5`, so the reset reply was not stuck in front of it, and the link was still up. The manual says `R4` reads and clears a stored reset code. Silence when nothing is stored fits this capture. It is not proof that 4.14.g1 dropped the command.

Registers C, W, and Z were absent from that backup because the file only stored the rows on the form. The driver did send `RC1;`, `RW1;`, `RZ1;`, and `R[1;`. C is reserved and the utility does not show it. W is the screen saver and Z is the multiplier in utility 4.B12. `[` is magnetic variation. Alpha 8 keeps those rows and writes them into the text copy.

## Screen saver

Version 4 addendum, rev 4, "Display Screen Saver operation [4.13]": controller firmware 4.13 adds a setup item for a screen saver of up to 1 hour. Off is the default. It requires display firmware 1.3, shown at the bottom left at boot. With that display module the panel blanks until a button is pressed or a remote command is initiated. The addendum says this is for the planned OLED option. An OLED has no brightness control, unlike the LCD and VFD. LCD and VFD units blank too if their display firmware is 1.3. The display firmware is not field-upgradable. "Display 1.3 FW supports the Screen Saver mode that is introduced in RT-21 v4.13."

Utility 4.B12's combo is labeled Minutes. The items are Off, 1, 2, 5, 10, 20, 30, 40, 50, 60. Read is `RW1;`. Zero is shown as Off. There is no 90-second step and no separate full-black versus dim level. The blank is the whole behavior on display firmware 1.3. Auto-dim is a different switch, advanced-options bit 16, and brightness is register O. The addendum says an OLED ignores brightness.

Do not enable this saver as burn-in protection. The 15 November observation above is the reason: on 4.14.g1 a 1-minute saver stops serial and network replies until a power cycle, and a button that wakes the display does not wake the port. The recommended stored value is Off (`W = 0`). Meridian does not write it.

What the addendum counts as a wake: a button, or a remote command. On this firmware that wake is the display only. The port stays silent. Leave the saver Off. There is no idle-dim stand-in: brightness does nothing visible on the OLED unit above.

The 4.B12 release note says that build fixes a bug that always cleared the screen saver. Utility 4.11's write prefix is the string `WW0` plus two trailing spaces. 4.B12's prefix is `WW0` with the spaces removed. A value glued onto the 4.11 prefix is not a decimal the controller can store, so the saver never engaged. Off is the separate literal `WW00` (`n = 0`, which reboots). Meridian does not write W from any group except Display, so saving speed, limits, or the network cannot clear it.

## Network read on the serial port

4.B12's `PollWiFiValues` sends `T11;` (leave TCP mode, open the module passthrough), then `AT;`, then `AT*DHCP?` and the other queries. Those `AT*` strings do not include a carriage return. The utility writes them with `SerialPort.WriteLine`, and that class's `NewLine` is `\n`. Meridian was sending `\r`. On the Edgeport at 4800 the utility's own window is sometimes junk and sometimes clean, so the failure is not a wrong baud: the CPU is already at 4800, which is the baud the utility used when the window came up. The junk is a timing and framing mix. A heading or status byte still in the UART, or `T11;` not finished switching, lands in the middle of `DHCP=1`. Meridian now sends the queries with `\n`, looks for the token anywhere in the printable text, retries the `AT;` handshake twice and each query once, and always finishes with Escape and `T01;` even when a read fails. Every byte of that session is hex-logged in `logs/` next to the program, whether or not the traffic checkbox is on. No `AT*` write, no `AT*RESET`, and no module defaults are sent.

## Network module TCP port

The Version 4 addendum (rev 4, November 2022), section “WiFi/LAN Operation”, says SETUP’s **Ports (Advanced and Optional)** chooses ports “other than the defaults.” It describes the HTTP port as the one a browser uses, and says that choosing 80 means the browser URL needs no port. The only numeric HTTP example in that section is `http://192.168.1.4:8080`. The same paragraph says **Serial – TCP Operation** sets the TCP port for serial-over-TCP programs such as GH Everyware and PST Rotator, on firmware 4.13 and later. It does not print a factory number for that serial-TCP port.

Hamlib’s RT-21 backend (`rotators/rotorez/rotorez.c`, `rt21_rot_caps`, version string 20230507.0) is `RIG_PORT_SERIAL` at 4800 8N1. It has no TCP port.

**Unverified on a live module:** a factory default for the serial-TCP port. Setup Utility 4.B12's "RESET to DEFAULTS" writes `AT*NVREG16_0=6555`. The addendum and Hamlib do not print that number. Find RT-21 does not assume it. It probes 23, 4001, 5000, 6555, 8080, and 10001, plus any port already saved and an optional custom port, and lists a host only when `BI1;` parses as a heading. The module's other settings, and the warning that `T11;` leaves TCP mode, are in `docs/RT21_SETUP_PARITY.md`.
