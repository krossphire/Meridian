# Review B — serial transport on Windows

Scope: how Meridian writes and reads bytes, how it closes and reopens the COM port in the middle of a network-module session, signal and slot lifetimes, buffering, timeouts, and the parser. Compared with Setup Utility 4.B12, which uses the .NET `SerialPort` / `SerialStream` on the same Edgeport (COM6, 4800 8N1, firmware 4.13.2).

Sources checked: Meridian as of alpha24 (`Rt21Driver`, `Rt21Protocol`, `SerialLines`, `TrafficLog`), Qt 6.8.3 `qserialport.cpp`, `qserialport_p.h`, and `qserialport_win.cpp` (the version the Windows release builds), and the .NET Framework `SerialStream` reference source. No capture file was in the tree. Conclusions that depend on the Device Monitoring Studio screenshots use the byte strings in the review request. Nothing here was run on the Edgeport.

Alpha24 behavior that this branch keeps: 650 ms with nothing sent before `AT;`, and the in-session gateway write `AT*AGW=` plus a line feed.

## The 0x00 bytes

Two different things are in those captures. They should not be fixed the same way.

**Trailing zeros are a capture artifact. Verified from the Qt 6.8.3 source.**

`QSerialPort` opens the port with `FILE_FLAG_OVERLAPPED`. Its read chunk is a `QByteArray` of `QSERIALPORT_BUFFERSIZE` bytes, which is **32768**, filled with `0` (`readChunkBuffer(QSERIALPORT_BUFFERSIZE, 0)` in `qserialport.cpp`). `ReadFile` is asked for that whole chunk. When the rotor has sent 9 bytes, the kernel writes 9 bytes at the front and the rest stays `0`. `completeAsyncRead` copies only `bytesTransferred` into the buffer the app reads. A monitor that dumps the requested buffer, not the transferred count, shows the payload and then zeros.

The example `01 30 00 3e 34 32 2e 39 3b 00 00` is that case. The middle `00` is real. It is the stopped flag in `01 30 00 3E` plus `42.9;`, the same frame already documented from the LAN module. The two zeros after `3b` are unused buffer. Stripping every `00` would throw away the stopped flag. A test now locks that frame.

**A `00` between every byte is not what Meridian transmits, and it is not what the parser receives. Verified from Meridian and from Qt. The monitor's exact display step was not re-run.**

`R01;` answered as `30 00 01 00 30 00 34 00 34 00 39 00 3b 00` is the UTF-16LE widening of the real packed reply `30 01 30 34 34 39 3b` (`0`, SOH, `0449;`). `<uart2>;` widened the same way is `3c 00 75 00 61 00 72 00 74 00 32 00 3e 00 3b 00`. The utility's buffer `30 01 30 34 36 34 3b` is the packed form.

Qt's read path never zero-extends. It appends `bytesTransferred` chars from that chunk. Meridian's transmit path never calls `QString::utf16()` on payload. Commands are `QByteArray` with an explicit size:

- `AT;` is `41 54 3B` (3 bytes)
- `AT*SERIAL_ECHO=0\n` is 17 bytes, `41 54 2A … 30 0A`, no `00`
- `R01;` and `R21;` are 4 bytes each

`CreateFileW` is used only for the port name (`\\.\COM6`). That is the correct wide API for a path. The bytes written afterward are 8-bit.

The parser would not accept a widened `<uart2>;`. A `00` between letters becomes a space, so the line is `< u a r t 2 >`, which is not `<uart2>`. The close/open that the capture shows is scheduled only after that match. So the bytes that reached the framer were packed. Headings on this station were already working through the same `QSerialPort` read path, which would also fail if every other byte were `00`.

So the interleaved view is a display of the same packed bytes after something widened them (a Unicode hex pane, or a `QString`, which stores Latin-1 as UTF-16). It is not a baud, parity, stop-bit, or byte-size difference, and it is not on the wire. Baud and line control were already ruled out at the IOCTL layer; the source agrees there is nothing left that would insert `00` on the wire.

