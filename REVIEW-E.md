# REVIEW-E — first-run and everyday use at alpha25h

Reviewer E. Tree: `974df51` (Meridian 0.1.0-alpha25h). Independent read of the driver, settings, finder, and globe. No other `REVIEW-*.md` was opened.

Built with Qt 6.4.2 and g++. `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`: 7/7 passed (`tst_setup` 136 s, `tst_driver` 14 s, the rest under 4 s). Headless drives used `meridian-mock` and the same widgets as the app: a fresh config with no `meridian.ini`, map click, typed heading, Space to stop, Find/Scan against the mock, a TCP abort standing in for a pulled cable, and reconnect.

## What holds

- A fresh rotor is transport Unset. Host, TCP port, and COM are empty. The port spin shows blank, not 6555 or 6665. With `ttyS0` present the serial combo stays on the blank row (COM1, COM2, COM10 ordering is covered by `tst_setup`). Home is EM26.
- Map click east of center arms 90°. Enter in the heading editor sends that heading. Esc leaves the previous target alone. Space while the mock is turning sends stop: motion ends, the target tick clears, the link returns to On.
- A goto is one `AP1` after a heading poll. An old goto is not sent again on a later reconnect (`ap` stayed 1 across a 20 s reconnect). Traces from connect, stop, Find, and an IP network read contained no `W0…;` and no `T11` / `AT` on TCP.
- `readNetwork()` on a live IP socket finishes immediately with "Needs a USB or serial connection; reading over IP drops the link." The Read network button is disabled. The register-W write-back stays off (`kRearmRegisterW` is false).
- Scan the network against the mock listed `127.0.0.1` in under half a second. Clicking that result saved the answering port and connected.
- Settings success text is green (`#7dcea0`) and four seconds later the same words are neutral grey (`#8ea099`). A failure note is still on the driver after four seconds. "No reply from the RT-21." stays; "Saved Brightness" is drawn in green while it is current.

## Major

### 1. A red "not connected" line stays up after the rotor is back

`src/rotor/Rt21Driver.cpp:622`, `src/rotor/Rt21Driver.cpp:1005`, `src/MainWindow.cpp:145`

`showNote` keeps a failure forever (the four-second timer is stopped). `onConnected` does not clear `note_`. `refreshStatus` prints the note ahead of the link, so Link::On is hidden for as long as that string is set.

Repro: connect to the mock, abort the client socket, click the map while the link is down. The status note becomes "RT-21 not connected — check settings and connections". The socket comes back (about 12 s when the click opens it immediately). The link is On, the detail is "Connected to 127.0.0.1:…", and the note is still the red not-connected sentence.

Fix: clear `note_` when the link reaches On, and let a live On/Moving state outrank a stale failure note. Keep the red text only while the link is still failed.

### 2. Pulling the cable leaves the target tick, and the status line does not say why

`src/rotor/Rt21Driver.cpp:900`, `src/rotor/Rt21Driver.cpp:1041`, `src/rotor/Rt21Driver.cpp:1093`, `src/rotor/Rt21Driver.cpp:1526`, `src/MainWindow.cpp:152`, `src/MainWindow.cpp:167`

`failLink` clears the pending command and does not clear `target_` or emit `targetChanged`. Once any heading has been seen, `refreshStatus` replaces the failure text with "Last seen …" and forces red. The real sentence is only the dot tooltip. `heading` is marked stale only for On and Moving, so the big number stays bright while the link is dead. The next heading poll treats an uncleared target as moving unless that one frame says stopped (`noteArrival`).

Aborting the socket also surfaces Qt's "Unknown error", because `errorString()` is used whenever it is non-empty. The clearer "The rotor closed the connection." is only the fallback for an empty string.

Repro: connect, `goTo(300)`, wait until the mock is moving, abort the socket. The link is Failed, detail is "Unknown error", target is still 300, and the rotor is still turning. Leave it. About 20 s later the link is On again, the mock is frozen at 20.4°, the UI heading is 20.4°, and the target is still 300. No second `AP` was sent.

