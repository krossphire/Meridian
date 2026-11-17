# RT-21 Setup Utility parity

Meridian should replace the Green Heron RT-21 Setup Utility. This file is the inventory of every setting that utility exposes, the bytes it sends, and whether those bytes have been seen on a live rotor. Alpha 7 can read them and save a plain-text copy. It does not write them.

**Verified** means the bytes were seen on a live RT-21 (Andrew's session of 14 Nov 2026, or a later one). **Unverified** means the command is printed in a Green Heron manual or is a literal string in Setup Utility 4.B12, and has not been seen on Andrew's rotor. Do not treat Unverified as safe to send.

Writes stay compiled out (`MERIDIAN_ALPHA_READONLY`). A future write, once a group is approved, saves a copy under `backups/`, sends the group, reads it back, and compares. One group at a time. Factory reset, the bootloader, and the module's "reset to defaults" stay excluded.

## Sources

- RT-21 manual 3.05 (8 August 2014), Appendix F. EEPROM letters, defaults, and `WXn(value)`. `n = 0` reboots after the write. Any other digit stores it.
- RT-21 Version 4 addendum, rev 4 (November 2022), [greenheronengineering.com](https://www.greenheronengineering.com/wp-content/uploads/2022/11/Version-4-rev-4.pdf). `T0n;`, `T1n;`, `AT;`, escape, `R2n;`, `R3n;`, `R4n;`, bootloader. Wi-Fi/LAN section: rotator name, DHCP or static IP, mask, gateway, Wi-Fi security, SSID, key, HTTP port. Example browser URL `http://192.168.1.4:8080`. It does not print a factory serial-TCP port, a DNS server, or an AP/client switch.
- Version 4 manual rev 2 (August 2019). Same Wi-Fi window. It says UDP and Serial were "for future use" and web login was not in the first release.
- Setup Utility 4.B12 (December 2023) and 4.11 (December 2022), zip files on the [software page](https://www.greenheronengineering.com/rotator-controllers-software/). 4.B12 is a .NET program (`RT-21 Setup Utility.exe`, file version 1.0.0.0, assembly 1.0.0.35). The command strings below are `ldstr` operands in that binary, tied to the named method. 4.11 has the same `AT*` set; 4.B12 adds magnetic variation.
- Firmware page, same site: 4.13.2 is current. 4.13 adds LAN and direct TCP for Ethernet or Wi-Fi. Version 4 firmware is only for serial numbers above 4000. 4.12 was a limited LAN/TCP build; Green Heron says to update it to 4.13.
- Product page for the controller: the utility also changes the vanity boot line, shows the rotation range, sets security, the no-motion timeout, auto-cal, and save/restore of a `.ghr` file.
- groups.io/g/greenheron is members-only. No public post was used as a source for a command.

## How a setting is approved

On Andrew's rotor, one group at a time:

1. Read every field and save `backups/meridian-setup-*.txt`.
2. Change one harmless value. Brightness (`O`) is the first candidate.
3. Read it back and compare.
4. Restore the previous value and read it back again.
5. Only then compile that group in.

Do not enable a second group in the same session.

## What can brick the rotor or drop the link

| Action | Why it is dangerous | Meridian |
| --- | --- | --- |
| `W00;` | Loads default EEPROM and resets the CPU. The utility sends this for factory reset and when the rotor type changes. | Excluded |
| `R3n;` | CPU and display reset. Same as power cycle. | Excluded |
| `B2n;` / `BUn;` | Bootloader on RS-232 or USB. | Excluded |
| `W` with digit `0` | Appendix F called this a reboot. The 4.13.2 disassembly says digit 0 stores and reloads the setup, and does not reboot. The utility's live brightness control is `WO0` plus the value plus `;`. | Brightness only, after a 4.13.2 read: `WO010;` for 10. |
| `T1n;` (`T11;`) | Addendum: leaves TCP mode so AT commands work. Sent over an IP connection, this can drop the link you are using. On 4.13.2 serial, captured reads that sent `T11;` failed, and `T11;` alone made the controller ignore `R01;` until ESC. | Not sent. Network reads and writes are USB or serial only, and they use the captured `AT;` / port-cycle / LF sequence. |
| `AT*RESET=1` | Utility button "Re-Connect". Reboots the module. | Excluded |
| Utility "RESET to DEFAULTS" | Writes DHCP static, `192.168.2.100`, gateway `192.168.2.1`, serial port `6555`, SSID `NETGEAR24`, a passphrase, and an adhoc name `RT-21`. The link will not come back on the old address. The passphrase is a known string in the program. | Excluded |
| Address, DHCP, or serial-TCP port write | The IP session dies. The new address has to be used afterward. | Not sent. The guide says so, and offers a reconnect only once writes exist. |
| Calibration `C0n;` `C1n;` `AWn;` and letters A, B, K | A bad cal makes the heading wrong until it is put back. | Read-only |
| Rotor type `WJ0` | The utility offers a factory reset as part of changing type. | Read-only. Meridian will not send `W00;` with it. |

## Session for the network module

The utility's constructor, then `PollWiFiValues`, then the Wi-Fi form's close:

1. `AT*SERIAL_ECHO=0` (only if it is already in AT mode; not part of Meridian's read)
2. Escape, escape, `;`
3. `AI1;`
4. `T11;` — allow AT passthrough, leave TCP mode
5. `AT;` — expect `OK`. Addendum: 30 second activity timeout, then the module returns to the rotor CPU
6. `AT*HWTYPE?` — the utility continues only when it sees `HWTYPE=2`. Other types are not decoded in the program.
7. The queries in the table below. Each reply is a `NAME=value` line. The utility treats `OK` as success for a write.
8. Escape and `;` leave AT mode (form close)
9. `T01;` on the main window close — put TCP mode back

The list above is the utility's older IL. Captures of 4.B12 on 4.13.2 serial do not use `T11;` or `T01;` for the read. Meridian follows the capture: poll, `AT;` and `<uart2>;`, close and reopen, `AT*SERIAL_ECHO=0` plus LF, then `AT*KEY?` plus LF, then ESC, close and reopen, and a poll. See [RT21_PROTOCOL.md](RT21_PROTOCOL.md). The Wi-Fi key and the web password are replaced with `****` in the traffic log.

`AT*` commands in that capture end in a line feed and not a semicolon. A semicolon on those lines failed. Semicolon remains the rotator-command terminator.

## Controller

Reads use `R` + letter + `1;`. A numeric write, when that group is later enabled, is `W` + letter + `1` + decimal + `;` (`WD1180;` for offset 180). The utility's on-screen controls use `n = 0` instead (`WD0180`, and the slider prefixes `WO0`, `WF0`, `WG0`, `WN0`, `WH0`, `WI0`, `WL0`, `WK0`, `WU0`, `WV0`, `WY0`, `WZ0`, `WT0`). File restore uses `n = 1` (`WA1`, `WB1`, `WD1`, … `WZ1`) except rotor type, which stays `WJ0`. Both shapes are Unverified on hardware. Meridian will use `n = 1` so a write does not reboot the controller.

The `.ghr` restore file uses these names: `ccwcal`, `cwcal`, `caloffset`, `reversedelay`, `minspeed`, `maxspeed`, `ccwlimit`, `cwlimit`, `rotortype`, `pulsedivider`, `rampdelay`, `brightness`, `motortimeout`, `security`, `altoffset1`, `altoffset2`, `advanced`, `otallow`, `crdelay`, `limitposition`, `multiplier`.

| ID | What you see | Manual default | Read | Write the utility actually loads | Risk | Status |
| --- | --- | --- | --- | --- | --- | --- |
| A | CCW cal | 0 | `RA1;` | live `WA0` + value, restore `WA1` + value. Reset button sends `WA00`. | high | Unverified. Read-only |
| B | CW cal | 950 | `RB1;` | `WB0` / `WB1`. Reset sends `WB0950`. | high | Unverified. Read-only |
| C | Reserved | | `RC1;` | none | | Excluded |
| D | Offset | 180 | `RD1;` | `WD0` + value. North button is the literal `WD0180`. South is `WD00`. Restore uses `WD1`. | medium | Unverified. Read-only |
| E | Delays (brake and reversal are this one field) | 3 | `RE1;` | `WE0` / `WE1` | medium | Unverified. Read-only |
| F | Min speed | 3 | `RF1;` | `WF0` / `WF1` | medium | Unverified. Read-only |
| G | Max speed | 10 | `RG1;` | `WG0` / `WG1`. Orion auto-cal uses the literals `WG04` and `WG010`. | medium | Unverified. Read-only |
| H | CCW limit | 180 | `RH1;` | `WH0` / `WH1` | medium | Unverified. Read-only |
| I | CW limit | 180 | `RI1;` | `WI0` / `WI1` | medium | Unverified. Read-only |
| J | Rotor type | Pot | `RJ1;` | `WJ0` + code. See the table under this one. | high | Unverified. Read-only |
| K | Pulse divider | 3960 | `RK1;` | `WK0` / `WK1`. The utility rejects a value at or above 43600. | high | Unverified. Read-only |
| L | Cal range | 360 | `RL1;` | `WL0` / `WL1` | medium | Unverified. Read-only |
| M | Mode | Normal | `RM1;` | `WM00` … `WM05`. See below. | high | Unverified. Read-only |
| N | Ramp | 3 | `RN1;` | `WN0` / `WN1` | medium | Unverified. Read-only |
| O | Brightness | 8 | `RO;` (the digit is ignored) | `WO0<n>;`. Brightness 10 is `WO010;`. Ack `06 4F 3B`, then 150 ms, then the read. Digit 0 reloads; it does not reboot on 4.13.2. OLED shows no change. | the only write | Write button only, and only when the read says 4.13.2. |
| P | No-motion timeout | 4 | `RP1;` | `WP0` / `WP1` | low | Unverified. Read-only |
| Q | Security | Admin | `RQ1;` | `WQ00` user, `WQ01` admin | high | Unverified. Read-only |
| R | Alt offset 1 | 361 | `RR1;` | `WR0` / `WR1`. Unused is the literal `WR0361`. | medium | Unverified. Read-only |
| S | Alt offset 2 | 361 | `RS1;` | `WS0` / `WS1`, `WS0361` | medium | Unverified. Read-only |
| T | Advanced options | 0 | `RT1;` | `WT0` or `WT1` plus the bitfield. Bits below. | medium | Unverified. Read-only |
| U | Overtravel | 90 | `RU1;` | `WU0` / `WU1`. The utility rejects a value above 180. | medium | Unverified. Read-only |
| V | Counter-rotate delay | 10 | `RV1;` | `WV0` / `WV1` | low | Unverified. Read-only |
| W | Screen saver, minutes. Off, 1, 2, 5, 10, 20, 30, 40, 50, 60. The 2014 manual calls W reserved. 4.13 plus display firmware 1.3 blanks the panel. | Off | `RW1;` | Combo writes `WW00` for Off and `WW0` plus the minutes (`n = 0`, which reboots). 4.11 used `WW0` plus two spaces, which is the bug 4.B12 says it fixed. On 4.14.g1, On stops serial until a power cycle. Meridian recommends Off and does not offer turning it on. | unsafe when on | Read-only. See the protocol note. |
| X | Limit switch position. The 2014 manual says read-only. The utility writes it. | | `RX1;` | `WX0` + value, must be 45 or below | medium | Unverified. Read-only |
| Y | Knob time | 40 | `RY1;` | `WY0` / `RY1` | low | Unverified. Read-only |
| Z | Multiplier. The 2014 manual says Z is unused. | | `RZ1;` | `WZ0` / `WZ1` | medium | Unverified. Read-only |
| `[` | Magnetic variation, degrees, −60 to +60. Added in utility 4.B12 for firmware 4.d14. | | `R[1;` | `W[0` + value | medium | Unverified. Read-only |

Rotor type codes, from the click handler of each radio button in 4.B12 (`rdoOptionPot_MouseClick` loads 0, and so on):

| Code | Utility label | `.ghr` name |
| --- | --- | --- |
| 0 | Pot | pot |
| 1 | Counter | counter |
| 2 | Ham-x | ham |
| 3 | T2X | t2x |
| 4 | TIC-PST | ticpst |
| 5 | Orion | orion |
| 6 | Spid | spid |
| 7 | Yaesu G-800/1000 | Yaesu |
| 8 | Yaesu G-5400 | YaesuAC360 |
| 9 | Yaesu G-5500 | YaesuAC450 |
| 10 | Yaesu G-2800 | (no name in the restore strings) |
| 11 | DX Engineering | (no name in the restore strings) |
| 12 | Sub-Lunar | SubLunar |

Mode codes, from `rdoNormal_Click` and the matching handlers: 0 Normal `WM00`, 1 Master/slave `WM01`, 2 Master counter-rotate `WM02`, 3 Slave counter-rotate `WM03`, 4 Offset `WM04`, 5 Debug `WM05`. Debug is only offered after an Az/El version check.

Security: 0 User `WQ00`, 1 Admin `WQ01`.

`T` bits, Appendix F: 1 freeze display, 2 fast brake release, 4 auto go-back, 8 elevation, 16 auto dim, 32 analog heading out, 64 EME, 128 point-and-shoot, 256 port is 9600, 512 2-bit encoder, 1024 demo, 2048 allow cal to the CCW limit. The utility writes the whole field with `WT0` on each checkbox, including `cboPort9600` and `cboSerialSensor`. The bit number for "Use Serial Sensor" (4.B12, firmware 4.14) was not separated from the other bits in the IL. It is not shown as its own checkbox here.

Other controller actions:

| Action | Command | Risk | Status |
| --- | --- | --- | --- |
| Version | `R11;` | low | Verified as a command we send. The reply text is Unverified on Andrew's unit. |
| Pot A/D | `R01;` | low | Unverified reply |
| Last abnormal reset | `R41;` then the controller clears it | low | Unverified reply |
| Heading, tenths | `BI1;` reply `NNN.N;` | low | Verified, 14 Nov 2026 |
| Status prefix | `01 30 xx 3E` then the heading. `xx` 0 stopped, 1 moving | low | Verified on that session. Not requested by `R21;`. |
| Calibrate CCW here | `C01;` | high | Unverified. Not sent |
| Calibrate CW here | `C11;` | high | Unverified. Not sent |
| Set pulse heading | `AW1` + `xxx.y` | high | Unverified. Not sent |
| Point and shoot | `AP1` + heading + CR + `;` | low | Sent by the controller page, not by setup |
| Vanity boot line | `W88` then text, max 14 characters, and `W00` appears in the same method | high | Excluded |
| Factory reset | `W00;` | high | Excluded |
| Backup to a file | no bytes on the wire | low | Done. JSON from the controller page, and `backups/meridian-setup-*.txt` for the whole read |
| Orion auto-cal | a script of `WG04`, `AW1`, `AP1`, `WG010` | high | Not implemented |

The PC baud box (`cboBaud`, 4800 or 9600) has no rotor command in its handler. It only changes the port this computer opens. The rotor's own speed is the 9600 bit above.

## Ports

One byte stream, three sockets:

| Port | What it is | Baud / framing | Status |
| --- | --- | --- | --- |
| RS-232 | The DB-9. DCU-1 superset, 8N1. | 4800 unless the 9600 bit is set | Baud default is the manual. Unverified as a read-back of the bit. |
| USB | FTDI on older units. CP210x from November 2022 (the port enumerates even when the rotor is off). Same bytes as RS-232. | Same baud bit | Driver note is the addendum. |
| Serial TCP | The module register `NVREG16_0`. Programs such as GH Everyware and PST Rotator. | `AT*Serial_Start=0x01` and `AT*Serial_End=0x3B` on the utility's reset. The 14 Nov session showed frames that start with `0x01` and end with `;`. | The start/end bytes are Unverified as the cause of that framing. The port number is Unverified on a live module. The utility's reset writes `6555`. |

## Network module

These are not EEPROM letters. They are AT commands to the add-in module, reached only after `T11;` and `AT;`.

There is no DNS command in the manual or in the utility. There is no AP/client mode command. The only related write is `AT*WiFi_Def_Adhoc=`, and it appears only inside "RESET to DEFAULTS", which Meridian does not send. The 2019 manual's "UDP and Serial are for future use" is out of date: 4.B12 reads and writes both, and the addendum describes the serial TCP port.

| What you see | Read | Reply token | Write | Risk | Status |
| --- | --- | --- | --- | --- | --- |
| Module type | `AT*HWTYPE?` | `HWTYPE=` | none | low | Unverified. The utility accepts only `HWTYPE=2`. |
| DHCP or static | `AT*DHCP?` | `DHCP=` | `AT*DHCP=0` when the DHCP radio is selected. `AT*DHCP=1` when Static IP is selected. The reset button sends `AT*DHCP=1` together with a static address. | high, drops an IP link | Unverified. The polarity is the opposite of the usual "1 means DHCP". |
| Current IP | `AT*IP?` | `IP=` | none (it follows DHCP) | low | Unverified |
| Static IP | `AT*AIP?` | `AIP=` | `AT*AIP=` + address. The reset writes `192.168.2.100`. | high, drops the link | Unverified |
| Current mask | `AT*NMASK?` | `NMASK=` | none | low | Unverified |
| Static mask | `AT*ANMASK?` | `ANMASK=` | `AT*ANMASK=` + mask. The form's placeholder is `255.255.255.0`. | high | Unverified |
| Current gateway | `AT*GW?` | `GW=` | none | low | Unverified |
| Static gateway | `AT*AGW?` | `AGW=` | `AT*AGW=` + gateway. The reset writes `192.168.2.1`. | high | Unverified |
| DNS | none | | none | | Not in the utility or the addendum |
| Rotator name | `AT*ID?` | `ID=` | `AT*ID=` + name. The reset writes `RT-21`. | low | Unverified |
| HTTP port | `AT*HTTP?` | `HTTP=` | `AT*HTTP=` + port. Port 80 means the browser URL has no port. | medium | Unverified |
| UDP port | `AT*UDP?` | `UDP=` | `AT*UDP=` + port | medium | Unverified |
| TFTP port | `AT*TFTP?` | `TFTP=` | The read is real. The write handler `txtPortTFTP_Validated` sends `AT*HTTP=` again. That is a bug in 4.B12. | medium | Read Unverified. Write excluded so we do not copy the bug. |
| Serial TCP port | `AT*NVREG16_0?` | `NVREG16_0=` | `AT*NVREG16_0=` + port. The reset writes `6555`. | high, drops the link | Unverified. 6555 is the utility's reset value, not a number printed in the addendum. |
| Wi-Fi security | `AT*WIFI_SECURITY?` | `SECURITY=` | `6` WPA2-PSK, `4` WPA-PSK, `8` WPA Auto, `0` None. Same order as the combo box and `SetSecurityType`. | high, can fail to join | Unverified. The index-to-code mapping is the order of the four strings in that method. |
| SSID | `AT*WIFI_ASSID?` | `ASSID=` | `AT*WIFI_ASSID=` + name. The reset writes `NETGEAR24`. | high | Unverified |
| Passphrase | `AT*WIFI_KEY?` | `KEY=` | `AT*WIFI_KEY=` + phrase. The utility requires 8 characters. The reset writes a known phrase. Not logged. | high | Unverified |
| AP or client | none | | `AT*WiFi_Def_Adhoc=` only inside the excluded reset | high | Not a user setting |
| Web user | `AT*HTTP_USER?` | `USER=` | not sent by Meridian. The 2019 manual says login was not implemented yet. 4.B12 still has the fields. | medium | Unverified |
| Web password | `AT*HTTP_PASS?` | `PASS=` | not sent. Not logged. | medium | Unverified |
| Link | `AT*LINK?` | `LINK=1` is "Link OK" | none | low | Unverified |
| Serial framing | not polled | | `AT*Serial_Start=0x01`, `AT*Serial_End=0x3B` on reset only | high | Unverified |
| Module reboot | | | `AT*RESET=1` | high, drops the link | Excluded |
| Named presets on the module | not polled | | `AT*NVREG16_12` through `AT*NVREG16_32` step 2, and `AT*NVSTRING100` then every 13 bytes through `AT*NVSTRING243` | medium | Unverified. Not shown. The web page's presets, not the rotor EEPROM. |
| Beamwidth / region | not polled | | Reset writes `AT*NVREG16_10=36`, `AT*NVREG8_255=42`, and `AT*NVSTRING100=Europe`, which overwrites the first preset name. | medium | Unverified. Not shown. |

## Where Meridian stands

| Area | Now |
| --- | --- |
| Find RT-21, USB first, then a short port list | Done |
| Controller read, including magnetic variation | Done, read-only |
| Ports page: controller speed, this computer's saved connection, local serial ports, module TCP port | Done, read-only |
| Network page | Done, read-only, over serial, USB, or IP |
| Guide: free TCP port, a static address outside .100–.199, drop-link warning, prefer USB | Done. Suggestions are not copied into the connection fields. |
| Plain-text copy in `backups/` | Done, and it runs before any future write |
| Write, read back, compare | Implemented behind the read-only switch. Not in this build. |
| Factory reset, bootloader, module reset, TFTP write, vanity line | Excluded |

## What Andrew's utility showed against the 4.14.g1 read

The setup utility, on the Edgeport COM port at 4800, showed Yaesu G-2800, Admin, Normal, limits 174/174, offset 174, max 10, min 1, ramp 3, motor timeout 8 s, rev/brake 3 s, A/D 0440, 3.58 V on term 4, CCW cal 5, CW cal 705, counter-rotate 10 s, over-travel 60, limit switch 1.4, cal range 360, magnetic variation 0, Auto Dim + Use Encoder + Allow Cal to the CCW limit, brightness 10, knob timer 40, screen saver 1 minute, and both alternate offsets off. Firmware line `RT-21 4.14.g1`.

| Utility | Register | Read earlier | Match |
| --- | --- | --- | --- |
| Yaesu G-2800 | J = 10 | 10 | yes |
| Admin | Q = 1 | 1 | yes |
| Normal | M = 0 | 0 | yes |
| Limits 174/174, offset 174 | H, I, D | 174 | yes |
| Max 10, ramp 3 | G, N | 10, 3 | yes |
| Min 1 | F | 125 | no. The slider is not the raw register. Meridian shows the raw value. |
| Motor timeout 8 s, brake 3 s | P, E | 8, 3 | yes |
| CCW 5, CW 705 | A, B | 5, 705 | yes |
| Counter-rotate 10, over-travel 60, cal range 360 | V, U, L | 10, 60, 360 | yes |
| Limit switch 1.4 | X = 14 | 14 | the utility shows tenths. Meridian shows 1.4 and the raw 14. |
| Auto dim, encoder, allow CCW cal | T = 16+512+2048 | 2576 | yes |
| Brightness 10, knob 40 | O, Y | 10, 40 | yes |
| Screen saver 1 min | W | not in that backup | the utility shows 1 minute |
| Alt offsets off | R, S = 361 | 361 | 361 is the unused sentinel. Meridian shows off. |
| 3.58 V at A/D 0440 | pot, not an EEPROM letter | pot was 0334 at another moment | not 440/1023×5 V. The utility derives it. |

A later serial open of the module, after one junk attempt, showed name RT-21, static address 192.168.1.106, mask 255.255.255.0, gateway 192.168.2.1, link 0, HTTP 801, UDP 23073, TFTP 69, serial TCP 6555. `AT*DHCP=1` is Static. The current IP shown beside the DHCP control is `AT*IP`, which was also 192.168.1.106. The gateway is on 192.168.2.0, not 192.168.1.0. Meridian says so and does not offer reset-to-defaults. The web password is reported only as set or not set.

`docs/RT21_PROTOCOL.md` is the motion-protocol note. This file is the setup map.