**The lone `00` about every 2 seconds is the read timeout, not a character from the rotor. Verified against the timeout value and MSDN. Not re-run on the Edgeport.**

Variant D sets `ReadIntervalTimeout` and `ReadTotalTimeoutMultiplier` to `MAXDWORD` and `ReadTotalTimeoutConstant` to `2000` (`d0 07`). That is the .NET `ReadTimeout = 2000` pattern. MSDN says that read returns as soon as any byte is already buffered, or when the first byte arrives, or after 2000 ms if nothing arrives. A real `00` from the rotor would complete the read immediately, not on a 2 second cadence. The cadence is the constant timeout. The old read used a 512-byte stack buffer that was not cleared, and returned without looking at it when `ReadFile` failed or the count was 0. A monitor that still prints the buffer shows `00`. That completion is not payload. Alpha25b logs it as `raw read complete 0 bytes (timeout, not a received 00)` and does not hand it to the parser.

## Findings, by impact on the failed entry

### 1. Variant D copied the IOCTLs and then did the I/O .NET does not do

**Likely impact: high. Code difference: verified. That this is what the Edgeport actually does with the echo: unverified (no COM6 here).**

Setup Utility opens with `CreateFile(..., FILE_FLAG_OVERLAPPED)` and reads and writes through overlapped `ReadFile` / `WriteFile` on a thread-pool handle (`SerialStream` constructor and `Read` / `Write`). `QSerialPort` does the same, and that path is the one that already receives `R01;`, `R21;`, and `<uart2>;` on this Edgeport.

Alpha24 variant D closed that handle and opened another with `FILE_ATTRIBUTE_NORMAL` and no overlapped flag. It then called `WriteFile` and `ReadFile` with a null `OVERLAPPED`, on the GUI thread, for up to 512 bytes. With the .NET timeouts, that `ReadFile` does not return until a byte arrives or 2000 ms pass. The 5 ms poll timer cannot run again, the 60 ms echo retry cannot run, Cancel cannot run, and the status line cannot update.

On a USB-serial driver, a synchronous `ReadFile` that blocks inside the driver before a queued write has left the chip is a known way to get exactly this capture: setup IOCTLs match, DTR timing matches, and the read completes every 2 seconds with nothing. .NET never blocks the calling thread inside `ReadFile`. Qt never does either. Variant D did.

Alpha25b opens that second handle with `FILE_FLAG_OVERLAPPED`, writes with its own `OVERLAPPED` and waits only until that write finishes (or 300 ms), and polls the read with `WaitForSingleObject(..., 0)`. The GUI thread is not stuck in the driver. Only the transferred count is parsed. The setup IOCTLs (DCB, both `SetCommState` calls, `SETDTR`, the 2000 ms timeouts, mask `0x1FB`, `SetupComm(4096, 4096)`) are unchanged.

### 2. A failed or timed-out `ReadFile` could hide bytes the driver had already copied

**Impact: high if the Edgeport returns failure-with-data; otherwise it only hid the timeout. The discard is verified. The driver's return code is unverified.**

```text
if (!ReadFile(...) || got == 0)
    return;
```

A comm timeout is often `TRUE` and a count of 0, which this ignored. Some USB drivers instead return `FALSE` after copying the bytes they did get. Those bytes were thrown away, and the buffer was uninitialized, so a monitor could show whatever was on the stack (often `00`). Alpha25b keeps a zeroed buffer and passes on only `serialBytesTaken(..., got)`. A count of 0 stays empty. It is not turned into a one-byte `00`.

### 3. The 60 ms echo retry could not happen while the read was blocked

**Impact: medium, once any reply exists. Verified from the control flow.**

