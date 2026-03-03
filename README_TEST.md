# SuperGreenOS UI Test And Deploy Guide

This guide covers safe testing of `html_app` changes without touching firmware binaries.

## What Gets Updated

`upload_htmlapp.sh` uploads only:

- `/fs/app.html`
- `/fs/config.json`

It does **not** flash firmware partitions (`ota_0`, `ota_1`).

## Prerequisites

- Windows PowerShell
- Git Bash installed (`C:\Program Files\Git\bin\bash.exe`)
- Node.js installed
- Python 3 installed
- `ejs-cli` installed globally:

```powershell
npm install -g ejs-cli
```

## 1. Regenerate Embedded UI Files

From PowerShell:

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc "cd /c/Sources/SuperGreenOS && ./update_htmlapp.sh config.controller.json"
```

This regenerates:

- `spiffs_fs/app.html`
- `spiffs_fs/config.json`

## 2. Local Test (No Controller, Zero Risk)

Run local mock API server:

```powershell
cd C:\Sources\SuperGreenOS
python .\mock_server.py --port 8080
```

Open:

- `http://127.0.0.1:8080/app.html`

Expected:

- Header values visible (`Firmware`, `Build`, `Wi-Fi`, `IP`)
- Global error banner works with `Retry` if server is stopped
- `Save` / `Refresh` buttons visible on writable fields

## 3. Find Real Controller IP (LAN)

PowerShell scan:

```powershell
1..254 | ForEach-Object {
  $ip = "192.168.1.$_"
  try {
    $r = Invoke-WebRequest -Uri "http://$ip/fs/config.json" -TimeoutSec 1 -ErrorAction Stop
    if ($r.StatusCode -eq 200 -and $r.Content -match '"keys"') { "FOUND $ip (200)" }
  } catch {
    if ($_.Exception.Response -and $_.Exception.Response.StatusCode.value__ -eq 401) {
      "FOUND $ip (401 auth)"
    }
  }
}
```

Example found IP: `192.168.1.104`

## 4. Backup Current UI From Real Controller

```powershell
curl.exe --fail -o app_backup.gz http://192.168.1.104/fs/app.html
curl.exe --fail -o config_backup.gz http://192.168.1.104/fs/config.json
```

## 5. Upload New UI To Real Controller

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc "cd /c/Sources/SuperGreenOS && ./upload_htmlapp.sh 192.168.1.104 ./spiffs_fs"
```

Expected upload response:

- `HTTP/1.1 200 OK`
- `@FS File uploaded successfully`

Then open:

- `http://192.168.1.104/fs/app.html`

Do hard refresh:

- `Ctrl+F5`

## 6. Rollback (If Needed)

```powershell
curl.exe --fail -X POST --upload-file config_backup.gz http://192.168.1.104/fs/config.json
curl.exe --fail -X POST --upload-file app_backup.gz http://192.168.1.104/fs/app.html
```

## Troubleshooting

- `'./upload_htmlapp.sh' is not recognized`
  - You are in CMD/PowerShell. Run scripts through Git Bash (`bash.exe -lc ...`).

- `ejs-cli: command not found`
  - Install: `npm install -g ejs-cli`

- `curl: (28) Could not connect`
  - Wrong IP or controller offline on port 80.
  - Verify:
    - `ping <ip>`
    - `Test-NetConnection <ip> -Port 80`

- `307 Temporary Redirect` with `/2.0/gui/...`
  - You uploaded to router (commonly `192.168.1.1`), not to controller.

- `Loading config failed (network error)` on localhost
  - You likely used static server only.
  - Use `python mock_server.py` to serve `/fs/config.json`, `/i`, `/s`.
