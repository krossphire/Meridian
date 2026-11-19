# Review F — RT-21 protocol and transport

Independent review of Meridian 0.1.0-alpha25h at `974df51` (`Keep a saved TCP port when Find connects over serial`). Scope is the serial and TCP drivers, framing, timeouts, retries, partial and garbled replies, reconnect, the Find probe thread versus the real port open, and the network-entry sequence `R11;` / `T11;` / `R01;` / `AT;` / `<uart2>;` / `AT*SERIAL_ECHO=0`. No other `REVIEW-*.md` was read before this was written. Application code was not changed.

## How this was checked

- Read `src/rotor/Rt21Driver.cpp`, `Rt21Protocol.cpp`, `Rt21Finder.cpp`, `SerialLines.h`, `Reconnect.h`, `TrafficLog.cpp`, the network path in `SettingsDialog.cpp` / `MainWindow.cpp`, `docs/RT21_PROTOCOL.md`, `docs/RT21_SETUP_PARITY.md`, and `tests/tst_driver.cpp`, `tst_finder.cpp`, `tst_setup.cpp`, `tst_core.cpp`.
- Built with g++ 13 and Qt 6.4.2. `ctest` (offscreen): 7/7 passed in 153 s. `tst_setup` alone was 136 s because the silent-module case walks all 12 tries.
- Rebuilt with `-fsanitize=address,undefined` and reran the same suite (`detect_leaks=0`). 7/7 passed, no ASan or UBSan report. That run does not execute the Windows raw-handle path, and ASan does not catch data races. A separate ASan/UBSan probe of `Rt21::Framer` is cited under M2.
- On the Linux pty the network read logged `network DTR-low 1 ms variant D` and `network uart2->ECHO 521 ms`. The captured utility successes are about 190–197 ms of DTR low and about 730 ms from `<uart2>;` to the echo.

## What holds

- Open is 4800 8N1, no flow control, DTR and RTS raised. TCP connect is capped at 3 s. A refused TCP port waits 15 s, then backs off to a 5 minute cap (`Reconnect.h`). A serial connect stops that timer (`Rt21Driver.cpp:1006`).
- `T11;` is sent on serial only. `tst_driver` asserts a TCP session never sends it. `AT*` lines end in LF and carry no semicolon. The read does not send `T01;`, `WW0`, `W0`, or `AT*RESET`.
- Echo handling matches the capture notes: one immediate resend after `ERROR`, no resend into silence, 400 ms more, then a single ESC. Queries are one `AT*KEY?` at a time with a 300 ms cap. Twelve tries, a 15 s warmup cap, and a 35 s quiet after a `<uart2>;` that was actually recognized.
- Brightness waits 150 ms after `06 <letter> ;` before `RO;` (`Rt21Driver.cpp:1303`). That covers the disassembly note that interrupts are off for about 25–30 ms and the receive buffer is cleared.
- Find copies COM names and drops `QSerialPortInfo` before any probe (`Rt21Finder.cpp:364`). The probe port is disconnected, closed, and `processEvents`'d before it is destroyed (`Rt21Finder.cpp:185`). The TCP sweep disconnects sockets before `delete` (`Rt21Finder.cpp:572`). A hit that replaces a down TCP endpoint waits for `QThread::finished` before `connectEndpoint` (`SettingsDialog.cpp:936`).

## Firmware facts used here

Written down in `docs/RT21_PROTOCOL.md`:

- A rotator command runs only when the last byte is `;`. A line feed after `;` corrupts the next command. A bare `;` clears the controller receive buffer.
- After a register-write ack, interrupts are off for about 25–30 ms and the receive buffer is cleared. `W0...;` is a different command: it displays a message with interrupts off for about 2 seconds. Meridian's register writer does not send that form.
- Firmware delays of 1–250 ms run with interrupts off.
- `T11;` alone made the controller ignore `R01;` until ESC. The module drops command mode after about 25–41 s with nothing on the wire. Pass-through times out after about 31 s of quiet.
- With DTR low, an Edgeport port transmits and the RT-21 sends nothing back (`Rt21Driver.cpp:923`).

Not written down anywhere in this tree: front-panel buttons are blocked while the AT bridge is up, and UART1 and UART2 share one buffer. Both are treated as real below. The host has a single framer on the one COM port, which is the right shape for a shared UART. The failure mode is two speakers concatenated in that buffer, and the framer does not resync when the delimiter is missing (M2, M3).

The button fact is why leaving the bridge without ESC is a controller wedge and not only a missed read. Until the module's own idle timer fires, the panel cannot stop the rotor, and any byte Meridian sends (a poll, a new `T11;`) restarts that timer.