The utility sends `AT*SERIAL_ECHO=0\n`, often gets `ERROR 1` in about 47 ms, and sends the same line again about 60 ms after the first. That second one is the one that gets `OK`. Alpha24 armed a 60 ms timer and then blocked in `ReadFile` for up to 2000 ms, so the resend ran only after the read returned. The first reply, if it had been in the buffer, would still have been visible. This does not by itself explain a capture with no `OK` and no `ERROR`. It does explain missing the utility's retry once the read path is fixed. The overlapped poll lets the 60 ms timer run.

### 4. `wakeSerialForClose` existed and was never called

**Impact on this entry: low, if the captured close was already fast. Robustness: medium. Verified unused before this branch.**

`QSerialPort::close` calls `CancelIo` and then, unless `settingsRestoredOnClose` is cleared, `SetCommState` of the DCB saved at open and `SetCommTimeouts` of the saved timeouts (`qserialport_win.cpp`, Qt 6.8.3). The saved DCB is whatever the Edgeport had before Qt touched it. Windows' default is often DTR enabled, so that restore raises DTR on the way out. `CancelIo` also does not always wake a pending `WaitCommEvent`, which is why close stretched on this adapter.

The network close already did its own `CancelIoEx`, `SetCommMask(0)`, `CLRDTR`, and both purges, and it clears `settingsRestoredOnClose` before `close()`. That matches the utility's close order, so this is not why variant D's IOCTL trace matched and still got no `OK`. A normal disconnect did none of that. Alpha25b calls `wakeSerialForClose` (short timeouts, mask 0, `CancelIoEx`, purge) and clears the restore flag on every close, not only on the network cycle.

The private-header write to `settingsRestoredOnClose` is still a private API. It compiles against the Qt 6.8.3 headers this branch links. It is not a public guarantee.

### 5. While variant D held the raw handle, the QSerialPort object was closed

**Impact: medium if the map was clicked during the read. Verified. The unattended failure does not need a click.**

`transportOpen()` was "the `QSerialPort` is open". During variant D it is not; the Win32 handle is. A click called `connectEndpoint`, which closes and reopens. Esc called `stopMotion`, which wrote `;` through the closed `QSerialPort`. Alpha25b refuses both while a network read or the raw handle is active, and the status line says `Waiting for the network read to finish` (amber). The network button still cancels.

`pump()` also will not write the closed `QSerialPort` while the raw handle is the live port.

### 6. `QSerialPort::flush()` does not wait for the bytes to leave

**Impact: low for variant D, because D does not use it. Verified in Qt 6.8.3. Unverified as a cause of an A/B miss.**

The public `flush()` only starts the overlapped write and returns. The comment in Qt says it does not block. Variant A/B call it after `AT*SERIAL_ECHO=0\n` and then return to the event loop, so the write still completes. It is not a wait for shift-register empty. Variant D now waits for its own overlapped write.

### 7. The GUI thread busy-waits across the quiet gap and the DTR gap

**Impact: low for the missing `OK`. Verified. Left as it is.**

`preciseSleepMs` spins for 650 ms before `AT;` and until 520 ms after `<uart2>;`. Slots do not run during the spin, including the serial notifier. The following purge is intentional. The tests lock those windows (DTR drop 490–560 ms after `<uart2>;`, quiet at least 650 ms). Changing them to a timer would move the thing the capture says has to match. The overlapped read no longer adds a further 2 second freeze after the port is open.

### 8. Other robustness notes

- **Write result ignored. Verified, fixed.** A failed or short `WriteFile` was still counted as an echo. The log now records `raw write failed`, `raw write short`, or `raw write still pending`.
- **No `ClearCommError` on the raw handle. Unverified whether an error was latched.** A 0-byte completion now calls it and logs `queued` and `err`. That IOCTL is not part of the open sequence.
- **Timer lambdas are bound to the driver.** `QTimer::singleShot(ms, this, ...)` is cancelled if the driver is destroyed. `retire()` closes the port before the object goes away. A serial `errorOccurred` during `networkMode` is ignored, so a late error from `close()` does not drop the link mid-read. Verified from the connections.
- **`cycleSerialPort` can return without calling its completion after the 520 ms sleep** if the link was dropped during the sleep. The sleep runs on the GUI thread, so a click cannot land in the middle of it. Low. Left unchanged.
- **Replacing the handle while Qt's notifier still has a posted completion.** `close()` deletes the notifier and sets its handle to invalid before `CreateFile`. The new handle is not on Qt's completion port. A stale callback should not steal the new read. Not observed; low.
- **Do not strip `00` in the framer.** The stopped flag is a real `00`. Tested.

