# Review G — SETUP Utility 4.B12 parity

Independent review of Meridian at **alpha25h, commit `974df5112384d3d90574fd36a146e606b1350592`** (subject: “Keep a saved TCP port when Find connects over serial”). The tree reviewed is that commit. No application code was changed. Other `REVIEW-*.md` files were not read.

The stated goal is a 100% replacement for Green Heron Engineering’s RT-21 SETUP Utility v4, current public beta **4.B12** (December 2023). This review scores the azimuth RT-21 (and RT-21D) setup surface. The same utility also programs the RT-21 Az/El. That product is listed once, after the score, and is not in the denominator.

## Sources

Fetched 19 November 2026 from [greenheronengineering.com](https://www.greenheronengineering.com/):

| Document | URL | What it covers |
| --- | --- | --- |
| RT-21 User Manual 3.5 (manual text says 3.05), 8 August 2014 | https://www.greenheronengineering.com/wp-content/uploads/2019/08/RT-21_Manual_current.pdf | SETUP items, calibration, master/slave, vanity and security as utility-only, Appendix F protocol and EEPROM map, v3 loader |
| RT-21 revision history | https://www.greenheronengineering.com/wp-content/uploads/2019/08/RT-21_Revision_History.pdf | CPU families through 4.09 (September 2015–2016). The PDF stops at 4.09 |
| RT-21 Version 4 addendum, rev 2 (August 2019 file) | https://www.greenheronengineering.com/wp-content/uploads/2019/08/Version_4_Manual.pdf | First v4 feature list, firmware update, `B2n` / `BUn`, Wi-Fi window |
| RT-21 Version 4 addendum, rev 4 (November 2022) | https://www.greenheronengineering.com/wp-content/uploads/2022/11/Version-4-rev-4.pdf | CP210x USB, LAN and TCP, debug mode, screen saver, serial-TCP port, same bootloader commands |
| RT-21 Az-El addendum (November 2022) | https://www.greenheronengineering.com/wp-content/uploads/2022/11/RT-21-Az-El-Version-2-Manual.pdf | Az/El is programmed with the same SETUP Utility |
| Software page | https://www.greenheronengineering.com/rotator-controllers-software/ | Utility 4.11 (December 2022) and 4.B12 (December 2023); firmware 4.13.2 called current |
| Documentation index | https://www.greenheronengineering.com/rotator-controllers-documentation/ | Links above |
| RT-21 product page | https://www.greenheronengineering.com/product/rotator-controller-rt-21/ | Utility bullet list: vanity, range graphic, rotation range, security, no-motion timeout, auto-cal, save, restore, test computer control |

4.B12’s own release note, on the software page: supports RT-21 4.14 with serial-sensor feedback, adds magnetic variation for pre-release 4.d14, raises Az/El divide-ratio limits, and fixes a bug that always cleared the screen saver.

The zip URLs on that page (`RT-21-SETUP-Utility-4.B12.zip`, `RT-21-v4.13-Firmware.zip`) answered this environment with an HTML interstitial or 404, so the utility executable was not unpacked. Functions below are the ones named in those public documents, plus the commands Meridian’s own capture notes attribute to 4.B12 in `docs/RT21_PROTOCOL.md`. A button that exists only inside the binary and in none of those texts would be missing from this table.

The embedded web page’s Setup tab (presets, beamwidth, map images) is described in the v4 addendum as a browser page. It is not a SETUP Utility function and is not scored.

## How the score is counted

Forty-seven azimuth functions. **Done = 1, partial = 0.5, missing = 0.** A function is done when the shipped UI can read it and, where the utility can change it, write it on firmware 4.13.2 with an acknowledgement and a read-back. Partial means the value is shown, or only part of the control works. The default CMake option `MERIDIAN_ALPHA_READONLY` is ON (`CMakeLists.txt:9`, `:50`). That flag stubs `applySetupGroup`, `writeConfiguration`, `sendCalibrate`, and `sendPulseHeading`. It does not stub `writeRegister`. The Write buttons on the setup column call `writeRegister`, so those writes are in the default build.

**Parity: 28.5 / 47 = 61%.** Twenty done, seventeen partial, ten missing. The flat score counts a brightness write the same as calibration. The ranked list is the priority order.

`docs/RT21_PROTOCOL.md:129` still says the only write in this build is brightness and that every other write is compiled out. The setup column does not match that sentence: `writeRegister` accepts `D E G H I K L N O P R S U V W Y` (`src/rotor/Rt21Driver.cpp:3034`) and the column gives those letters Write buttons, except F, which is not in the set. The score follows the buttons.

## Parity table

| # | SETUP Utility function | Public basis | Meridian | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Connect on RS-232 or USB, 4800 or 9600 8N1, and show that the link is up (the utility’s green LED) | Manual 3.5 Appendix F; v4 rev 4 firmware-update steps | **Done.** Find probes COM ports; Settings has 4800/9600; the map dot is the link | `src/SettingsDialog.cpp:589`, `src/MainWindow.cpp:201`, `src/GlobeWidget.cpp:352` |
| 2 | Read model/version, pot A/D, and last abnormal reset | Appendix F `R1n` `R0n` `R4n`; v4 rev 4 `R4m` | **Done.** | `src/rotor/Rt21Driver.cpp:1727` |
| 3 | Read every setup register the utility shows | Appendix F `RXn` A–Z; 4.B12 adds screen saver, multiplier, magnetic variation | **Done.** A–Z and `[` are queried | `src/rotor/Rt21Driver.cpp:1747` |
| 4 | Rotor type (OPTION), including Yaesu AC360 and AC450 | Manual 3.5 §3.1; v4 addendum “New rotators” | **Partial.** Radios are disabled. Two name tables disagree at codes 8 and 9: the column says Yaesu AC360 / AC450, `rotorTypeName` says Yaesu G-5400 / G-5500 | `src/ControllerColumn.cpp:141`, `src/rotor/Rt21Setup.cpp:101` |
| 5 | Security lock (User / Admin). Front panel cannot change it | Manual 3.5 §5.8; product page | **Partial.** Shown, write disabled | `src/ControllerColumn.cpp:146`, `:114` |
| 6 | Mode: Normal, Master/Slave, Master/Counter-Rotate, Slave/Counter-Rotate, Offset, Debug | Manual 3.5 §3.2.1; v4 rev 4 debug mode | **Partial.** Shown, write disabled. Debug cannot be entered | `src/ControllerColumn.cpp:147` |
| 7 | Graphic of the rotation range | Product page | **Done.** Horizontal bar, CCW, CW, offset mark | `src/ControllerColumn.cpp:30`, `:164` |
| 8 | CCW and CW soft limits | Manual 3.5 §3.2.1; product page “change the rotation range” | **Done** on 4.13.2. `WH0` / `WI0`, ack, read-back. No confirm. See safety | `src/ControllerColumn.cpp:191`, `src/rotor/Rt21Driver.cpp:3029` |
| 9 | Offset (north-center 180, south-center 0). Changing it repositions soft limits inside the controller | Manual 3.5 §1.2 and §3.2.1 | **Done** on 4.13.2. Same write path. No confirm | `src/ControllerColumn.cpp:193` |
| 10 | Minimum speed, scale 1–10 | Manual 3.5 §3.2.1, EEPROM F default 3 | **Partial.** Read only. The column records that the utility showed 1 while register F read 125, so the raw register is displayed and not written | `src/ControllerColumn.cpp:205`, `:241`, `:478` |
| 11 | Maximum speed 1–10, plus speed 11 (full speed, no ramp / AC motor PWM) | Manual 3.5 §3.2.1; revision history 1.06 | **Partial.** Writes 0–10. Speed 11 is not offered | `src/ControllerColumn.cpp:211`, `:240` |
| 12 | Ramp 0–9 (at speed 11, early stop is ramp × 2 degrees) | Manual 3.5 §3.2.1 | **Done** as a write. The spin also allows 10, which the manual does not | `src/ControllerColumn.cpp:211`, `:242` |
| 13 | Delays: reversal and brake, one setting, 1 second steps, maximum 6 | Manual 3.5 §3.2.1, EEPROM E | **Done** as one register. The spin allows 0–99. Fast brake release is a separate advanced bit (row 22) | `src/ControllerColumn.cpp:212`, `:244` |
| 14 | No-motion / rotor-fail timeout | Manual 3.5 Appendix E; product page; EEPROM P default 4 | **Done.** | `src/ControllerColumn.cpp:243` |
| 15 | Live pot A/D and the stored CCW/CW calibration constants | Appendix F `R0n`, EEPROM A and B | **Done** as a read. The constants are labels | `src/ControllerColumn.cpp:251` |
| 16 | Calibrate at the antenna: CCW cal, CW cal, quick cal, auto-cal | Manual 3.5 §4; product page; Appendix F `C0n` `C1n`; revision history 1.04 | **Missing.** The column says Set, Quick Cal, and Reset Cal are off. `sendCalibrate` is compiled out of the default build | `src/ControllerColumn.cpp:260`, `src/rotor/Rt21Driver.cpp:3451`, `CMakeLists.txt:9` |
| 17 | Cal range (removed from the front panel in 3.08, still in the utility) | Manual 3.5 §3.2.1; revision history 3.08 | **Done.** Register L is writable | `src/ControllerColumn.cpp:296` |
| 18 | Pulse divide ratio, including values above 9999 (front panel cannot) and above 50,000 on later firmware | Manual 3.5 §3.2.1; revision history 1.06 and 3.08 | **Partial.** One spin, maximum 20,000. Counter rotors only (enabled when OPTION reads as 1) | `src/ControllerColumn.cpp:215`, `:266`, `:436` |
| 19 | Set the pulse-counter heading | Appendix F `AWnxxx.y` | **Missing.** `sendPulseHeading` is compiled out and has no button | `src/rotor/Rt21Driver.cpp:3467` |
| 20 | Over-travel allowance | Revision history 1.07, EEPROM U, default 90 | **Done.** | `src/ControllerColumn.cpp:293` |
| 21 | Limit-switch position | Appendix F EEPROM X | **Partial.** Shown as degrees plus the raw value. No write | `src/ControllerColumn.cpp:294`, `:471` |
| 22 | Advanced options: freeze display, fast brake, auto go-back, elevation, auto dim, analog out, EME, point-and-shoot shortest route, port 9600, encoder, demo, allow cal to the CCW limit | Manual 3.5 §5.10; Appendix F bit list | **Partial.** Twelve checkboxes, all disabled. “Use serial sensor” is a thirteenth checkbox that is never filled from a bit | `src/ControllerColumn.cpp:273` |
| 23 | Counter-rotate delay | Manual 3.5 §5.10.12; EEPROM V | **Done** as a number. The mode that uses it is row 6 | `src/ControllerColumn.cpp:292` |
| 24 | Two alternate offsets, 361 means unused | Manual 3.5 §5.9; EEPROM R and S | **Done** as values. Offset mode itself is row 6 | `src/ControllerColumn.cpp:361` |
| 25 | Knob time (0 disables point-and-shoot) | Manual 3.5 §3.2.1; revision history 3.08.7 and 4.07 | **Done.** | `src/ControllerColumn.cpp:342` |
| 26 | Display brightness (LCD 1–10, VFD 1–4; OLED has none) | Manual 3.5 §3.2.1; v4 rev 4 | **Done** on 4.13.2, with read-back. Spin is 0–15. The UI says an OLED does not change | `src/ControllerColumn.cpp:306`, `src/rotor/Rt21Driver.cpp:3024` |
| 27 | Screen saver: Off, 1, 2, 5, 10, 20, 30, 40, 50, 60 minutes. Needs display firmware 1.3. 4.B12 fixed a utility bug that cleared it | v4 rev 4; software page | **Partial.** The combo matches those steps and the Write button calls `writeRegister('W')` on 4.13.2. 4.14 is refused. A string in `screenSaverRecommendation` still says the register is not written | `src/ControllerColumn.cpp:343`, `src/rotor/Rt21Setup.cpp:547`, `:578`, `src/rotor/Rt21Driver.cpp:3043` |
| 28 | Vanity boot message | Manual 3.5 §5.8; Appendix F `W0n(callsign)`; product page; v4 addendum (front-panel RESET EE clears it on a 1.2 display) | **Missing.** No field. The protocol notes say the command is not sent | `docs/RT21_PROTOCOL.md:113` |
| 29 | Factory RESET EE. Resets every setting except OPTION, and recenters a counter rotor. Front-panel chord is CANCEL+CCW+CW | Manual 3.5 §1 and §4.1; v4 addendum; Appendix F `W00` | **Missing.** `planGroup("factory")` returns “That command is not available.” There is no button. That is the safe absence; see safety | `src/rotor/Rt21Setup.cpp:296` |
| 30 | Save rotor settings to a file | Product page | **Done.** “Save backup” / “Save a copy” writes a timestamped `.txt` and `.json` of the registers read and the network fields held in memory | `src/SettingsDialog.cpp:408`, `src/rotor/Rt21Setup.cpp:302` |
| 31 | Restore rotor settings from a file | Product page | **Missing.** No import. The backup text says nothing was written back | `src/rotor/Rt21Setup.cpp:308` |
| 32 | Open the network module from RS-232 or USB (green LED, then Web-Server Configure). `AT;` enters, ESC leaves. `T1n` allows the pass-through | v4 rev 4 “WiFi/LAN Operation” and the `AT;` / `T0n` / `T1n` commands | **Done** as a serial/USB read, with retries. An IP-only link is refused, which matches the addendum (configure from RS-232 or USB) | `src/rotor/Rt21Setup.cpp:78`, `src/SetupPanel.cpp:274`, `:372` |
| 33 | Rotator name ID | v4 rev 4 | **Partial.** Read (`AT*ID?`). No write button. A planner exists and is not wired | `src/SetupPanel.cpp:154`, `src/rotor/Rt21Setup.cpp:290` |
| 34 | DHCP or static IP, subnet, gateway | v4 rev 4 | **Partial.** All four are read. Only the gateway has a Write button, and only after a successful serial read in this session | `src/SetupPanel.cpp:187`, `src/rotor/Rt21Driver.cpp:3138` |
| 35 | Gateway write checked against the module | Captured 4.B12 behavior recorded in `docs/RT21_PROTOCOL.md`; live path compares `AT*AGW?` and `AT*GW?` | **Done** for this one field, on 4.13.2, serial or USB | `src/rotor/Rt21Driver.cpp:3129`, `:3178` |
| 36 | Wi-Fi security type, SSID, and key. The type must match the access point. Passphrase at least 8 characters | v4 rev 4 “WiFi Security (WiFi Only)” | **Missing.** The fields are on the form and stay at “Not read.” The read sequence never sends `AT*WIFI_SECURITY?`, `AT*WIFI_ASSID?`, or `AT*WIFI_KEY?` | `src/SetupPanel.cpp:167`, `src/rotor/Rt21Setup.cpp:78` |
| 37 | HTTP port, UDP port (factory use), and the serial-TCP port | v4 rev 4 “Ports”; serial-TCP is 4.13 and later | **Partial.** All three are read (`AT*HTTP?`, `AT*UDP?`, `AT*NVREG16_0?`). None are written. The utility’s module-reset path writes `AT*NVREG16_0=6555`; Meridian only records that | `src/rotor/Rt21Setup.cpp:91`, `src/SetupPanel.cpp:214` |
| 38 | Reset the network module / reset to defaults, then restart so the new address is used | v4 rev 4 “Close and restart”; captured utility `AT*RESET` | **Missing.** The form says reset to defaults is not offered | `src/SetupPanel.cpp:214`, `src/rotor/Rt21Setup.cpp:296` |
| 39 | Firmware update for v4: Update Firmware, choose `RT-21 4.xx.hex`, Load Firmware, display flashes BOOT | v4 rev 2 and rev 4 | **Missing.** See the firmware section | `src/rotor/Rt21Setup.cpp:296` |
| 40 | Test computer control: send a heading and stop | Product page; Appendix F `APn` with CR, and `;` | **Done.** Map click and the heading editor send `AP1xxx.y\r;`. Esc and Space send `;` | `src/GlobeWidget.cpp:890`, `src/MainWindow.cpp:40`, `src/rotor/Rt21Driver.cpp:691`, `:1655` |
| 41 | Jog the motor from the utility (CW/CCW buttons; protocol `AAn` / `ABn`, 1.5 s, retriggerable, soft limits apply) | Appendix F; the setup column’s own note that those buttons stay off | **Missing.** | `src/ControllerColumn.cpp:156` |
| 42 | Show in-motion, no-motion, pot-out-of-range, and counter-range as they happen | v4 addendum; `R2n` status 0, 1, 2, 4, 5 | **Partial.** Status 0 and 1 drive the moving dot. Status 2, 4, and 5 are parsed and then ignored | `src/rotor/Rt21Driver.cpp:1523` |
| 43 | Multi-unit: master/slave and counter-rotate cabling, one master, `AVn` between controllers | Manual 3.5 §5.2–5.4 | **Partial.** Several saved rotors can be picked on the map, one active. That is a connection list. Master/slave mode cannot be set, and `AVn` is not sent | `src/MainWindow.cpp:53`, `src/GlobeWidget.cpp:682` |
| 44 | Magnetic variation, for pre-release 4.d14 | Software page, 4.B12 | **Partial.** `R[1;` is sent. A missing reply is tolerated. The value is shown and not written. 4.13.2 does not implement the register | `src/rotor/Rt21Driver.cpp:1755`, `src/ControllerColumn.cpp:295` |
| 45 | Serial-sensor feedback (4.14) | Software page, 4.B12 | **Missing.** The checkbox is disabled and is not tied to a bit. The tooltip says the bit was not separate in the program | `src/ControllerColumn.cpp:286` |
| 46 | Multiplier (utility 4.B12; the 2014 manual called Z unused) | `docs/RT21_PROTOCOL.md` screen-saver section, from the utility | **Partial.** `RZ1;` is read and the backup names it. The setup column has no row | `src/rotor/Rt21Driver.cpp:1747`, `src/rotor/Rt21Setup.cpp:652` |
| 47 | Debug view: pot systems show live A/D beside the stored cal; counter systems show the live pulse count. Point-and-shoot is disabled in this mode | v4 rev 4 “RT-21 DEBUG Mode” | **Partial.** Live A/D and the stored cal constants are on the form. Debug mode cannot be selected, and there is no live counter view | `src/ControllerColumn.cpp:251`, row 6 |

Rows 16, 19, 28, 29, 31, 36, 38, 39, 41, and 45 are the ten missing items.

## Ranked gaps

### Blocker

1. **Calibration cannot be performed.** Endpoint cal (`C0n` / `C1n`), quick cal, auto-cal, and setting a pulse-counter heading are all absent, and the default build compiles the first and the last out. A new rotor, a mast slip, or an Orion still has to be calibrated from the front panel or from SETUP Utility. The v4 addendum warns that calibrating a Prosistel, AlfaSpid, or TIC Ring remotely can over-rotate a rotator that has no limit switches. Meridian does not do the job, so it also does not provide a guarded way to do it.

2. **No v4 firmware update.** From SETUP Utility 4.0 onward this is the only update path the v4 addendum documents. The v3 loader (`tinybldWin.exe`, 57600 baud, power-cycle timing) is for the PIC18F2520 units, serial numbers 2000–3999. Shipping a replacement that cannot load `RT-21 4.xx.hex` leaves the owner on the Green Heron utility for the one operation that can blank the controller.

### Major

3. **The identity of the controller cannot be changed.** Rotor type, security, and mode (including debug, offset, and master/slave) are read-only, and the two rotor-type name tables disagree. OPTION is the setting a factory reset deliberately keeps. Writing the wrong type is how a station gets the wrong pulse divider and the wrong limits. Leaving it read-only is safer than a guess, and it is still a hole in a setup utility.

4. **Minimum speed is unsolved, and maximum speed has no speed 11.** Register F does not match the utility’s 0–10 slider on the station Meridian was built against (raw 125 versus a displayed 1). Speed 11 is the documented “no ramp / AC motor” setting. Both are everyday motor setup.

5. **Advanced options are display-only.** That one disabled group is freeze, fast brake, auto go-back, elevation, auto-dim, analog heading output, EME, point-and-shoot shortest route, the rotor’s 9600 bit, the quadrature encoder, demo mode, and “allow cal to the CCW limit.” The PC baud combo changes Meridian’s port. It does not set the controller’s 9600 bit.

6. **Network configuration is a reader plus one gateway write.** Name, DHCP, static address, mask, HTTP, UDP, and the serial-TCP port are read and not written. Wi-Fi security, SSID, and key are not even queried. There is no module reset, which is how the utility applies a new address and how it restores serial-TCP port 6555. The gateway write itself is the right shape: serial or USB only, after a read, `AT*AGW=` plus LF, then `AT*AGW?` and `AT*GW?`.

7. **A backup cannot be restored.** The file is an honest snapshot, and the text says so. Rotor type, security, minimum speed, the advanced bits, and the network block could not be restored field-by-field either, because those writes do not exist.

8. **Vanity message is absent.** The manual says this is a utility-only setting. The product page lists it first.

9. **4.B12’s new RT-21 settings are unfinished.** Magnetic variation is read and locked. Serial-sensor feedback has a dead checkbox. Screen saver can be written on 4.13.2 and is refused on 4.14, which is the firmware 4.B12 was published for. The in-app recommendation string still says Meridian does not write the screen-saver register (`src/rotor/Rt21Setup.cpp:578`).

10. **Fault status is swallowed.** `R2n` status 2 (no motion), 4 (pot out of range), and 5 (counter range) do not change the status line. The v4 addendum added those messages so a remote operator can see a jammed rotor or a failed pot. Meridian keeps polling as if the only facts were “idle” and “moving.”

11. **Divide ratio is capped at 20,000, and the counter heading cannot be set.** The manual’s reason for using the utility is values the front panel cannot enter (above 9,999, and later above 50,000). High-count and EME systems fall outside the spin box. Row 19 is the other half of counter calibration.

### Minor

12. **Ramp accepts 10, delay accepts 99, brightness accepts 0–15.** The manual’s ranges are 0–9, 1–6 seconds, and 1–10 (1–4 on a VFD).

13. **Multiplier is in the backup and not on the form.**

14. **No CW/CCW jog.** Goto and stop cover the product page’s “test computer control.” The 1.5-second jog commands, which the manual says obey soft limits, are not sent. The setup column says those buttons stay off on purpose.

15. **Terminal voltage is not computed.** The calibration note says the utility shows a voltage that is not a 5 V full-scale conversion, and Meridian does not attempt it (`src/ControllerColumn.cpp:260`).

16. **RT-21 Az/El is out of scope of this score and absent.** One chassis, two CPUs, elevation limits (including above 180° on later Az/El firmware), elevation OPTION values, inclinometers, and the higher divide ratios 4.B12 raised. The Az/El addendum says the SETUP Utility is the program for that controller.

### Not scored

Browser presets and beamwidth live on the module’s web page, not in SETUP Utility. DNS and an AP-or-client switch are labeled on Meridian’s form as “Not in the setup utility,” which matches the addendum. USB driver packages (FTDI for older units, CP210x from November 2022) are separate downloads. Display-module firmware is not field-upgradable; the addendum says so.

Meridian also does work the utility does not: the azimuth map, sun and day/night, Find over COM then TCP 6555 and 6665, and a traffic log. Those are outside this parity score.

## Hardware safety

The live register write is the careful one. `writeRegister` sends `W<letter>0<value>;`, waits for `06 <letter> ;`, waits 150 ms, reads `R<letter>;`, and then watches heading polls for 10 seconds. “Saved …” requires the read-back to match and at least one heading reply. Anything else is “Write not confirmed” (`src/rotor/Rt21Driver.cpp:1292`, `:1400`, `:3325`). Digit 0 is the form that stores and reloads the setup. That reload is why a single Write is not a single-byte poke: the controller re-reads its settings to apply the change.

What is still unsafe, or one step from it:

- **Soft limits, offset, divide ratio, over-travel, and cal range commit on one click.** There is no dialog. `registerWriteRequested` checks that some earlier read contained `4.13.2`, then calls `writeRegister` (`src/SettingsDialog.cpp:183`). The manual says an offset change moves the soft limits by itself. On a Prosistel, AlfaSpid, or TIC Ring, limits outside the loop are the damage case the v4 addendum describes. Read-back proves the controller stored the number. It does not prove the number is inside the antenna’s travel.

- **The driver’s firmware gate treats an empty version as allowed.** The refusal is: contains `4.14`, or non-empty and does not contain `4.13.2` (`src/rotor/Rt21Driver.cpp:3047`). An empty `firmwareReport` falls through and the bytes are sent. The dialog refuses an empty `versionReport`, and it does not clear `versionReport` when `setDriver` runs (`src/SettingsDialog.cpp:241`). A Write after the driver object has been replaced can be authorized by the previous session’s version string while the new driver has not yet stored one.

- **Screen saver is a real write.** On 4.13.2 the project’s own testing says the panel blanks and serial keeps answering. On 4.14.g1 the same setting stops serial until a power cycle. The 4.14 lock is the right call. The button has no confirmation, and the empty-version hole above applies to it. `W` with digit 0 also reloads every other setting.

- **Factory reset is not a missing-confirmation bug, because it cannot be sent.** `W00` and the `factory` plan are refused (`src/rotor/Rt21Setup.cpp:296`). When it is added, it needs a confirmation that states the consequence: every setting except OPTION returns to that rotor type’s defaults, a counter rotor is recentered, and a front-panel reset also clears the vanity message on a 1.2 display. A single OK is a weak confirmation for that.

- **Two write paths do not read back, and they are compiled out.** With `MERIDIAN_ALPHA_READONLY` ON, `writeConfiguration` never runs. If that option is turned off, it queues `W<letter>1<value>;` (store only, digit 1) and does not read it back (`src/rotor/Rt21Driver.cpp:3423`, `src/rotor/Rt21Protocol.cpp:101`). `applySetupGroup` is also stubbed, and nothing in the UI calls it. Its unused network planner ends AT commands with CR (`src/rotor/Rt21Setup.cpp:245`). The live gateway write ends them with LF (`src/rotor/Rt21Driver.cpp:3178`). Turning the CMake option off would not by itself make those planners safe.

- **Calibration and the bootloader stay off the wire in the default build.** That is the right default. `B2n`, `BUn`, and `R3n` are not on a button.

- **The gateway write is guarded.** IP is refused, a prior successful read is required, the address is checked as IPv4, and a mismatch is “Write not confirmed.” The other network fields cannot be written, which avoids dropping a link by accident and also leaves the module half-configured. A wrong gateway on a serial session still takes remote access down after the next module restart. There is no confirm.

- **The setup copy can contain the Wi-Fi key in the clear** if a key is ever stored in `rawNetwork`. The web password is reduced to “set” or “not set” before it is kept (`src/SetupPanel.cpp:492`). The backup header says the password is included so a restore can use it (`src/rotor/Rt21Setup.cpp:309`). The password is not included, and there is no restore. Today the Wi-Fi queries are never sent, so the key field is empty.

- **No-motion and pot-range faults are not announced** (row 42). An operator can keep sending headings into a stopped motor.

## What a firmware update feature needs

This is a design section. The bootloader protocol after the entry command is not in the public manuals, and it was not recovered from the utility binary.

### What Green Heron has published

- v4 controllers, serial numbers 4000 and up, use a **PIC18F26K22** with a second UART for the network module. v3 is a PIC18F2520 with an FTDI USB chip. v1 is a PIC18F2550. The revision-history PDF states those three CPUs on its first page. A v4 hex must not be offered for a v3 board, and the reverse is the same mistake.

- SETUP Utility 4.0 replaced the separate loader for v4. The published steps, identical in the rev 2 and rev 4 addenda, are: connect on the selected COM port until the utility’s LED is green; click **Update Firmware**; choose a file named like `RT-21 4.xx.hex`; click **Load Firmware**. The controller flashes **BOOT**, the display goes blank, the utility shows progress, and the controller restarts itself at the end.

- The entry commands are in the v4 addendum’s protocol list, and only there:
  - **`B2n;`** places the RT-21 into bootloader mode on **RS-232**.
  - **`BUn;`** places it into bootloader mode on **USB**.
  - `n` is the same 0–9 digit used on the other commands. The addendum does not say which digit the utility sends.

- The v3 procedure is a different program (`tinybldWin.exe`), **57600 baud**, and a power cycle during “Write Flash” (Manual 3.5 §5.7). v3 says a successful load leaves SETUP values in place. The v4 addendum does not repeat that promise. A v4 feature has to back up first and read the setup back after the restart.

- Display-module firmware is not updated by this path. Rev 4 says it is not field-upgradable. Screen saver needs display 1.3; brightness behavior depends on LCD, VFD, or OLED. The CPU load does not change that module.

- The current public firmware page calls **4.13.2** current and says the zip also contains the K7NV prop-pitch and K0XG images. Those are separate hex files for separate hardware. Az/El Version 2 has its own firmware line (2.013 / 2.a014 on the software page) and its own “do not install in V1” warning.

- `R3n;` is a CPU and display reset, not the bootloader. 4.02 removed it and 4.03 put it back. It is the wrong command for an update.

- USB enumeration differs by chip. FTDI (through late 2022) appears when the controller is on. CP210x (from November 2022) enumerates while the controller is off. The bootloader still needs the virtual COM port the utility already uses. The addendum’s picture is a COM port, not a raw USB device.

### What is not published

The bytes after `B2n;` or `BUn;` are not documented: baud, Intel-HEX framing, checksum, erase, verify, the timeout while the display is blank, or how to leave the bootloader if the load stops halfway. Microchip’s reference bootloaders for this PIC family are not evidence that Green Heron used one of them. Guessing that protocol in the client is how a controller gets stuck in BOOT with no public recovery except another copy of SETUP Utility.

`T0n;` and `T1n;` are network-module mode switches. They are not bootloader commands. An update over the serial-TCP port is not described anywhere, and `T1n` is what the addendum says is required before the utility can talk AT commands. The published update path is RS-232 or USB only.

### A feature that matches the published procedure

1. **Serial or USB only.** Refuse TCP. Require a live `R11;` on that COM port before the button enables, the same precondition as the green LED.

2. **Name the running firmware and the file.** Show the `R11;` string beside the hex file’s version. Refuse a file whose header is not an RT-21 v4 image for this CPU. Keep K7NV, K0XG, Az/El, and v3 files out of the same picker, or require a second confirmation that states the hardware they are for.

3. **Back up first.** Write the same snapshot as “Save backup,” and refuse to continue if that write fails. The v4 addendum does not say EEPROM survives.

4. **Confirm in words.** The dialog should say the display will show BOOT and then go blank, the link will drop, and a failed load can leave the unit in the bootloader until a good load finishes. Do not put `B2n;` or `BUn;` on a menu that a mis-click can fire. Meridian already keeps those commands out of `planGroup`.

5. **Pick the entry command from the port.** `B2n;` on the DB-9, `BUn;` on the USB virtual COM port. Then stop. The programming dialogue that follows is the undocumented part. It needs a captured session from a current 4.B12 (or later) load against a known 4.13.2 unit, checked byte for byte, including a failed-load recovery. Until that capture exists, the honest UI is a disabled button that says the loader protocol is not published.

6. **One flight at a time.** No polls, no `AT;`, no setup write, no second client, from the moment BOOT is expected until `R11;` answers again. The display is blank during the load, so the COM port is the only progress indicator.

7. **Verify after the restart.** Read `R11;` and require the version the file claimed. Then read the setup again and show a diff against the backup. Offer the restore only for fields Meridian can write safely. Do not auto-write the whole file.

8. **Do not invent a baud.** 57600 belongs to the v3 loader. The v4 addendum never states the bootloader baud. The capture has to include it.

9. **Leave the screen-saver and 4.14 interaction alone during the load.** A 4.14.g1 unit with the saver on stops answering serial until a power cycle. An update started in that state will time out and look like a brick. The feature should refuse to start unless the port is answering.

## Az/El, outside the 61%

The November 2022 Az/El addendum says the SETUP Utility programs those controllers. 4.11’s note says it added the then-current Az/El v2 features. 4.B12 raises allowable divide ratios. Meridian’s rotor list and protocol are azimuth-only. Counting Az/El as one more missing row would make the score **28.5 / 48 = 59%**. It is a second product in the same Windows program, and it is entirely absent here: elevation limits, the elevation OPTION values (the addendum names G-5400, G-5500, G-5500DC, MT1000, SPID, Yaesu elevation), negative elevations, and a separate point-and-shoot knob.

## Bottom of the page

| | |
| --- | --- |
| Commit | `974df5112384d3d90574fd36a146e606b1350592` |
| Branch this file is on | `review/g-parity` |
| Score | **28.5 / 47 (61%)** against azimuth SETUP Utility 4.B12 |
| Done / partial / missing | 20 / 17 / 10 |
| Top gaps | Calibration; v4 firmware update; rotor type, mode, security, and advanced bits; minimum speed and speed 11; network writes beyond the gateway, including Wi-Fi and module reset; restore; vanity; 4.14 serial sensor and magnetic variation |
| Unsafe in the build that ships | One-click limit, offset, and divider writes; screen-saver write with no confirm; driver allows a register write when it has not yet stored a firmware version. Factory reset and the bootloader are not sent. The live register path does read back |
