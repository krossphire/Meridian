# Meridian review C — overall

Independent read of the tree at `b88f9cb` (imported alpha24 history), plus the small fixes on `review/c-overall`. This is a code and test review. Nothing here was run against a real RT-21, a Windows machine, or a Mac. The open “no OK after `AT*SERIAL_ECHO=0`” bug is named once and left to the serial reviewers. I did not re-time the Windows IOCTLs.

The program is a real rotor UI, not a sketch. Heading, goto, stop, reconnect backoff, Find, and the settings page are understandable, and the mock matches the byte shapes the driver expects. The part that will not stay reliable is the rotor driver: one 3,000-line object owns the link, the polls, every setting write, and two different network-module implementations.

## Critical

No confirmed path sends `W0…;` or starts a network-module read on a TCP link. `registerWriteCommand` refuses a letter outside `A`–`Z` (`src/rotor/Rt21Protocol.cpp:112`), and an IP link is turned away before `AT;` (`src/rotor/Rt21Driver.cpp:2573`).

Two crashes were critical and are fixed on this branch.

`MainWindow::keyPressEvent` called `link->stopMotion()` for Escape and Space with no null check (`src/MainWindow.cpp:303`). The map’s own key handler already checks. The window handler did not. With no rotor in the list, `link` is null and the key crashes. The globe click and heading paths were already guarded. Escape and Space now are too.

`Rt21Finder::scanHosts` copied a lambda that still referred to another lambda on the for-loop stack. The slot runs later, from `loop.exec()`, after that stack frame is gone. A host that answered `BI1;` segfaulted in `QObject::property` (`src/rotor/Rt21Finder.cpp:437` is the log helper; the dangling capture was the `settle` lambda now at `src/rotor/Rt21Finder.cpp:449`). `tst_finder` hit this on the first Linux run. The lambda now copies the pieces it needs. Unverified on Windows and Mac.

## High

**The driver is the product, and it has two of everything.** `Rt21Driver` is about 3,030 lines and about 80 fields (`src/rotor/Rt21Driver.h:94`). Network entry is a `NetworkPhase` enum (`src/rotor/Rt21Driver.h:135`) driven by `handleNetworkFrame`. A second path still queues `Kind::Module` jobs, retries handshakes, and calls `finishNetwork` (`src/rotor/Rt21Driver.cpp:266` and `src/rotor/Rt21Driver.cpp:1162`). The live Read network button uses only the phase machine. `applySetupGroup` uses the job queue and is compiled out while `MERIDIAN_ALPHA_READONLY` is on (`CMakeLists.txt:9`, `src/rotor/Rt21Driver.cpp:2907`). `finishNetwork` throws away the `ok` and `message` it is passed and decides from flags instead (`src/rotor/Rt21Driver.cpp:1576`). An `OK` to the echo sets `networkGotField` before any `AT*KEY?` has been answered (`src/rotor/Rt21Driver.cpp:2346`), so a module that says OK and then goes quiet can be reported as a successful read.

Register writes have the same split. The buttons call `writeRegister`, which always sets `brightness = true` even for limits and the screen saver (`src/rotor/Rt21Driver.cpp:2693`). `writeConfiguration` sends the other shape, `W<letter>1<value>;`, via `writeEe` (`src/rotor/Rt21Protocol.cpp:101`) and is compiled out. `planGroup` builds a third list (`src/rotor/Rt21Setup.cpp:158`) that nothing in the default build sends.

**The network sequence blocks the GUI thread.** `preciseSleepMs` spins the calling thread (`src/rotor/Rt21Driver.cpp:66`). It runs for the 650 ms pre-`AT;` quiet (`src/rotor/Rt21Driver.cpp:2529`), the 520 ms wait after `<uart2>;`, and the 195 ms DTR-low hold on variants A and B (`src/rotor/Rt21Driver.cpp:1835` and `:1957`). The window cannot repaint, and Qt will not deliver `readyRead` until the spin ends. That is a separate defect from the Windows echo timing, and it is why the sequence is hard to test and hard to change. I did not touch those delays.

**Windows serial close depends on a private Qt header, and CI never compiles it.** `PortLeaveLines` casts a `QSerialPort` to reach `QSerialPortPrivate::settingsRestoredOnClose` (`src/rotor/Rt21Driver.cpp:131`). The include path is glued together from `Qt6_DIR` (`CMakeLists.txt:36`). A Qt patch that moves that field still compiles until the member type changes, then the Windows build breaks. `.github/workflows/ci.yml` builds and tests only on `ubuntu-24.04`. The `#if defined(Q_OS_WIN)` half of `cycleSerialPort` (`src/rotor/Rt21Driver.cpp:1840`) is the half that talks to the station’s Edgeport, and it is not in CI.