## Findings

### B1. A latched `T11;` / AT bridge is dropped without ESC, then the next open latches it again

**Where:** `src/rotor/Rt21Driver.cpp:340`, `src/rotor/Rt21Driver.cpp:755`, `src/rotor/Rt21Driver.cpp:1454`, `src/rotor/Rt21Driver.cpp:1634`, `src/rotor/Rt21Driver.cpp:2828`, `src/MainWindow.cpp:48`, `src/MainWindow.cpp:304`

Every serial connect sends `R11;`, then `T11;` and `R01;` with no wait (`beginSerialRoute`). That matches the utility's cold open up through `R01;`. The utility then sends ESC. This path does not.

If `R01;` is ignored, the reply timer at `Rt21Driver.cpp:340` logs `serial route no reply`, clears the flag, and starts `BI1;` polls. The documented recovery for that state is ESC. The polls are the opposite: they are traffic, so the module's 25–41 s idle timer keeps restarting. Three missed polls call `failLink`, and the first session-closed reconnect is 20 s (`Reconnect.h:28`), which is shorter than the pass-through timeout. The new open sends `T11;` again.

The same hole is on every teardown while the bridge is up:

- `closeIo` (`Rt21Driver.cpp:755`) restores the Qt port, emits "The link closed during the network read.", and closes. It writes no `0x1b`.
- `disconnectLink` and a map click during the Windows raw-handle window both get there. `transportOpen()` (`Rt21Driver.cpp:648`) is true only for a connected TCP socket or an open `QSerialPort`. After `<uart2>;` the Qt port is closed and the live handle is `winRaw`, so `transportOpen()` is false. `goTo` then calls `connectEndpoint` (`Rt21Driver.cpp:1645`), which is `closeIo` plus a new open, with no ESC in between. Escape and Space on the main window call `stopMotion` even while a network read is in progress (`MainWindow.cpp:304`).

`cancelNetwork` does send ESC. Disconnect, a failed route, and a goto while the raw handle is up do not. The mock answers `R01;` and `BI1;` after `T11;`, so `tst_driver::serialPty` stays green and never models the latch.

**Suggested fix:** Send the `R11;` / `T11;` / `R01;` trio only as the lead-in to a network read, and send `0x1b` on the route timeout before any poll. Make `closeIo`, `disconnectLink`, and `goTo` send ESC and then stay silent for at least 35 s before the next open whenever `networkEntered` is set or `networkPhase` is not idle. Count a live `winRaw` handle as transport-open so a map click cannot tear the bridge down. Keep the reconnect timer from firing during that silence.

### M1. The live reopen holds DTR low for 1 ms, not the captured ~195 ms

**Where:** `src/rotor/Rt21Driver.cpp:64`, `src/rotor/Rt21Driver.cpp:2095`, `src/rotor/Rt21Driver.cpp:2130`, `src/rotor/Rt21Driver.cpp:2153`, `src/rotor/Rt21Driver.cpp:2267`, `tests/tst_setup.cpp:232`

The constants at the top of the driver record the two successful captures: DTR low 190–197 ms, open about 40 ms after close, echo 3–14 ms after the final SET_DTR, `<uart2>;` to echo about 730 ms. `kDtrLowMs` (195) and `kOpenAfterCloseMs` (40) are the names of that. `variantForThisTry()` always returns `'D'`, and the `'D'` arm closes and reopens immediately. The 195 ms sleep is only on the `'A'` / `'B'` arm below it, which that function never selects.

This run's traffic log, from `tst_setup` against the pty:

```text
network uart2->DTR low 520 ms
network DTR-low 1 ms variant D
network DTR high->ECHO 0 ms
network uart2->ECHO 521 ms
```

The suite accepts that. `tst_setup.cpp:239` allows DTR-low of 0–150 ms and uart2→ECHO up to 720 ms, so a pulse of 1 ms passes, and the captured 730 ms would fail the test. The mock treats an echo as valid when it is 480–800 ms after `<uart2>;` and within 40 ms of DTR rising (`Rt21Simulator.cpp:335`). It does not require a 190 ms break.

On a fast open the module sees a DTR glitch, then `AT*SERIAL_ECHO=0` about 200 ms earlier than either captured success. `primeUtilityLines` can also return after `CLRDTR` if `GetCommState` fails (`Rt21Driver.cpp:1884`), and the Edgeport note says the RT-21 stays silent while DTR is low.