## What alpha25b changes

- Variant D's second open uses `FILE_FLAG_OVERLAPPED`. Read and write use overlapped I/O. The read poll does not block. Timeouts are still the utility's 2000 ms values.
- Only transferred bytes are parsed. A 0-byte read is logged and is not a received `00`.
- Normal close wakes the pending Qt read and does not restore the old DCB.
- Map clicks and Esc do not reopen or write `;` during the network read.
- Settings, Station page: **Log exact serial bytes**, on unless turned off. Also `--serial-bytes`. Each `QSerialPort` read and write is a `PORT-RX` or `PORT-TX` line with the time, the length, the text, and the hex. The raw handle logs the same way. The line is written from `readData` / `writeData`, not from a `QString`.
- Version string is `0.1.0-alpha25b`. The 650 ms quiet and the gateway write are unchanged. Nothing sends `W0…;`. No new pop-ups. The status line stays amber, green, red, or grey.

## Tests

`ctest` on Linux: `tst_core`, `tst_finder`, `tst_usb`, `tst_driver`, `tst_globe`, `tst_settings`, `tst_setup` all passed, including the pty network read (variant D close/open timing, `ERROR 1`, second echo, gateway query).

New checks in `tst_core`: echo bytes are exactly the 17-byte line feed form and not UTF-16; a 0-byte transfer is empty rather than `00`; the stopped-status frame keeps its real `00` and ignores a zero tail; widened `<uart2>;` does not match; `PORT-TX` hex has no `00` between bytes.

The Windows objects, including `Rt21Driver.cpp`, were cross-compiled with Qt 6.8.3 mingw and linked. That exe was not started on Windows.

## Zip

`Meridian-0.1.0-alpha25b-win64.zip`

SHA-256: `95722998a356e91bdd6bbc66e802e967a2855821bb09c93a3806d61c11d5304e`

Built from this branch with Qt 6.8.3 `win64_mingw`, posix mingw-w64 GCC 13. The zip contains `meridian.exe`, `meridian-mock.exe`, the Qt DLLs those imports need, the MinGW runtime that ships in that Qt kit, and `platforms/qwindows.dll`.

## Field test

1. Quit the Green Heron setup utility so it is not holding COM6. Leave the rotor on, screen saver off, firmware as it is.
2. Unzip the alpha25b build and start `meridian.exe`. The title should say `0.1.0-alpha25b`.
3. Open settings (the gear). On the Station page, **Log exact serial bytes** should be checked. Leave it checked. Do not type an address or a port.
4. Rotors page: the port should be the Edgeport COM port at 4800. Connect.
5. The lower-left line should go green when headings are coming in. Open Setup, Network, and read the network. Let it run. The line stays amber and counts tries. Do not click the map or press Esc while it is reading. Cancel is the network button again.
6. If it works, the line turns green and says the module answered and nothing was written.
7. If it does not, wait until it stops (it will try up to 12 times). The log is in the folder next to `meridian.exe`, or in `%APPDATA%\Meridian\logs` if the program folder could not be written. Settings shows the folder. Send that log.

In the log, a good echo attempt has `network raw handle overlapped`, a `PORT-TX` of 17 bytes ending `… 45 43 48 4F 3D 30 0A` (that is `AT*SERIAL_ECHO=0` and a line feed, not a wide character), and a `PORT-RX` whose text is `ERROR 1` or `OK`. A line that says `raw read complete 0 bytes` means the port timed out with no character. It does not mean the rotor sent a zero. `raw write failed` or `raw write short` means the echo did not all leave the PC.