Fix: on failure, clear the target and say the fault on the status line (keep "Last seen" as a second clause). Map an empty or "Unknown error" string to "The rotor closed the connection." Dim the heading while the link is Failed. Do not paint Moving from a target the driver has already dropped.

### 3. Find at 19200 is saved and opened at 4800

`src/rotor/Rt21Finder.cpp:251`, `src/SettingsDialog.cpp:590`, `src/SettingsDialog.cpp:763`, `src/SettingsDialog.cpp:828`, `src/SettingsDialog.cpp:990`

The probe list is 4800, 9600, 19200. The baud combo has only 4800 and 9600. `loadRotor` displays anything other than 9600 as 4800, then `commitDraft` → `storeRotor` writes that combo value. `applyFound` sets `rotor.baud = 19200` and then calls both.

Repro: present a serial hit at 19200 and click the result button. The combo reads 4800 and `meridian.ini` contains `baud=4800`. The next open uses 4800, the controller stays silent, and the failure path in finding 2 takes over.

Fix: keep 19200 in the combo and in `storeRotor`. Persist `found.endpoint.baud` instead of folding every non-9600 value to 4800.

### 4. Read network with no open port never finishes

`src/SetupPanel.cpp:274`, `src/SetupPanel.cpp:372`, `src/rotor/Rt21Driver.cpp:2996`, `src/rotor/Rt21Driver.cpp:1857`, `src/rotor/Rt21Driver.cpp:2764`, `src/rotor/Rt21Driver.cpp:1589`

The button is enabled whenever the live link is not IP. A fresh install, a serial rotor that is Off, and a serial rotor that is Failed all take that path. `readNetwork` does not require an open port. It emits "Reading network (try 1 of 12)…", sends the lead-in into a null `io`, and queues a warmup poll. `pump` returns when `io` is null, so the reply timer never starts and the 15 s warmup cap is never checked. `networkFinished` does not fire. Cancel is the only way out. The same hang happens if the cable is already unplugged and the user hits Read network.

Repro: fresh window, open Settings, click Read network. The button is enabled. After 2.5 s the status line is "Reading network (try 1 of 12)…" and `networkFinished` has not been emitted.

Fix: if the transport is not an open serial port, finish at once with "Connect to a rotor before reading the network module." and leave the button disabled until USB or serial is actually up.

### 5. Find drops a live serial link without stopping the rotor

`src/SettingsDialog.cpp:860`, `src/rotor/Rt21Driver.cpp:958`

Find has to close the COM port before it can probe. It calls `disconnectLink`, which closes the port and does not write `;`. A turn that was in progress keeps running on the controller until it reaches the last target or a limit. The UI target is cleared by `disconnectLink`, so the map looks stopped.

Repro (code path; not run on a USB dongle here): connect serial, start a turn, click Find RT-21. The log line is `FIND release <port>`, then the port is closed with no stop byte.

Fix: call `stopMotion()` and wait for the stop to hit the wire before `disconnectLink`.

### 6. Save on a bad location opens a dialog

`src/SettingsDialog.cpp:440`

The location field already paints the parse error on the summary label. Save still calls `QMessageBox::warning`. The setup status line stays empty. This is the only modal found on the first-run path.

Repro: Settings, home `not-a-place`, Save. A dialog titled "Location" says "Couldn't parse that location." The status label is blank.

Fix: `setStatus` with that sentence and stay on the Station page. Do not open a message box.

### 7. Space or Esc crashes after the last rotor is removed

`src/MainWindow.cpp:303`

`keyPressEvent` calls `link->stopMotion()` with no null check. Removing the only rotor retires the driver and sets `link` to null. The globe's own key handler is null-safe; the window handler is not.

