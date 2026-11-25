# Notify the current New Contact callsign, grid, and azimuth

Draft for Ladislav Foldyna's [QLog](https://github.com/foldynl/QLog). Written against `eebdfd55e23679fc641fe07694a65a8a4020b32b` (preparation for 0.54.2). Not opened on that repository.

## What it adds

QLog already sends UDP JSON notifications (`msgtype` `qso`, `dxspot`, `wsjtxcqspot`, `rigstatus`, and the rest). This adds `currentcontact`, sent when the New Contact callsign or grid changes, before the QSO is logged.

```json
{
  "appid": "QLog",
  "msgtype": "currentcontact",
  "time": 1647801358067,
  "logid": "{...}",
  "data": {
    "callsign": "C91RU",
    "grid": "KG54",
    "azimuth": 92.4
  }
}
```

`azimuth` is the short-path bearing QLog already computes in `NewContactWidget::updateCoordinates`, from the station profile locator to the contact. An empty callsign (the field was cleared) is sent with an empty call, an empty grid, and azimuth 0 so a listener can hide its control.

The destination is Settings → Network → Current contact, the same `IP:PORT` list as the other notifications. Nothing is sent until that list has an address. The patch does not change the default WSJT-X port.

## Where it hooks

- `NewContactWidget` emits `currentContactChanged` from `updateCoordinates` after the bearing is known, and when the callsign is cleared.
- `MainWindow` connects that signal to `NetworkNotification::currentContact`.
- `LogParam` stores `network/notif/currentcontact/addrs`.

## Apply

```bash
git checkout eebdfd55e23679fc641fe07694a65a8a4020b32b
patch -p1 < 0001-notify-current-contact.patch
```

`patch --dry-run -p1` succeeds against that commit. I have not built QLog with the patch applied.

## Not done here

- No pull request was opened on foldynl/QLog.
- The Settings line was added in `SettingsDialog.ui` as row 6 of the notification form. Worth a look in Qt Designer in case a later QLog commit moved that form.
