# RT-21 Setup Utility parity

Meridian should eventually replace the Green Heron RT-21 Setup Utility (4.B12). Until each write has been checked on Andrew's rotor, **writes stay compiled out** (`MERIDIAN_ALPHA_READONLY`). Reads and backups are available. This inventory is the map for that work. It is not a promise that Apply sends anything in the current build.

Status words:

- **done** — in the app and safe to use now
- **read-only** — the screen can show it; the write exists in source but is compiled out
- **todo** — not built yet
- **excluded** — will not be sent unless a later, explicit, safe path is added

Risk is about a mistaken write: **low** (display or a value you can put back), **medium** (motion limits and speed), **high** (calibration, the network module, or anything that reboots).

## How a write gets approved

On the real RT-21, one group at a time:

1. Read every field and save the backup JSON.
2. Change one harmless value (brightness is the first candidate).
3. Read it back and confirm the number.
4. Restore the backup and read it back again.
5. Only then compile that group in, still behind the Apply confirmation.

Do not enable a second group in the same session. Factory reset and the bootloader stay excluded.

## Find RT-21

**done.** On an unconfigured rotor, Settings leads with **Find RT-21**. It checks USB and COM ports first, shows a friendly name when the adapter is FTDI, Prolific, CH340, or CP210, and probes each port with `BI1;` at 4800 then 9600. A port that will not open (another program has it, including Flex SmartSDR CAT ports) is skipped with no error and no dialog. A result looks like `RT-21 on COM6 (USB, 4800), heading 132°`. One click saves it. **Scan the network** is a separate button: it walks local /24s for TCP port 6555, a few hosts at a time, and only when asked. Nothing is selected automatically. A saved rotor is left alone.

## Controller tab

Reads work over serial, USB, and IP. The same command bytes go out on every transport. The tab stays grouped, in plain language, in this order:

1. Identity — version (`R11;`), pot A/D (`R01;`), last reset (`R41;`)
2. Motion — min speed, max speed, ramp, delays, counter-rotate delay
3. Stops and travel — CCW limit, CW limit, cal range, overtravel
4. Display — brightness, and the display bits inside advanced options
5. Calibration — offset, CCW cal, CW cal, pulse divider, alt offsets, pulse heading, cal-here buttons
6. Rotor type — option, mode, security, limit switch (shown, not written until the codes are known)
7. Network module — its own section, below, not mixed into EEPROM letters

Apply, calibration, and pulse heading stay disabled while writes are compiled out. Backup writes a JSON file on this computer and does not send a command.

## EEPROM letters

Read command is `R` + letter + `1;`. A numeric write, when a group is later enabled, is `W` + letter + `1` + decimal + `;` (`n = 1`, store, do not reboot). Example: offset 180 is `WD1180;`. That shape is inferred from Appendix F and **has not been checked on hardware**.

| ID | What you see | Manual default | Read | Write | Risk | Status |
| --- | --- | --- | --- | --- | --- | --- |
| A | CCW cal | 0 | `RA1;` | `WA1` + value + `;` | high | read-only |
| B | CW cal | 950 | `RB1;` | `WB1` + value + `;` | high | read-only |
| C | Reserved | | `RC1;` | none | | excluded |
| D | Offset | 180 | `RD1;` | `WD1` + value + `;` | medium | read-only |
| E | Delays (brake and reversal are not separate) | 3 | `RE1;` | `WE1` + value + `;` | medium | read-only |
| F | Min speed | 3 | `RF1;` | `WF1` + value + `;` | medium | read-only |
| G | Max speed | 10 | `RG1;` | `WG1` + value + `;` | medium | read-only |
| H | CCW limit | 180 | `RH1;` | `WH1` + value + `;` | medium | read-only |
| I | CW limit | 180 | `RI1;` | `WI1` + value + `;` | medium | read-only |
| J | Option (pot / counter) | Pot | `RJ1;` | unknown codes | high | read-only |
| K | Pulse divider | 3960 | `RK1;` | `WK1` + value + `;` | high | read-only |
| L | Cal range | 360 | `RL1;` | `WL1` + value + `;` | medium | read-only |
| M | Mode | Normal | `RM1;` | unknown codes | high | read-only |
| N | Ramp | 3 | `RN1;` | `WN1` + value + `;` | medium | read-only |
| O | Brightness | 8 | `RO1;` | `WO1` + value + `;` | low | read-only |
| P | Fail timeout | 4 | `RP1;` | `WP1` + value + `;` | low | read-only |
| Q | Security | Admin | `RQ1;` | unknown | high | read-only |
| R | Alt offset 1 | 361 | `RR1;` | `WR1` + value + `;` | medium | read-only |
| S | Alt offset 2 | 361 | `RS1;` | `WS1` + value + `;` | medium | read-only |
| T | Advanced options (bits below) | 0 | `RT1;` | `WT1` + value + `;` | medium | read-only |
| U | Overtravel | 90 | `RU1;` | `WU1` + value + `;` | medium | read-only |
| V | Counter-rotate delay | 10 | `RV1;` | `WV1` + value + `;` | low | read-only |
| W | Reserved | | `RW1;` | none | | excluded |
| X | Limit switch position | | `RX1;` | none | | read-only |
| Y | Knob time | 40 | `RY1;` | `WY1` + value + `;` | low | read-only |
| Z | Not used | | `RZ1;` | none | | excluded |

`T` bits, same page: 1 freeze display, 2 fast brake release, 4 auto go-back, 8 elevation, 16 auto dim, 32 analog heading out, 64 EME, 128 point-and-shoot, 256 port is 9600, 512 2-bit encoder, 1024 demo, 2048 allow cal to the CCW limit.

Other controller actions:

| Action | Command | Risk | Status |
| --- | --- | --- | --- |
| Version | `R11;` | low | done (read) |
| Pot A/D | `R01;` | low | done (read) |
| Last abnormal reset | `R41;` | low | done (read) |
| Calibrate CCW here | `C01;` | high | read-only |
| Calibrate CW here | `C11;` | high | read-only |
| Set pulse heading | `AW1` + `xxx.y` + `;` | high | read-only |
| Backup to a JSON file | none on the wire | low | done |
| Factory reset | `W00;` | high | excluded |
| Bootloader | `B2n;` / `BUn;` | high | excluded |
| CPU reset | `R3n;` | high | excluded |

## Network module

**todo**, and the priority inside setup parity. The Green Heron utility often crashes while editing the module's own Ethernet and Wi-Fi settings. Those settings are not EEPROM letters. They belong to the LAN module (IP address, DHCP, TCP port, Wi-Fi SSID, passphrase) and are reached through the module passthrough (`ATn;`, `T0n;`, `T1n;`), which Meridian does not send today.

Design when that page is built:

- It works over serial, USB, or IP. The same forms are used on every transport.
- Prefer serial or USB for a network change. Say so on the page, in one sentence.
- If the session is IP and the user changes the address, DHCP, or port, warn before the write that this connection will drop, show the new address to reconnect to, and offer to reconnect there automatically after the write.
- Back up the current network values before any write.
- Do not store the passphrase in the traffic log.
- Rate-limit and confirm. No scan and no write runs by itself.

`docs/RT21_PROTOCOL.md` remains the byte-level note. This file is the product map.