**Suggested fix:** On the `'D'` path, drop DTR, wait until `networkDtrLowMs + 195` (with the ~40 ms close-to-open gap inside that window), raise DTR, then send the echo within 15 ms. Point the test at the capture: DTR-low about 180–210 ms, uart2→ECHO about 700–760 ms. If `GetCommState` fails, put DTR back before returning.

### M2. A stray `;` or a partial reply is parsed as a heading, and `OK` glued to `<uart2>` is missed

**Where:** `src/rotor/Rt21Protocol.cpp:276`, `src/rotor/Rt21Protocol.cpp:289`, `src/rotor/Rt21Driver.cpp:1135`, `src/rotor/Rt21Driver.cpp:2609`

The framer has no maximum length. Bytes with no `;` and no CR stay in `buffer` forever (a 2 MB run of `A` produced 0 frames and was still buffered). A leading `;` is stored as a DCU-1 start, and the fourth byte of `;ddd` commits a frame immediately:

| Bytes | Frames the probe got |
| --- | --- |
| `131.9;` | `131.9` = 131.9 |
| `;131.9;` | `131` = 131.0, then `.9` = 0.9 |
| `131` then `132.4;` | `131132.4`, rejected |
| `OK` then `<uart2>;` | one frame `OK<uart2>` |

`parseHeading(".9")` is 0.9, inside 0–359.9, so the ghost is a real heading update. A shared UART makes this ordinary: the rotor's leftover and the module's `<uart2>;` land in one buffer. `handleNetworkFrame` accepts only a frame that is exactly `<uart2>` (`Rt21Driver.cpp:2610`) and returns true for anything else, so `OK<uart2>` is eaten and the 500 ms timer fires. The module did enter command mode. Meridian's next move is ESC and another try. `takeLeadingOk` strips a chunk that *starts* with `OK`, and it does not help once any other byte is in front, or once the two pieces arrive in separate reads (the first read leaves `OK` in the framer).

`networkT11Watch` is set when `T11;` is sent (`Rt21Driver.cpp:2982`) and cleared only when `<uart2>` matches (`Rt21Driver.cpp:2616`). A missed prompt leaves the flag set, so a later chunk that starts with `OK` is deleted before framing.

`isCommandEcho` (`Rt21Protocol.cpp:179`) returns true for every `AP` frame because the condition ends in `|| body.startsWith("AP")`. Harmless, and it hides the heading check in front of it.

**Suggested fix:** Cap the buffer (256 bytes is enough for these replies) and drop it on overflow. Commit `;ddd` only when the next byte is absent and a short gap has passed, or when `;` or CR arrives. Treat a frame that contains `<uart2>` as the prompt. Clear `networkT11Watch` on every exit. Add the four rows above to `tst_core`.

### M3. The pre-AT quiet and the 520 ms uart2 wait block the GUI thread and do not drain the port

**Where:** `src/rotor/Rt21Driver.cpp:107`, `src/rotor/Rt21Driver.cpp:2130`, `src/rotor/Rt21Driver.cpp:2950`

`preciseSleepMs` is a `Sleep` plus a busy spin, on the GUI thread. `holdQuietThenEnter` spins for 650 ms. `cycleSerialPort` spins until 520 ms after `<uart2>;`. ReadyRead, Cancel, and Escape sit in the queue until the spin ends. Bytes the controller sends during the quiet stay in the OS buffer. `startNetworkPass` then clears the framer and sends `AT;`, and only afterward does the event loop read. Those bytes are framed together with `<uart2>;`, which is M2's glue case.

The quiet exists so the shared UART can finish the last `R21;` / `R01;` before `AT;`. The controller side may go idle. The host side does not read, so the quiet does not separate the two speakers. Cancel during the spin cannot put ESC on the wire until the spin returns (B1 is then whether that path sends ESC at all).

The 20 ms DTR dip in `primeUtilityLines` is the same kind of wait, short enough to be minor on its own. The 650 ms and 520 ms waits are the ones that collide with the bridge.

**Suggested fix:** Arm a `QTimer` for both waits. While it runs, keep reading and drop complete frames. Send `AT;` only after that drain. Then Cancel and Escape run during the wait.

### M4. A serial Find reopens the COM port before the probe thread has finished

**Where:** `src/SettingsDialog.cpp:860`, `src/SettingsDialog.cpp:924`, `src/SettingsDialog.cpp:936`, `src/SettingsDialog.cpp:965`, `src/rotor/Rt21Finder.cpp:185`, `src/rotor/TrafficLog.cpp:116`

The alpha25d crash was an ntdll `c0000005` from overlapped I/O into a port the probe had already freed, while the GUI opened that same COM port. The current code does the right things around the probe object itself: disconnect, close, `processEvents`, and a second `processEvents` at the bottom of the worker before it posts `finishFind`.

