# Review A — why the network module never answers

Alpha18 through alpha23 get `<uart2>;` from the RT-21 and then no `OK`. Alpha23 variant D matches the Green Heron utility's close and reopen, IOCTL for IOCTL, and still fails. The failure is not the baud, the parity, or the modem status. It is what happens to the read after that reopen.

This note ranks the causes. "Verified" means the code or the capture shows it. "Unverified" means the bench still has to decide it.

## 1. Verified in the code, and it matches the capture: after `<uart2>;` the read is not the utility's read

The utility is a .NET `SerialPort`. It opens the port overlapped (`FILE_FLAG_OVERLAPPED`), sets the event mask `0x1FB`, and waits on `WaitCommEvent`. When a byte arrives it reads the bytes already queued. `AT*SERIAL_ECHO=0` is answered `OK` about 54 ms later. A first `ERROR 1` is resent about 60 ms later.

Alpha23 variant D does the same close (mask 0, `CLRDTR`, purge, `CloseHandle`) and the same `SetCommState` / `SetCommTimeouts` / `SetCommMask` / `SetupComm` sequence. Then it diverges in a way an IOCTL trace does not show:

- `CreateFile` is called **without** `FILE_FLAG_OVERLAPPED`.
- The only reader is a `QTimer` at 5 ms that calls synchronous `ReadFile` for **512 bytes** with `ReadIntervalTimeout = MAXDWORD`, `ReadTotalTimeoutMultiplier = MAXDWORD`, `ReadTotalTimeoutConstant = 2000`.
- That call runs on the GUI thread. With an empty buffer it blocks for up to 2000 ms. The 60 ms retry timer cannot run while it is blocked.
- `if (!ReadFile(...) || got == 0) return;` throws away a timeout that still transferred bytes. MSDN allows a timeout to return a non-zero count.

The alpha23 capture is that call:

```
41854.5  TX  AT*SERIAL_ECHO=0\n
43904.2  RX  00          (+2049.7 ms)
43905.8  TX  AT*SERIAL_ECHO=0\n
44027.2  TX  AT*SERIAL_ECHO=0\n
46066.2  RX  00          (+2039 ms from the third echo)
```

`d0 07` in `SET_TIMEOUTS` is 2000 ms. The first RX is the timeout, not a 54 ms `OK`. The second echo is 2 seconds late, not 60 ms late, because the thread was inside `ReadFile`.

Variants A and B stay on Qt's handle. Qt 6.8.3 arms an overlapped `WaitCommEvent`. `PurgeComm(PURGE_RXABORT)` completes that wait with `ERROR_OPERATION_ABORTED`. `_q_notified` maps that to `ResourceError` and returns without arming another wait. `communicationStarted` stays true, so a later `startAsyncCommunication()` is a no-op. Meridian treated the error as ignorable and left the port open. An `OK` with no wait pending is silence in an API sniffer, which only logs read completions. A and B clear the buffer with `PURGE_RXCLEAR` only (no abort) in the current tree; a DTR change can still complete the wait with an error. The error handler did not re-arm it.

The unit test never executed this path. On Linux, variant D closes and reopens `QSerialPort`, which arms the reader again. The pty mock then answers `OK`. A green test only proved the state machine.

**Unverified on the wire:** whether the module actually transmitted `OK` during those 2000 ms and the Edgeport's special-case timeout dropped it, or the module transmitted nothing. Both look the same in this capture. The synchronous read cannot tell them apart. A live overlapped reader can.

## 2. Verified as not the bytes the program matched: the interleaved `00`s

`<uart2>;` in the sniffer is `3c 00 75 00 61 00 72 00 74 00 32 00 3e 00 3b 00`. The poll reply in the same capture is `30 01 30 34 34 39 3b 00`, one trailing `00`, not a `00` after every byte. The utility's reads of the same replies are clean.

Alpha23's matcher copies printable bytes and turns every other byte into a space. Interior `00`s would make the line `< u a r t 2 >`, which is not `<uart2>`. The port cycle runs only after that match, and the capture shows the cycle. The bytes `QSerialPort` handed the parser did not contain those `00`s. They are how the monitor dumped the read buffer (a short read padded, or a one-byte completion shown as a word), not a second copy of the frame on the wire.

The lone `00` at +2000 ms is the same family: a timeout completion shown as one zero. A zero-length read is not an `OK`. A real `00` payload would not contain the letters `OK` either.

**Unverified:** some later read, on the raw handle, really returning `4f 00 4b 00`. Alpha25a strips `00` before the match, so that shape still counts as `OK`, and the log shows the raw hex either way.

## 3. Unverified, and not sufficient: the 0.1 ms gap before `AT;`

The utility waits about 470–640 ms with nothing on the wire after the last poll reply. Alpha23 sent `AT;` 0.1 ms after `R01;` came back. The controller still answered `<uart2>;`, so pass-through did start. Alpha24 added a 650 ms pause and was not run on the rotor. That pause does not create a reader. It stays in alpha25a, as a timer rather than a busy-wait inside `readyRead`, because freezing the thread that owns the port was its own bug. It is not the explanation of an RX event 2000 ms after the echo.