Repro: Settings, Remove, close the dialog (`driverCount` is 0), deliver Space to the main window. The process exits with SIGSEGV (139). A Space that the globe handles while it has focus does not crash.

Fix: return immediately when `link` is null.

## Minor

### 8. Find's progress and its failure never use the status line

`src/SettingsDialog.cpp:858`, `src/SettingsDialog.cpp:949`, `src/SettingsDialog.cpp:357`

"Checking USB and COM ports…", "Scan cancelled.", "No RT-21 found.", and "Pick the RT-21 to use." are written to `findNote` in the default text color. `setStatus` is not called, so the line stays blank: no amber while scanning, no red when nothing answers. Observed on a fresh dialog: during Find, `findNote` was "Checking USB and COM ports…" and `setupStatus` was empty; after Cancel, the note was "Scan cancelled." and the status line was still empty.

Fix: route those sentences through `setStatus` (amber while the scan runs, red for "No RT-21 found." and a cancelled scan that found nothing).

### 9. Find probes every COM port in numerical order, USB included in that one list

`src/rotor/Rt21Finder.cpp:367`

`scanSerialPorts` sorts `QSerialPortInfo` by name and probes that order at 4800, then 9600, then 19200, then the network. A USB RT-21 on COM6 waits behind COM1–COM5. Each silent port costs three 650 ms probes. Network still runs only after serial misses, and it stops at the first `BI1;` plus version. That part matches.

Fix: split the list into USB-serial (vendor id, a known UART chip, `ttyUSB` / `ttyACM`) and other ports, sort each group numerically, probe USB, then the other COM ports, then the subnet.

### 10. In-progress text on the map is cleared on the same four-second timer as success

`src/rotor/Rt21Driver.cpp:622`, `src/GlobeWidget.cpp:519`

Any note that is not a failure is wiped after 4 s. "Reading network (try 1 of 12)…" is gone from the driver note after four seconds even though the read is still running. A turn does not use the status line at all: while the mock was moving, the bottom band had no amber pixels; the beam and the moving dot are yellow `(255, 191, 0)`, and the amber used for Connecting is `(224, 177, 92)`. Connect success turns the dot green and leaves the status line blank, so there is nothing to fade.

Fix: run the four-second clear only for Success. Leave Busy until the operation ends. While Moving, either keep an amber status line or paint the dot with the Busy amber and fade a short "Turning to N°" when the move finishes.

### 11. A map click before any rotor is set up looks like no click

`src/MainWindow.cpp:138`, `src/rotor/Rt21Driver.cpp:1634`

An unconfigured window always paints "Set up your RT-21 in settings" and returns before the transient note. `goTo` posts that same sentence, classified Neutral, so the line does not change and it is not red. Observed: tone 0, link Off, after a click to 90°.

Fix: when the click is ignored, set a failure note the early return does not discard, for example "No rotor yet — use Find RT-21 in settings."

## Nit

### 12. The network-open log always says 4800

`src/rotor/Rt21Driver.cpp:1926`

`primeUtilityLines` programs the real baud, then logs "4800 8N1" unconditionally. A 9600 or 19200 session looks like 4800 in the traffic log.

Fix: log `endpoint.baud`.

### 13. One network error is painted neutral

`src/StatusTone.h:15`, `src/SetupPanel.cpp:335`

"The module didn't return a name. USB is the reliable way to read it." matches none of the failure phrases (`didn't answer` is the one that is listed). It is grey and, on the map, it is cleared after four seconds.

Fix: add that sentence, or a shared "didn't" / "no name" check, to the failure list.

## Rank

1. Stale red "not connected" after a good reconnect (major 1).
2. Cable-pull keeps the target and hides the fault behind "Last seen" / "Unknown error" (major 2).
3. A 19200 find is stored as 4800 (major 3).
4. Read network with the link down never finishes (major 4).
5. Find releases a live serial port without sending stop (major 5).
