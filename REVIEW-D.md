# Review D — why the module answers the utility and stays silent for Meridian

Compared, from port open through `AT;` and across retries:

- `captures/2026-11-18-gh-utility-ok-timeline.txt` (Setup Utility 4.B12, two successes)
- `captures/2026-11-18-meridian-25a-fail-timeline.txt` (alpha25a, twelve silent tries; the 25b log is the same shape)
- `captures/2026-11-15-gh-utility-ok-cap2-timeline.txt` (older utility success)
- FILETIME in the two `.dmslog8` files, via `captures/parse.py`

The reopen that follows `<uart2>;`, and the first `AT*SERIAL_ECHO=0` plus LF, already match. Alpha25a received `<uart2>;` and then no `ERROR` and no `OK`. The pending read was cancelled at ESC. Reviews A, B, and C blamed the host read path. That is ruled out: the module sent no byte.

Wall clock, UTC, from the sniffer FILETIME: Meridian runs 2026-11-18 23:53:47 through 2026-11-19 00:00:05. The utility runs 2026-11-19 01:51:04 through 01:53:10. The gap is about 1 hour 51 minutes. The module's pass-through window had already expired before the utility opened the port. "Minutes earlier" is not what the captures show.

## Diff, open through `AT;`

Times are milliseconds from the start of that capture.