## 4. Ruled out, and left alone

DSR/CTS/RLSD are `0xb0` on the success and on the failure. The close/open IOCTL list and its timing match. 4800 8N1 matches. The echo string the program queues is `AT*SERIAL_ECHO=0` plus `0x0A` and nothing else. No `W0`, no `T11;`, no `T01;`.

## What the earlier builds got wrong

- An IOCTL-identical open was treated as an identical session. The utility's overlapped wait is not an IOCTL.
- The reader that had just received `<uart2>;` was thrown away and replaced with a blocking 512-byte `ReadFile` using the .NET timeout numbers. .NET uses those numbers so a read of bytes **already queued** returns at once. It does not call `ReadFile` for 512 bytes on an empty buffer.
- Qt's reaction to `ERROR_OPERATION_ABORTED` was ignored. The port stayed open and the wait stayed dead.
- The test that "proves" the echo runs on a pty reopen, which rearms Qt. It never built the Windows raw handle.
- The sniffer's `00` bytes were investigated as a wire format. The parser that accepted `<uart2>;` would have rejected them.
- Alpha24's silence before `AT;`, and the in-session gateway write, do not touch this read.

## What alpha25a changes

- Variant D still drops DTR, purges, and closes on the utility's schedule, about 520 ms after `<uart2>;`. It then opens the same `QSerialPort` again and raises DTR and RTS. `open()` arms the overlapped `WaitCommEvent` before it returns, and the echo is sent immediately after that. There is no synchronous `ReadFile` and no 2000 ms block.
- Variants A and B still hold DTR low for 195 ms on the open handle. They clear the buffer without `PURGE_RXABORT`. If Qt still reports `ResourceError`, the wait is issued again with `WaitCommEvent` on Qt's own `OVERLAPPED` (the private `startAsyncCommunication` symbol is not exported from `Qt6SerialPort.dll`). Queued abort events are delivered before the echo.
- `00` bytes are skipped when matching `<uart2>` and `OK`. Any other non-printable byte still becomes a space, so a real gap stays visible.
- During the module session every transmitted and received byte is logged as `network wire tx` or `network wire rx`, with a millisecond offset and the hex. The offset is from the echo once the echo has been sent, and from `<uart2>;` before that. A zero-length timeout is not written down as the byte `00`.
- The 650 ms quiet before `AT;` remains. It no longer busy-waits inside `readyRead`.
- The gateway write is unchanged from alpha24: only the gateway, only after a serial read on this connection, `AT*AGW=` plus a line feed, then `AT*AGW?` and `AT*GW?`. No `W0`. No address or port is pre-filled. Status stays on the lower-left line.

`networkTextIgnoresNulPadding` locks the NUL rule. The existing mock network read still has to see `ERROR 1`, the second echo, `OK`, and the echo's hex `41 54 2a 53 45 52 49 41 4c 5f 45 43 48 4f 3d 30 0a`. All seven test binaries passed.

## Build

`Meridian-0.1.0-alpha25a-win64.zip`

SHA-256: `3cc09ac39b709d3aafd2647205988800b99fc2af2a3eb41acd9f930edf371c16`

Qt 6.8.3 MinGW, 64-bit. The title bar reads `Meridian 0.1.0-alpha25a`. It was cross-compiled; it has not been double-clicked on the shack PC. If Windows reports a missing DLL, that is this package, not the rotor.

## Bench steps

1. Quit the Green Heron utility so nothing else owns COM6. Leave the RT-21 powered, on the Edgeport, at 4800.
2. Unzip the alpha25a archive into a new folder. Do not copy it over an older Meridian.
3. Run `meridian.exe`. The title must say `0.1.0-alpha25a`. If the window does not appear, stop and keep the Windows message.
4. Open settings with the gear. The status line is the lower-left one. Choose the saved COM6 rotor at 4800. Do not type an IP or a port. Connect, and wait until headings are arriving on their own.
5. Open Setup, Network, and press Read network once. The line goes amber: `Reading network (try 1 of 12)…`. Leave it until it turns green (`Network module answered. Nothing was written.`) or red. Do not turn the antenna while it is in that read.
6. The log is `logs\meridian-*.log` beside `meridian.exe`, or under `%APPDATA%\Meridian\logs` if that folder could not be written. The first line names alpha25a.
7. In that log, `network wire tx` / `network wire rx` are the bytes. A working try has `41 54 3b` (`AT;`), a receive containing `3c 75 61 72 74 32 3e 3b` (`<uart2>;`), then `41 54 2a 53 45 52 49 41 4c 5f 45 43 48 4f 3d 30 0a`, then within about 100 ms a receive containing `4f 4b` (`OK`) or `45 52 52 4f 52` (`ERROR`) and a second echo. A receive that is only `00`, about two seconds after the echo, means the new reader also saw silence. That result is different from alpha23 and the log is the evidence.
8. On success the network page fills from the rotor. The gateway box is empty until then. Do not write a gateway unless you intend to change it. Any other network write stays refused, and the reason is on the lower-left line, not in a pop-up.
