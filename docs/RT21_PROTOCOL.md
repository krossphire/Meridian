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

Appendix F writes `WXn(value)` and describes `n = 0` as a reboot. That description is wrong for 4.13.2. See the disassembly section below. On 4.13.2, each confirmed register is sent as `W<letter>0<value>;` from its own Write button, then read back with `R<letter>;`. 4.14 stays locked. Min speed, rotor type, mode, security, the advanced bitfield, the limit switch, magnetic variation, and the multiplier stay locked because their encodings are not in the disassembly notes.

### Brightness, the one unlocked write

Register O, default 8. The Write button sends `WO0` plus the decimal plus `;`. Brightness 10 is `WO010;` (`57 4F 30 31 30 3B`). On 16 November, over IP to 192.168.1.106:6555 with firmware 4.13.2, `WO08;` was acked `06 4F 3B` in 161 ms, `RO;` read back 8, and the following 10 seconds of heading polls returned 5 replies and no timeouts. The value is the one that was written. The controller answers `06 4F 3B` before it stores. Meridian waits for that ack, pauses 150 ms, sends `RO;`, then watches heading polls for 10 seconds. No line feed is sent. Has no visible effect on OLED RT-21 units (LCD units only, unverified).

The button never writes by itself. A read must already have reported 4.13.2, the poll in flight has to finish, and no `AT` byte may have gone out in the previous 5 seconds. The traffic log records the set and the read-back as `WRITE-TX` and `WRITE-RX` with timestamps, then a note of how many heading replies arrived in the next ten seconds. The status line is one of: `Brightness written and verified`, `Brightness read-back mismatch: wrote X, read Y`, or `RT-21 stopped answering after write. Power-cycle it.`

## From 4.13.2 disassembly, static analysis