The open is still posted from the worker, and the worker then returns:

- `finishFind` is queued at `SettingsDialog.cpp:928`, then the thread function ends.
- The `QThread::finished` handler opens the port only for `pendingApply`.
- `pendingApply` is set only when `findReplacesSavedEndpoint` is true, which requires the *saved* transport to be TCP (`Rt21Finder.cpp:256`).
- A Find that started from a live serial session sets `resumeSerial`, disconnects, probes that COM port, and reopens it from `finishFind` (`SettingsDialog.cpp:965`). That slot can run as soon as it is queued, while the worker is still inside `QThread` shutdown. That is the same handoff as the access violation, on the path that reopens the port the probe just used.

`tst_driver::findHitConnectsWhileIpReconnectIsArmed` waits until `thread->isFinished()` and only then calls `connectEndpoint`. The production resume path does not.

`TrafficLog::recordSession` flips `enabled` and writes a `QFile` with no lock (`TrafficLog.cpp:116`). The probe thread calls it for every `FIND-TX` / `FIND-RX` while `finishFind` can already be inside `connectEndpoint` on the GUI thread, which logs too. `QFile` and `QString` are not safe for that. ASan will not report it. ThreadSanitizer would.

**Suggested fix:** Reopen the COM port only from the `finished` handler, including the resume path. Keep `processEvents` on the worker before it returns. Put one mutex around `TrafficLog`.

### M5. Stop sends `;` into the AT bridge and turns line mode off

**Where:** `src/rotor/Rt21Driver.cpp:1655`, `src/rotor/Rt21Protocol.cpp:307`, `src/MainWindow.cpp:304`

`stopMotion` clears the queue, writes `;`, calls `framer.clear()`, and drops the inflight job. `Framer::clear` sets `lineMode` back to false. It does not look at `networkMode` or `networkPhase`.

`;` is the rotor "clear the receive buffer" command. The protocol note says a semicolon on an AT line failed in the captures. During `WaitEcho` / `Query` the port is open (on Linux the Qt port, on Windows only if the raw handle is not the one `io` points at). The `;` goes to the module. The following `OK\r\n` or `DHCP=0\r\n` no longer terminates on CR, because line mode is off, so the rest of that try times out. Escape on the main window is this function, including while Read network is showing Cancel. Cancel itself calls `cancelNetwork` and sends `0x1b`. The two keys do different things depending on which widget has focus.

A goto during an active network read on Linux is milder: `sendQuery` returns while `networkMode` is set, and the 3 s command timer drops the target. On Windows, during the raw handle, it is B1.

**Suggested fix:** While a network phase is active, make Stop and Escape call `cancelNetwork` and leave the framer's line mode alone. Do not write `;` until ESC has been sent and a heading is back.

### M6. The Windows close and the raw handle can block, leak a completion, or ignore the live port

**Where:** `src/rotor/Rt21Driver.cpp:186`, `src/rotor/Rt21Driver.cpp:205`, `src/rotor/Rt21Driver.cpp:226`, `src/rotor/Rt21Driver.cpp:648`, `src/rotor/Rt21Driver.cpp:1172`, `src/rotor/Rt21Driver.cpp:2153`

`wakeSerialForClose` exists to arm a 1 ms read timeout, drop the comm mask, and `CancelIoEx` before `QSerialPort::close`, because close otherwise waits out `WaitCommEvent` and CancelIo from this thread does not always wake it. Nothing calls it. `cycleSerialPort` does its own `CancelIoEx` and `SetCommMask(0)` and then `serial->close()` with no short timeouts (`Rt21Driver.cpp:2158`). `closeIo` calls `port->close()` with neither. On the Edgeport that wait has already been hundreds of milliseconds, and a wait that does not wake is unbounded, on the GUI thread, while the module is in command mode.

`writeWinRaw` returns when the write is still pending after 300 ms (`Rt21Driver.cpp:241`). The next write `ZeroMemory`s that same `OVERLAPPED`. Completing the first write then writes into a reused structure. `closeWinRaw` waits 50 ms for the read event and `CloseHandle`s anyway (`Rt21Driver.cpp:212`). A completion after that is a use-after-free in the same family as the ntdll crash.

`pollRawSerial` does check `winRaw != raw` after a callback that may have closed the handle. That part is careful. `onReadyRead` bails out while `winRaw` is set, so the closed Qt port is not read twice. The hole is `transportOpen` and `stopMotion`, which still use the closed `QSerialPort` (B1, M5).

