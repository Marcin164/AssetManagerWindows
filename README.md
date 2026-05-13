# LanVentory Windows Agent

Skanuje hosta z systemem Windows i wysyła snapshot na backend LanVentory.
Format payloadu pasuje 1:1 do widoków w
`frontend/src/Pages/Main/Devices` (Hardware, Network, Security, Software,
Peripherals, Events, Users, SystemInfo).

Trzymane prosto: **jeden binary, jeden instalator, jedna ścieżka deployu**.

## Model deployu

```
   GitHub Releases               backend Docker image
   (lanventory-agent.exe in .iss) ─────► /app/assets/LanVentoryAgentSetup.exe
                                              │
                                              ▼
   Frontend "Settings > Windows Agent"     acme.lanventory.io
   (admin kopiuje snippet)                /devices/agent/installer
                                              + /devices/agent/setup-info
                                              │
                                              ▼
                                          host Windows
```

1. **CI builduje generic installer** — push tagu `v0.1.0` → GitHub Actions
   → Releases attachment `LanVentoryAgentSetup-x.y.z.exe`. Bez sekretów.
2. **Backend Docker image** przy buildzie ściąga ten plik
   (`--build-arg AGENT_VERSION=v0.1.0`) i wkłada do `/app/assets/`.
3. **Każda instancja** (cloud lub on-prem) serwuje swój `.exe` pod
   `https://<tenant>/devices/agent/installer` — ten sam plik dla wszystkich.
4. **Admin tenanta** w **Settings → Windows Agent** widzi swój Backend URL
   + swój `AGENT_ENROLLMENT_TOKEN` + gotowy PowerShell snippet.
5. **Operator hosta** wkleja snippet w elevated PowerShell:
   `Invoke-WebRequest ...` + `setup.exe /SILENT /BACKENDURL=... /TOKEN=...`.
   Instalator robi wszystko cicho, host pojawia się w UI po ~30 s.

## Co siedzi w środku

```
windowsApp/
├── agent/                     # Python agent
│   ├── main.py                # CLI entry
│   ├── config.py              # config.json + state.json + DPAPI
│   ├── dpapi.py               # ctypes DPAPI wrapper (no pywin32)
│   ├── transport.py           # HMAC POST /devices/agent/data
│   ├── enrollment.py          # POST /devices/agent/enroll
│   ├── fingerprint.py         # TPM/MAC/CPU/serial collector
│   ├── installer_core.py      # schtasks register/unregister
│   └── scanner/               # 8 sekcji: system/hardware/software/...
├── installer/installer.iss    # Inno Setup -- CLI args flow only
├── scripts/build.ps1          # PyInstaller + Inno Setup
├── requirements.txt
└── README.md
```

## CLI agenta

```
lanventory-agent.exe --once                 # jeden skan (Task Scheduler)
lanventory-agent.exe --watch                # pętla
lanventory-agent.exe --dry-run              # wypisz payload, nie wysyłaj
lanventory-agent.exe --enroll-only          # tylko enrollment
lanventory-agent.exe --force-enroll         # discard state + enroll
lanventory-agent.exe --register-task        # post-install hook (Inno Setup)
lanventory-agent.exe --unregister-task      # uninstall hook
```

## Pliki na hoście

| Ścieżka | Co tam jest |
|---------|-------------|
| `C:\Program Files\LanVentory\agent\lanventory-agent.exe` | Binarka |
| `C:\ProgramData\LanVentory\agent\config.json` | Backend URL + token (DPAPI po pierwszym czytaniu) |
| `C:\ProgramData\LanVentory\agent\state.json`  | device_id + sekret (DPAPI) |
| `C:\ProgramData\LanVentory\agent\agent.log`   | Log skaner + enroll |

## Build (lokalny)

Wymagania:
- Python 3.10+ na PATH
- [Inno Setup 6](https://jrsoftware.org/isinfo.php) w standardowej ścieżce

```powershell
cd D:\Projects\WebDev\LanVentory\windowsApp
.\scripts\build.ps1 -Clean
```

Wynik: `installer\Output\LanVentoryAgentSetup-0.1.0.exe`.

## Build (CI)

Push tagu `vX.Y.Z` → workflow `.github/workflows/windows-agent.yml` zrobi
build na `windows-latest` i wrzuci `.exe` jako asset Release'a.

## Bezpieczeństwo

- **HMAC per scan**: `HMAC_SHA256(sha256(secret), "${ts}|${nonce}|${body}")`,
  walidacja w `backend/src/guards/agentGuard.guard.ts`.
- **DPAPI LocalMachine**: token w `config.json` i sekret w `state.json`
  zaszyfrowane — skopiowanie pliku na inny host nic nie da.
- **ACL**: oba pliki tylko SYSTEM + Administrators (Inno Setup `[Dirs]`
  `Permissions: admins-full system-full`).
- **Enrollment token rotation**: zmiana `AGENT_ENROLLMENT_TOKEN` w env
  backendu + restart. Stare instalatory padają na enrollu, już enrolled
  hosty jadą dalej (mają per-device HMAC secrets).