| What | Utility, 18 Nov, first success | Utility, second success (after `AT*RESET=1`) | Utility, 15 Nov | Meridian alpha25a, try 1 |
| --- | --- | --- | --- | --- |
| Open lines | SET_RTS, CLR_DTR 3 ms later, DTR low 20 ms, then DTR control + RTS control | Same pulse on the reopen at 87737 (DTR low 22 ms) | Same pulse | CLR_RTS, SET_DTR. No CLR_DTR until 518 ms after `<uart2>;` |
| Steady handflow before traffic | `01 00 00 00 40 …`, Xon/Xoff limits `0x200`, chars Eof/Evt `0x1A`, Xon `0x11`, Xoff `0x13` | same | same | same `01 … 40` by the first `BI1;` |
| Timeouts on that handle | MAXDWORD / MAXDWORD / 2000 | same | same | MAXDWORD / 0 / 0 (return at once, including 0 bytes) |
| Wait mask | `0x1FB` | same | same | `0x01` (EV_RXCHAR) |
| Queue | SetupComm 4096/4096 | same | same | no SET_QUEUE_SIZE |
| Lead-in | `T11;` at 7411, `R01;` 0.2 ms later with no reply, ESC at 7622.6 and 7623.3, `;` at 9134 (1511 ms later) | none. One session-end ESC at 87570, then the reopen, then `R21;`/`R01;` only | `T11;`, immediate `R01;` with no reply, ESC pair, `;` 1506 ms later | no ESC and no `;` before the failed echo |
| `WW02;` | once, after `RW1;` read 2, ack `06 W;` | not between the reset and the second `AT;` | `WW01;` that day, because W read back 1 | never |
| Register sweep | R11, RA–RZ skipping RC, then `R[1;` with no reply for 1204 ms | not repeated | present | R11…R[, plus `BI1;`, `R41;`, `RC1;` (reply `C` 65280). `R[1;` at 69030 has no reply for 1197 ms |
| Polls in the last few seconds | `R21;` then `R01;` ~20 ms after the status reply; next `R21;` ~210 ms after the pot reply | same pair only | same pair only | `R01;` then `R21;`, about 480 ms apart |
| Quiet before `AT;` | 412 ms after the last `R21;` reply | 704 ms after the last `R01;` reply | polls, then `AT;` with no glued `R01;` | 648 ms |
| `R01;` glued to `AT;` | yes, 0.4 ms later, no reply of its own | no | no | no |
| `<uart2>;` to first echo | 736 ms. DTR low 519 ms after the reply ends | 734 ms. First echo answered `OK` in 63 ms | echo answered `OK` in 53 ms | 726 ms. Then a second echo at +94 ms and a third at +95 ms. No RX |
| Tries | one entry, then a later entry after reset | that later entry | one entry | 12 entries, 8.0–8.6 s apart, then another 12 |

Host-only columns (timeouts, wait mask, queue size, how reads are posted) cannot make the module stay silent. They are listed because the open was compared in full. The device-visible differences are the DTR pulse, the lead-in bytes, the extra pre-`AT;` commands, the poll shape, and the retry spacing.

## The interleaved `00`s

`captures/tl2.py` stores each completed read, strips trailing `00` from the sniffer's fixed data slot, and turns an empty completion into a printed `00`. A real `0x00` and an empty completion look the same in the timeline. They are not the same on the wire.

The utility's `R21;` reply is `01 30 00 3e 35 30 2e 31 3b`. The `00` is the status byte. There is no `00` between the ASCII digits.

Meridian's first heading is printed `34 00 39 00 2e 00 35 00 3b 00`. That is `49.5;` plus an empty completion after every byte. The same frame also arrives packed, `01 30 00 3e 34 39 2e 35 3b 00 00`: the real status `00`, then two empty completions. `<uart2>;` is `3c 75 61 72 74 32 3e 3b 00 00`, the eight wire bytes plus two empty completions. The utility's `<uart2>;` has no trailing `00`.

So the extra `00`s are Qt completing reads of zero bytes (its timeouts return immediately). They are not bytes the controller sent, and they are not why the module was silent. After `<uart2>;` the meridian timeline has no `ERROR`, no `OK`, and no `AT*HWTYPE`.

## Ranked causes

1. **No DTR-low pulse while the controller is still in normal mode.** Every utility open, including the reopen that precedes the second success, does SET_RTS then CLR_DTR for about 20 ms, then enables DTR. Meridian holds DTR high from the Qt open until 520 ms after `<uart2>;`. This is the only device-visible open difference that is present on all three successes and absent on all twelve failures. Alpha25d drops DTR for 20 ms, RTS held on, at the start of the network read.

2. **No cold-entry lead-in.** The first 18 November success and the 15 November success both send ESC, ESC, then `;` about 1.5 s later, after a `T11;` that gets no `R01;` reply. The second 18 November success does not: it is a reopen of a session that already spoke `AT*` and ended with one ESC. The lead-in is part of a cold entry, and it is not required once the utility has already had the port. Meridian's first ESC was only after a failed echo. Alpha25d sends ESC ESC and `;`, and does not send `T11;`, because `T11;` wedges the controller until ESC.

3. **Retries every 8 s after a real `<uart2>;`.** This does not explain try 1, and it does not explain the utility working almost two hours later. It does explain tries 2–12: each one entered pass-through and the next `AT;` came before the module's ~31 s drop. Alpha25d waits so the next `AT;` is at least 35 s after an `AT;` that did get `<uart2>;`. A try that never got `<uart2>;` starts again as soon as a heading is back.

4. **Bytes the utility does not send before `AT;`.** `BI1;`, `R41;`, and `RC1;` are in the meridian capture and in neither utility success. `R[1;` is in both and hangs ~1.2 s with no reply, so it is not the difference. These extra commands are from the connect and the setup sweep, which finished about 5 s before try 1. Tries 2–12 no longer send them and still get silence, so they are not sufficient. The network-read path in alpha25d sends none of them.

5. **Poll shape.** The utility's pair is `R21;` then `R01;` close together, then ~210 ms. Meridian alternates from `R01;` at ~480 ms. The quiet gap before `AT;` already sits inside the success band (648 ms, against 412 and 704). Alpha25d uses the utility's pair and keeps the short 3 s / 5 s stretch.

6. **Three echoes, 94 ms apart, into silence.** The first echo already matches the utility's bytes and the ~730 ms uart2-to-echo time, and the wire was idle for that 94 ms. The extra writes did not eat a reply that had already been sent. `ERROR 1` and `OK` arrive in 47–63 ms when the entry worked. Alpha25d sends the echo once, waits 700 ms, and resends only after `ERROR 1`.

Rejected:

- **The host read path, and the printed `00`s.** `<uart2>;` was received. Nothing followed it. The `00`s are empty completions. The overlapped reader from review B is kept on the reopened handle so a real `OK` is collected without blocking the GUI. It is how the reply is received. It is not the fix for the silence.
- **`R01;` glued to `AT;`.** Only the first of the three successes does it, and that `R01;` has no reply. Not copied.
- **`WW02;`.** It reloads the screen saver (W read back 2 that night, 1 on 15 November). It is absent between the reset and the second success. Not sent. Nothing starting with `W0` is sent.
- **Missing `AT*AGW?`.** The utility's query list skips it. The module is already answering by then. Meridian still asks, because the gateway write compares AGW.

## What alpha25d changes

The network read, on a USB or serial connection only:

1. DTR low 20 ms, RTS on, 4800 8N1, no flow control, queue 4096. Qt's timeouts and wait mask stay, so the GUI reader does not block.
2. ESC ESC, then `;` at 1510 ms. No `T11;`.
3. `R21;` / `R01;` for 3 s (5 s on a retry), in the utility's order and spacing, then 650 ms quiet, then `AT;`.
4. The same close and reopen as the utility, always. Overlapped read on that handle. One echo. A second echo only after `ERROR 1`.
5. If `<uart2>;` was seen and the echo got no OK, the next `AT;` waits out 35 s. Twelve tries. No pop-ups.
6. Find's network scan tries TCP 6665, then 6555. The utility's reset value is still 6555, and that is still what an empty recommendation returns.

## Bench test

Quit the setup utility so it is not holding COM6. Unzip `Meridian-0.1.0-alpha25e-win64.zip`. The title bar should read `Meridian 0.1.0-alpha25e`. The lead-in below is unchanged. Before `AT;` the log should also show `network bridge read RW1;`, the value that came back, `network bridge write WW0` plus that same value, `network bridge ack <06>W;`, and `network bridge reread`. A saved IP port of 6555 is stored as 6665 when Find saves a hit and that IP link is down. Find stops at the first port that answers, and a serial connect cancels the 15 s IP reconnect. Connect the Edgeport on COM6 at 4800 and wait until headings are moving. Read the network once. Do not click the map.

The log is beside the exe, or under `%APPDATA%\Meridian\logs`. A good try has, in order: `network open lines: DTR low 20 ms`, `network lead-in ESC ESC`, `network lead-in ;`, the warmup `R21;` and `R01;`, `network bridge read RW1;`, `network bridge value` with the digit that came back, `network bridge write WW0` plus that same digit, `network bridge ack <06>W;`, `network bridge reread`, `network pre-AT quiet` near 650 ms, `AT;`, `<uart2>;`, `variant D`, `network raw handle overlapped`, one 17-byte echo ending `30 0a`, then `ERROR 1` or `OK` within about 100 ms, then the `AT*KEY?` lines, then ESC. The serial-port read-back should be 6665. The log should contain no `W0`, no `T11`, and no second `AT;` inside 35 s.

If the echo is silent, the log should show one echo, `network echo no reply in 700 ms, not resending into silence`, ESC, and the next `AT;` about 35 s later.