**Screen saver and shortest route do not match the register list in the review brief.** The brief says W is shortest route and T is screen saver. The code, the UI, the tests, and the in-tree utility notes say the opposite: the screen-saver Write button sends `WW0` plus the minutes (`src/ControllerColumn.cpp:367`, `src/rotor/Rt21Setup.cpp:192`), and T is the advanced-options bitfield whose bit 128 is described as point-and-shoot (`src/rotor/Rt21Setup.cpp:590`). `writeRegister`’s allow-list is `DEGHIKLNOPRSUVWY` (`src/rotor/Rt21Driver.cpp:2605`), which includes W and does not include T. H, I, and D match the brief (CCW limit, CW limit, offset). I did not retarget any letter. The utility-capture notes in `docs/RT21_PROTOCOL.md` are detailed and consistent with the code, and they disagree with the brief. Which source is right is unverified on hardware in this review. Do not swap the letters until a capture shows the bytes the Green Heron utility sends for those two controls.

**Read-back uses a different command than setup reads.** Setup reads send `R<letter>1;` (`readEe`, `src/rotor/Rt21Protocol.cpp:95`). A write reads back with `R<letter>;` and no digit (`registerReadCommand`, `src/rotor/Rt21Protocol.cpp:121`). The brief says the read-back is `R<letter>1;`. The protocol note says the digit is ignored and that a bench brightness check used `RO;`. Both forms are in the tree. I left the bytes alone because the bench note is the one hardware confirmation of that read. One helper should own it.

**Mac will not reproduce the Windows close/open gap.** The non-Windows variant D closes the port and raises DTR as soon as `open` returns (`src/rotor/Rt21Driver.cpp:1961`). There is no 200 ms low period on that path. On Windows the low period is mostly however long close and `CreateFile` take. Whether a Mac USB-serial reopen is near 200 ms is unverified. Variants A and B do sleep 195 ms on both sides.

## Medium

**CI and the release workflow do not cover the platforms the app claims.** CI is one Linux job (`.github/workflows/ci.yml:8`). `release.yml` builds Windows and macOS only on tags matching `v*`, and both jobs upload with `softprops/action-gh-release` (`release.yml:36` and `:61`). Two jobs publishing the same tag race; one upload can fail or replace the other. The macOS job is `macos-14` (Apple silicon), calls `macdeployqt` with no codesign and no notarization (`release.yml:58`), and has never been observed by this review. Whether `macdeployqt` is on `PATH` after `install-qt-action` is unverified.

**The docs disagree with the code and with each other.** `README.md` still says the program has not been tried on a real RT-21, that the screen saver is not written, and that the only EEPROM write shape is the digit-1 form. `docs/RT21_PROTOCOL.md` later says 4.13.2 writes are unlocked and also, in an older section, that brightness is the only write. `docs/ROADMAP.md` says writes stay compiled out. The live buttons are not behind `MERIDIAN_ALPHA_READONLY`. The flag only wraps `applySetupGroup`, `writeConfiguration`, calibrate, and pulse heading.

**Find’s order is “all serial ports, then the network,” not a separate USB pass.** `startFind` probes every present serial port, numerically sorted, and scans TCP only if none answer (`src/SettingsDialog.cpp:871` and `:893`). USB-serial and an Edgeport COM port share that list. A chip with no COM port is a hint from `UsbHint`, not a probe. The network leg asks only for port 6555 unless the user typed another port (`src/SettingsDialog.cpp:893`). `networkCandidatePorts` still lists 23, 4001, 5000, 6555, 8080, and 10001 (`src/rotor/Rt21Finder.cpp:148`) and Find does not use it. `docs/RT21_PROTOCOL.md` describes the longer list. The code matches the README (“TCP 6555 only”), not that doc.

**Status color is a bag of substrings.** `statusTone` (`src/StatusTone.h:15`) paints red when the text contains “couldn’t”, “timeout”, “ipv4”, and now “writes stay off”. A new error sentence that does not contain one of those words is grey. The map’s lower-left line is drawn with those colors (`src/GlobeWidget.cpp:868`). Green used to disappear after 4 seconds along with amber. Amber now stays until the next note (`src/rotor/Rt21Driver.cpp:523`). On the map, green is cleared rather than recolored; the settings line keeps the words and returns them to grey (`src/SettingsDialog.cpp:363`). Red stays in both places. The global stylesheet still sets widget text to `#e8eeea` (`src/main.cpp:72`). The status line overrides that. I did not find a status string drawn in white.