`PortLeaveLines` writes `QSerialPortPrivate::settingsRestoredOnClose` through the private d-pointer (`Rt21Driver.cpp:173`) so close does not put the saved DCB back and raise DTR. That is load-bearing and tied to the Qt private headers. A mismatch corrupts the private object. Worth a comment at the include, and a test that close leaves DTR low.

This Linux ASan run never entered this path.

**Suggested fix:** Call `wakeSerialForClose` before every Windows `QSerialPort::close`. On a raw-write timeout, `CancelIoEx` and wait for the event before reusing `writeOv`. In `closeWinRaw`, wait for the pending read to finish, then close. Treat `winRaw` as the open transport.

### Minor

1. **Goto is one shot with a 60 ms inflight.** `pump` uses 60 ms for `Kind::Motion` (`Rt21Driver.cpp:1616`). The timeout handler then just pumps (`Rt21Driver.cpp:449`). There is no second `AP`. The disassembly says delays of 1–250 ms run with interrupts off, and `sendArmedGoto` writes the `AP` in the same turn as the heading reply. A command that lands in that window is dropped while the target can stay on screen (`noteArrival` keeps it when status is stopped and the gap is large). Retry the `AP` once, after 150 ms, if the heading has not moved.
2. **Warmup spacing is 20 ms after `R21;`** (`kPairAfterR21Ms`, `Rt21Driver.cpp:82`). That matches the utility and is inside the 25–30 ms interrupt-off window that follows a register ack. A miss restarts the warmup and the 15 s cap still fires, so this does not wedge. Leave the pair, and don't use 20 ms after a write ack. The brightness path's 150 ms is the one that matches the write.
3. **Screen saver Write sends `WW0n;`.** `ControllerColumn.cpp:367` emits register `W`, and `writeRegister` allows it (`Rt21Driver.cpp:3034`). Digit 0 stores and reloads every setting, with interrupts off and the receive buffer cleared. `docs/RT21_PROTOCOL.md` still says Meridian does not write W. The 4.14 hang is gated when `firmwareReport` contains `4.14`, and an empty report is refused in the dialog. Either stop sending `WW0` or correct the protocol note and keep the 150 ms gap, which this path does share with brightness.
4. **`finishNetwork`'s `ok` argument is ignored** (`Rt21Driver.cpp:1781`). Success is `networkGotField`, and a good echo sets that flag before any query (`Rt21Driver.cpp:2656`). A try whose queries all time out still finishes as "Network module answered." That matches the status line if the echo was real. It also hides a module that answered `OK` and then died.
5. **`networkReopenFailed` is never called.** A failed reopen goes through `done(false)` to `beginNetworkExit`, which does try to send ESC. The dead function also calls `failLink` without that ESC. Delete it so the next edit does not use it.
6. **Lead-in ESC bytes are back to back** (`Rt21Driver.cpp:1944`). The capture had about 0.7 ms between them. Two writes usually land that close. If a module needs two distinct breaks, insert the gap.
7. **Find's serial probe accepts any `BI1;` heading and does not ask for `R11;`.** The TCP probe does require a version containing `RT-21`. A DCU-1 compatible device on a COM port becomes a found rotor. Ask for the version on serial too, with a short timeout, and skip the port when it is missing.

### Nit

1. `kOpenAfterCloseMs` is unused. `kDtrLowMs` is unused on the path that actually runs (M1).
2. `startNetworkPass` logs variant `F` for the `T11;` and `variantForThisTry` has already stored `'D'` (`Rt21Driver.cpp:2975`). The success line is variant D. One name for one sequence would match the log to the capture note.
3. `preciseSleepMs` calls `timeBeginPeriod(1)` on Windows and busy-spins the rest. Even after M3 moves the long waits onto timers, don't busy-spin.
4. `applySetupGroup` / `writeConfiguration` still have a non-readonly path that reports a write successful on timeout (`Rt21Driver.cpp:455`). This alpha compiles that out. When it comes back, wait for the `06` ack.

## Tests

| Build | Result |
| --- | --- |
| g++ 13, Qt 6.4.2, RelWithDebInfo, `QT_QPA_PLATFORM=offscreen` | 7/7 passed, 153 s |
| Same flags plus `-fsanitize=address,undefined`, leaks disabled | 7/7 passed, 154 s, no sanitizer report |

`tst_setup`'s timing assertions pass because they allow a 1 ms DTR pulse (M1). The framer cases in M2 are not in `tst_core`. The Windows raw handle, `wakeSerialForClose`, and the Find-versus-`QSerialPort` race were not executed here. ThreadSanitizer was not run. The traffic-log race in M4 is visible statically.