These points are from a disassembly of the official `RT21_4_13.2.hex` (PIC18F26K22, byte-identical to the Green Heron release). They are static analysis, not a hardware test. The firmware file is the current public release at [greenheronengineering.com/rotator-controllers-software](https://www.greenheronengineering.com/rotator-controllers-software/).

A rotator command runs only when its last byte is `;`. A line feed is never a terminator for those commands. A stray line feed after `;` corrupts the next command or poll. Meridian does not send a line feed on rotator commands. The network module is different: after the pass-through switch, its AT* lines end in LF and must not end in `;`.

A read is `R` plus a letter plus `;`. The digit after the letter is ignored. The reply is `<letter>` `0x01` `<value>`. `O` is brightness (default 8). `W` is screen-saver minutes.

A write is `W` plus a letter plus a digit 0–9 plus the value plus `;`. The controller replies `0x06` `<letter>` `;` (brightness is `06 4F 3B`) before the EEPROM write. Digit 0 stores and reloads every setting, so the change applies live. That is the form the utility uses. Digits 1–9 store only, and apply at the next reload or power-up. Neither form reboots the controller. `W0...;` is a different command: it displays a message with interrupts off for about 2 seconds. Meridian does not send it.

After the ack, interrupts are off for about 25–30 ms and the receive buffer is cleared. Meridian waits 150 ms, then sends `RO;`.

The serial network-module sequence below is from two Device Monitoring Studio captures of Setup Utility 4.B12 on COM6, firmware 4.13.2 (two successes and five failures, identical bytes on both successes). It replaces the earlier disassembly guess. `T11;` and a semicolon on an AT* line failed in those captures, and `T11;` alone made the controller ignore `R01;` until ESC. Alpha25f sends `T11;` on a serial connection only: on connect it sends `R11;`, then `T11;` and `R01;` with no wait for a T11 reply, and it sends `T11;` again immediately before each `AT;`. It is never sent on an IP link. `T01;` is not sent. The `WW0` write-back of register W is not sent. `R[;` (magnetic variation) is not in 4.13.2; a missing reply is recorded and does not fail the rest of the read, and the setting stays locked.

Firmware delays of 1–250 ms run with interrupts off. Polls stay spaced, and a single missed reply is retried once without changing the status line.

Screen-saver polls do not count as activity. On 4.13.2 the saver blanks the panel and serial keeps answering; a turn command wakes the display. On 4.14.g1 the saver and a register write hang serial until a power cycle. 4.13.2 is the firmware to run, especially on an OLED unit.

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

Meridian is designed and tested with Andrew's RT-21, which reports `RT-21 4.14.g1`. That build is not on the public download page. Fetched 15 Nov 2026, [greenheronengineering.com/rotator-controllers-software](https://www.greenheronengineering.com/rotator-controllers-software/) lists RT-21 firmware 4.13.2 as current (version 4, serial numbers 4000 and up). 4.14 is not a download. Setup Utility 4.B12 (beta, December 2023) is the program that mentions it: "Supports 4.14 w/Serial Sensor feedback", "Magnetic Variation with pre-release 4.d14", and "Fixes bug that always cleared the Screen Saver function in RT-21". Utility 4.11 (December 2022) is the non-beta. 4.13.x is expected to speak the same protocol and has not been tested here. A read that is not 4.13.2 gets a status-line note. 4.14 gets: "Firmware 4.14 is a pre-release; on 4.14.g1 the screen saver and register writes hang serial. 4.13.2 is recommended." The only write in this build is display brightness, and only after a read that reports 4.13.2. Every other write stays compiled out. The public firmware is [4.13.2](https://www.greenheronengineering.com/rotator-controllers-software/).

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

On 4.14.g1 a 1-minute saver stops serial and network replies until a power cycle, and a button that wakes the display does not wake the port. That warning is only for 4.14. On 4.13.2, with display firmware 3.1 at boot, the saver blanks the panel and Meridian kept polling (280 RX bytes, heading updates). A turn command woke the screen and turned the beam. The disassembly says polls do not count as saver activity. Meridian shows the Off / minutes control and does not write it. OLED units should run 4.13.2.

The 4.B12 release note says that build fixes a bug that always cleared the screen saver. Utility 4.11's write prefix is the string `WW0` plus two trailing spaces. 4.B12's prefix is `WW0` with the spaces removed. A value glued onto the 4.11 prefix is not a decimal the controller can store, so the saver never engaged. Off is the separate literal `WW00` (`n = 0`, which reboots). Meridian does not write W from any group except Display, so saving speed, limits, or the network cannot clear it.

## Network read on the serial port

Ground truth from Setup Utility 4.B12 on COM6 at 4800 8N1, no flow control, firmware 4.13.2. The 18 November 2026 captures are in `captures/`. The utility succeeds; alpha25a and alpha25b get `<uart2>;` and then nothing. The byte-for-byte comparison is in [REVIEW-D.md](../REVIEW-D.md).

The utility's CreateFile leaves DTR low for about 20 ms with RTS on, then enables DTR. Its timeouts are ReadInterval and ReadTotalMultiplier MAXDWORD and ReadTotalConstant 2000. Its wait mask is 0x1FB and its queue is 4096 in each direction. A cold open then sends `R11;`, then `T11;` and `R01;` with no wait for a T11 reply, then two ESC bytes 0.7 ms apart, then `;` about 1510 ms later. Alpha25f sends that `R11;` / `T11;` / `R01;` trio on a serial connect, and it sends `T11;` again immediately before each `AT;`. It is never sent on an IP link. `T01;` is not sent. A network read still drops DTR for 20 ms with RTS on, then sends those two ESC bytes and the `;`. Timeouts and the wait mask on that Qt-owned handle stay with Qt's reader. The utility's 2 s timeout and mask 0x1FB are applied on the raw handle opened after `<uart2>;`.

Before `AT;` the utility polls `R21;` and then `R01;` about 20 ms after the status reply, with the next `R21;` about 210 ms after the pot reply. The quiet gap before `AT;` was 412 ms on the first 18 November success and 704 ms on the second. Meridian uses that pair for 3 s before the first `AT;` and 5 s before a retry, capped at 15 s, then sends nothing for 650 ms. The log line is `network pre-AT quiet N ms`. The network read does not send `BI1;`, `R41;`, `RC1;`, `R[1;`, or `WW02;`. `WW02;` is a screen-saver write. No command that starts with `W0` is sent.

The captured successes send `AT*SERIAL_ECHO=0` about 730 ms after `<uart2>;`. DTR goes low about 520 ms after that reply, close is about 9 ms, open is about 40 ms after close, and DTR stays low for 190–197 ms. The echo is 3–14 ms after the final SET_DTR. Alpha25a used the same reopen and the same echo bytes, and the module still sent nothing, so that half of the sequence is necessary and was not the miss.

1. Lead-in, then the poll pair, then 650 ms of quiet, then TX `AT;` (`41 54 3b`). Expect `<uart2>;` (`3c 75 61 72 74 32 3e 3b`) in about 50 ms. No `R01;` glued onto `AT;`. The first utility success did glue one, 0.4 ms later, and that `R01;` got no reply of its own. The second success and the 15 November capture did not glue it.
2. About 520 ms after `<uart2>;`, the close and open replays the utility and the echo goes out as soon as the open finishes. Close is SetCommMask(0), CLRDTR, PurgeComm RX abort+clear, PurgeComm TX abort+clear, CloseHandle. Reopen is CreateFile with FILE_FLAG_OVERLAPPED, GetCommProperties, GetCommModemStatus, SetCommState at 4800 8N1 with fDtrControl disabled and fRtsControl enabled (Xon 0x11, Xoff 0x13, Eof and Evt 0x1A, XonLim and XoffLim 0x200), SetCommState again with fDtrControl enabled, SETDTR, then SetCommTimeouts (ReadInterval and ReadTotalMultiplier MAXDWORD, ReadTotalConstant 2000), SetCommMask(0x1FB), SetupComm(4096, 4096). The read on that handle is overlapped, so the GUI thread is not blocked inside ReadFile. Every try is this close and open. Each try logs the DTR-low time and uart2→ECHO. A success is logged as `network success variant D`.
3. One `AT*SERIAL_ECHO=0` plus LF. On the first 18 November success the module answered `ERROR 1` in 47 ms and `OK` to the resend. On the second it answered `OK` in 63 ms with no resend. Meridian waits 700 ms. `ERROR 1` gets one immediate resend. Silence does not: it listens another 400 ms, then ESC. It does not send a third echo. The module drops command mode after about 25–41 s idle. Meridian does not send `AT*RESET`.
4. Queries are `AT*KEY?` plus LF, one at a time, read to CR LF before the next. Observed replies are 25–45 ms; Meridian allows 300 ms. Order: HWTYPE, DHCP, IP, AIP, NMASK, GW, AGW, ID, HTTP, UDP, TFTP, HTTP_USER, HTTP_PASS, NVREG16_0, LINK. The utility's own read skips AGW. Meridian keeps `AT*AGW?` because the gateway write compares it, and that query is after a successful echo. On this station before the utility's write: HWTYPE=2, DHCP=0, IP=AIP=192.168.1.106, NMASK=255.255.255.0, GW=192.168.2.1, ID=RT-21, HTTP=801, UDP=23073, TFTP=69, HTTP_USER empty, HTTP_PASS empty, NVREG16_0=6555, LINK=1. The utility then wrote `AT*NVREG16_0=6665` and `AT*RESET=1`. The second read returned NVREG16_0=6665.
5. The only network write is the gateway, and only over USB or serial, after a read succeeded on this connection. It is a fresh entry, the same handshake as a read, then `AT*AGW=<dotted-quad>` plus LF, `OK`, `AT*AGW?`, `AT*GW?`, and ESC. A matching AGW read-back is `Saved Gateway`. Anything else is `Write not confirmed: Gateway`. Other keys stay read-only.
6. Exit is a single ESC (`0x1b`) on every try, including a failed reopen and a try that never got `<uart2>;`. No second DTR cycle on the way out, and no `T01;`. Meridian then waits for a heading (`R21;`). It does not start the next try without one.

If that heading does not arrive, Meridian sends ESC again, transmits nothing for 35 seconds (pass-through times out after about 31 seconds of quiet), and polls once more. If there is still no heading it stops the read with `Controller stopped answering. Power-cycle the RT-21.` The same stop happens when the 15 second warmup cap expires and the quiet poll is still silent.

If `AT*SERIAL_ECHO=0` gets no OK after `<uart2>;` arrived, Meridian sends ESC and, once a heading is back, waits so the next `AT;` is at least 35 s after the previous one, then repeats the lead-in and the 5 s poll. A try that never got `<uart2>;` does not wait that 35 s. The cap is 12 tries. Cancel sends ESC the same way. The status line is `Reading network (try n of 12)…`, then `Network module answered. Nothing was written.` or `Network module didn't answer after 12 tries`. The intentional close is not a disconnect and does not turn the status red. An IP-only link still cannot read or write network settings.

## Network module TCP port

The Version 4 addendum (rev 4, November 2022), section “WiFi/LAN Operation”, says SETUP’s **Ports (Advanced and Optional)** chooses ports “other than the defaults.” It describes the HTTP port as the one a browser uses, and says that choosing 80 means the browser URL needs no port. The only numeric HTTP example in that section is `http://192.168.1.4:8080`. The same paragraph says **Serial – TCP Operation** sets the TCP port for serial-over-TCP programs such as GH Everyware and PST Rotator, on firmware 4.13 and later. It does not print a factory number for that serial-TCP port.

Hamlib’s RT-21 backend (`rotators/rotorez/rotorez.c`, `rt21_rot_caps`, version string 20230507.0) is `RIG_PORT_SERIAL` at 4800 8N1. It has no TCP port.

**Unverified on a live module:** a factory default for the serial-TCP port. Setup Utility 4.B12's "RESET to DEFAULTS" writes `AT*NVREG16_0=6555`. The addendum and Hamlib do not print that number. On 18 November 2026 the utility wrote `AT*NVREG16_0=6665` on this station and the following read returned 6665. Find RT-21 does not assume either number. Its network scan tries 6665 and then 6555, plus a custom port when one was typed, and lists a host only when `BI1;` parses as a heading. The module's other settings, and the warning that `T11;` leaves TCP mode, are in `docs/RT21_SETUP_PARITY.md`.