**A bad home location was a dialog.** Save called `QMessageBox::warning`. It now writes “Couldn't use that location…” on the status line (`src/SettingsDialog.cpp:441`). About Meridian is still a window the user opened on purpose.

**DHCP 0 vs 1 is only as sure as the unit test.** `addressModeText` says 0 is “the router assigns” and 1 is static (`src/rotor/Rt21Setup.cpp:149`). `tst_setup` locks that wording. The protocol note’s capture is `DHCP=0` together with a fixed address. That capture does not prove the label. Unverified. I did not flip it.

**Other protocol nicks, not chased further.** `isCommandEcho` treats every `AP…` frame as an echo because of `|| body.startsWith("AP")` (`src/rotor/Rt21Protocol.cpp:179`); the heading parser already rejects `AP`, so this is dead logic rather than a wrong heading. `OK` is a substring match (`src/rotor/Rt21Driver.cpp:2329`). A network query that times out records an empty field and moves on (`src/rotor/Rt21Driver.cpp:2272`). The traffic log used a static `QFile` from the Find thread and the GUI thread with no lock. It is locked now (`src/rotor/TrafficLog.cpp`).

## Low

- `eeCommand` / `eeRead` in `Rt21Setup.cpp` build the digit-1 form and nothing calls them.
- `formatSetupBackup` has a ternary whose both sides return the Wi-Fi key unchanged (`src/rotor/Rt21Setup.cpp` around the network loop). The backup file includes the key and the web password on purpose, so a later restore has them. That file sits next to the program.
- `classifyFirmware` uses `contains("4.13.2")`, so a hypothetical `4.13.20` would count as tested (`src/rotor/Rt21Setup.cpp:522`).
- `timeBeginPeriod(1)` during the Windows spin changes the process timer resolution for the length of the sleep (`src/rotor/Rt21Driver.cpp:72`).
- First launch still fills the home grid with `EM26` (`src/rotor/AppConfig.cpp:25`). Host and port stay empty. That matches “never pre-fill an IP or port.”

## What is in good shape

Heading framing, the 359.9° clamp, reconnect delays, numeric COM sorting (including `cu.usbserial-2` before `cu.usbserial-10`), meridian.ini beside the executable with an AppData fallback, and the refusal to enter the network module over TCP are clear and tested. Brightness is still a control. The status colors that the tests lock are amber, green, red, and grey, not white. The mock’s echo rules are strict enough that a Linux pty test can fail when the timing window is wrong. That pty cannot see DTR (`src/rotor/NetCycleProbe.h`), so those tests do not show that Windows matches the Green Heron utility.

## Architecture

Keep byte strings in `Rt21Protocol` and nowhere else. One write builder, one read builder, and a function that refuses a command starting with `W0`.

Replace the `brightness` flag with one register transaction: letter, value, name, then the steps wait-for-`06 <letter>`, 150 ms, read back, then the 10 s poll watch. The watch should not be what decides that the EEPROM matched. A matching read-back is the confirmation. Silence after that is a separate “stopped answering” line. This branch now does that split (`finishBrightnessWatch`).

Delete the `Kind::Module` queue, or make it the only implementation. The phase enum is the one that matches the capture (`AT;` → `<uart2>;` → close/open → echo → queries → ESC). `finishNetwork` should use the message its caller already computed. `networkGotField` should mean “a key was answered,” not “echo said OK.”

Turn the three spins into timer states (`PreAt`, `AfterUart`, `DtrLow`) so the GUI thread keeps running. Do that without changing the recorded Windows delays until the echo bug is understood. The same timer state can enforce a 200 ms DTR-low on Mac and Linux instead of hoping `close` + `open` takes that long.

`MERIDIAN_ALPHA_READONLY` should either wrap `writeRegister` or be removed. Today the name says writes are off while the Controller page sends them.

## Test gaps

Covered well: grids, coordinates, ZIPs, projection, framing, reconnect math, COM sort, mock TCP heading, a pty network read and gateway write, brightness ack plus read-back, firmware classification, status colors.

Not covered, or only covered by a comment:

- A table of on-wire bytes for H, I, D, W, and T, including the read-back form. Brightness `WO010;` / `RO;` is locked. The other letters are not, except `WD0180;` and `WW01;` as raw strings in `tst_setup`.
- An IP endpoint refusing Read network and the gateway write. The code does this. No test calls it.
- Find order: a USB hit must stop the scan before TCP. `scanHosts` is tested alone.
- The phase machine as data: ERROR then OK, a missing query, cancel, the 35 s quiet. `silentModuleSkipsQueries` checks that 12 tries send `AT;` and ESC and no `AT*`. It does not check the states.
- `writeEe` (digit 1) versus `registerWriteCommand` (digit 0) against a single expected style.
- The Windows `CreateFile` cycle and the Mac ioctl cycle. CI cannot see either.
- The map status line as pixels. `statusTone` is tested as a function.

`tst_setup` takes about two minutes because a silent module still walks all 12 tries with the real quiet delays. That is an honest test and a slow one.

## Mac readiness

The CMake side is started: `MACOSX_BUNDLE`, the bundle id `net.k5po.meridian`, IOKit and CoreFoundation (`CMakeLists.txt:47` and `:79`). `UsbHint` has an IOKit enumerator (`src/rotor/UsbHint.cpp` under `Q_OS_MACOS`). Serial open uses POSIX `TIOCM_*` ioctls (`src/rotor/SerialLines.h`), which is the right family for `cu.*` devices. Settings fall back through `QStandardPaths::GenericDataLocation`, which on macOS is `~/Library/Application Support`, when the folder beside the binary is not writable (`src/rotor/UserFiles.cpp:19`). Unverified by running it.

Not ready to ship on a Mac:

- No Mac job in CI, so the IOKit file and the Unix half of `cycleSerialPort` are not compiled here either (this review’s compile was Linux, which does compile the Unix half).
- Variant D does not hold DTR low for ~200 ms.
- The release `.dmg` is unsigned and is built on `macos-14` only. Intel Macs are unverified.
- A bundle’s `Contents/MacOS` is a poor place for `meridian.ini` when the app is writable, and `~/Library/Application Support/Meridian` when it is not. Worth a single explicit Mac path so a signed app does not try to write inside itself.

## Fixes on this branch

All of these keep alpha24’s features. Linux `ctest` (Qt 6.4.2): 7/7 passed after the changes. Not run on Windows or Mac.

- A missing `06 <letter>` ack fails the write. The driver no longer sends the read-back after a timeout (`src/rotor/Rt21Driver.cpp:345`, `failRegisterWrite` at `:509`). A non-ack write that timed out used to emit success, “Settings sent.” It now fails.
- Read-back mismatch says what came back. A matched read-back followed by no heading polls says the controller stopped answering. “Saved” requires the ack, a matching read-back, and at least one later heading.
- `writeRegister` and `writeNetworkSetting` call `firmwareWritesAllowed`. An empty or non-4.13.2 report does not go on the wire. The settings page already refused; the driver did not, and a late heading of `0.0` could be stored as the firmware (`src/rotor/Rt21Driver.cpp:1273`). Version, pot, and reset notes now ignore a frame that is not their reply.
- Amber and red notes stay until the next note. Green still clears after 4 seconds. “Writes stay off” is red.
- A bad home location is a status line, not a message box.
- Escape and Space no longer call through a null rotor pointer when the rotor list is empty. The map path already checked; the window key handler did not.
- Find’s network scan no longer crashes when a host answers.
- The traffic log is mutexed so Find and the driver can write it together.
- The framer drops a gathered buffer once it passes 1024 bytes without a terminator (`src/rotor/Rt21Protocol.cpp`, `Framer::append`).
- The unused `planGroup` AT commands end in LF, same as the live gateway write. They are still not sent while the readonly flag is on.
- The setup-copy sentence that said the screen saver is never written now matches the Display Write button.
- The version string is `0.1.0-alpha25c`.

## Windows zip

`Meridian-0.1.0-alpha25c-win64.zip` sits at the repo root (gitignored, same as the other zips). SHA-256:

`98c284d27d467de60e61486434aa7faa0168ac10291764e2da07aa6c21bab66a`

It is a MinGW cross-build of this branch against Qt 6.8.3, plus `meridian-mock.exe`, the Qt DLLs, `platforms/qwindows.dll`, the MinGW runtime, `LICENSE.txt`, and the Inter OFL. `file` reports a PE32+ GUI executable. It was not launched. There is no Windows machine in this review, so startup, the serial port, and the map are unverified for this zip.

## Unverified

- Every hardware claim in `docs/RT21_PROTOCOL.md`, including which letter is the screen saver.
- Whether `R<letter>;` and `R<letter>1;` are the same on 4.13.2. The project’s disassembly note says the digit is ignored. I did not check the hex file.
- The Windows echo/OK failure. Not investigated here.
- DHCP polarity.
- A Mac build, a signed `.dmg`, and DTR timing on a Mac USB-serial device.
- The GitHub Actions release race. Read from the workflow file, not from a run.
