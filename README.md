# LanVentory Windows Agent

Skanuje hosta z systemem Windows i wysyła snapshot na backend LanVentory.
Format payloadu pasuje 1:1 do widoków w
`frontend/src/Pages/Main/Devices` (Hardware, Network, Security, Software,
Peripherals, Events, Users, SystemInfo).

## Trzy binarki + jeden instalator

| Plik                              | Język  | Co robi |
|-----------------------------------|--------|---------|
| `lanventory-agent.exe`            | C++20  | Headless skaner. Odpalany przez Task Scheduler co 60 min. ~1.5 MB statycznie zlinkowany. |
| `lanventory-configurator.exe`     | Python/Qt | GUI. Wpisanie Backend URL + Enrollment Token + enrollment. |
| `lanventory-manager.exe`          | Python/Qt | GUI status. Tail logu, „Scan now", „Re-enroll", „Open log". |
| `LanVentoryAgentSetup-x.y.z.exe`  | Inno Setup | Pakuje wszystko + Start Menu + Task Scheduler. **Ten plik trafia do operatora.** |

Headless agent jest natywnym kodem C++ rozmawiającym z Windowsem przez
WMI (COM), Registry, IP Helper, BCrypt (HMAC/SHA-256), Crypt32 (DPAPI)
i WinHTTP — zero zależności runtime'owych poza Windows SDK. GUI zostały
w PySide6, bo to mały ułamek odpaleń i Qt jest bardziej produktywne
na widoki / live tail / akcje.

## Layout

```
windowsApp/
├── agent-cpp/                 # C++ agent
│   ├── CMakeLists.txt
│   ├── include/lanventory/    # nagłówki publiczne
│   └── src/
│       ├── main.cpp           # CLI entry
│       ├── wmi.cpp / winhttp_client.cpp / crypto.cpp / dpapi.cpp
│       ├── config.cpp / fingerprint.cpp / enrollment.cpp / transport.cpp
│       ├── task_scheduler.cpp # schtasks register/unregister
│       └── scanner/{system,hardware,software,network,security,peripherals,events,users}.cpp
├── gui/                       # PySide6 GUI
│   ├── agent_io.py            # cienki shim nad lanventory-agent.exe + config/state JSON
│   ├── configurator.py
│   └── manager.py
├── installer/installer.iss    # Inno Setup 6
├── scripts/build.ps1          # CMake + PyInstaller + Inno Setup
├── requirements.txt           # tylko dla GUI
└── README.md
```

## Enrollment (bez zmian względem wersji Pythonowej)

Operator nie generuje device-id ani sekretu ręcznie. Konfigurator zbiera
fingerprint hosta (TPM EK → SHA-256, MAC-i, ProcessorID, serial płyty,
hostname) i strzela w `POST /devices/agent/enroll` ze wspólnym dla floty
tokenem. Backend albo dopina do istniejącego rekordu (TPM=100, CPU=50,
MAC overlap, baseboard serial=30, hostname=10; próg 50), albo tworzy
nowy device w grupie `Computers / Auto-enrolled`. Wrócony sekret HMAC
ląduje DPAPI-zaszyfrowany w `state.json`.

## Wymagania backendu

`AGENT_ENROLLMENT_TOKEN` w env (>= 16 znaków):

```bash
openssl rand -hex 32
```

## Wymagania build hosta

- **Visual Studio 2022 Build Tools** z workloadem "Desktop development
  with C++" (MSVC v143, Windows 10/11 SDK).
- **CMake 3.20+** na PATH.
- **Python 3.10+** na PATH (do PyInstallera dla GUI).
- **Inno Setup 6** — domyślnie szukany w
  `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`.

## Build

```powershell
cd D:\Projects\WebDev\LanVentory\windowsApp
.\scripts\build.ps1 -Clean
```

Pipeline:

1. CMake configure (`-A x64`) + MSBuild Release → `dist\lanventory-agent.exe`.
   FetchContent pulluje `nlohmann/json v3.11.3` automatycznie przy
   pierwszym buildzie.
2. PyInstaller × 2 → `dist\lanventory-{configurator,manager}.exe`.
3. ISCC kompiluje `installer\installer.iss` → `installer\Output\LanVentoryAgentSetup-0.1.0.exe`.

Flagi:
- `-SkipInstaller` — tylko binarki, bez Inno Setup.
- `-Config Debug` — debug build agenta (do gdb/Visual Studio debuggera).
- `-Cmake D:\cmake\bin\cmake.exe`, `-Iscc D:\path\ISCC.exe` — gdy
  narzędzia w nietypowych ścieżkach.

## Skąd pobrać instalator

Buduje go GitHub Actions ([windows-agent.yml](../.github/workflows/windows-agent.yml)).
Trzy ścieżki:

- **Wydanie (stable)** — Release na GitHubie, attachment
  `LanVentoryAgentSetup-x.y.z.exe`. Tworzony automatycznie z każdego
  pushu tagu `vX.Y.Z`. Publiczny URL, bez logowania:
  `https://github.com/<org>/<repo>/releases/latest`
- **Najnowszy commit z mastera** — *Actions → Windows Agent → najnowszy
  run → Artifacts → LanVentoryAgentSetup*. Wymaga konta GH z dostępem
  do repo; retencja 30 dni.
- **Lokalny build** — gdy potrzebujesz natychmiastowej iteracji:
  `windowsApp\scripts\build.ps1 -Clean` (sekcja "Build" niżej).

## Dwa smaki instalatora

Build wybiera smak automatycznie zależnie od env vars:

### A) Zero-config (baked) — pełna automatyzacja, **rekomendowane**

Jeśli build dostaje `LV_BACKEND_URL` + `LV_ENROLLMENT_TOKEN` w env (CI:
GitHub Secrets), wpieka je do instalatora. Operator robi:

1. Double-click `LanVentoryAgentSetup-x.y.z.exe`
2. UAC → Next → Install → Finish

**Koniec.** Konfigurator się nie odpala. Inno Setup `[Run]` w tle:
- rejestruje zadanie SYSTEM,
- woła `lanventory-agent.exe --enroll-only`,
- po pierwszym czytaniu pliku agent auto-szyfruje plaintext token do
  DPAPI w miejscu (`config.json` na dysku ma już chroniony format).

**Konsekwencje bezpieczeństwa**: instalator zawiera token enrollment.
Każdy z dostępem do `.exe` może go wyciągnąć (raczej trywialnie —
plaintext JSON w środku PE). To akceptowalne ryzyko bo:

- Token można rotować bez przebudowy — wystarczy zmiana
  `AGENT_ENROLLMENT_TOKEN` na backendzie, stare instalatory przestają
  działać przy enrollu.
- Już enrolled hosts mają indywidualne sekrety per-device (HMAC), token
  bootstrap nie daje im dostępu.
- Standardowa praktyka dla agentów SaaS (Datadog, CrowdStrike, SentinelOne).

Setup GitHub Secrets jednorazowo:

```
Settings → Secrets and variables → Actions → New repository secret
  LV_BACKEND_URL      = https://lanventory.example.com
  LV_ENROLLMENT_TOKEN = <output of `openssl rand -hex 32`>
```

Re-installacja: `config.json` ma flagę `onlyifdoesntexist`, więc
ponowne uruchomienie instalatora **nie nadpisze** już enrolled hosta.
Żeby wymusić re-enrollment, najpierw odinstaluj (z usunięciem state).

### B) Generic — operator wpisuje wartości w konfiguratorze

Build bez tych env vars → instalator jest uniwersalny, każdy operator
wpisuje Backend URL + token sam:

1. Double-click → UAC → Install
2. Na Finish: checkbox „Launch LanVentory configuration now"
3. W konfiguratorze: URL + Token → Save & enroll.

Ten sam binary dla wielu deploymentów, ale dodatkowy krok na każdym hoście.

## Instalacja na hoście

1. Skopiuj `LanVentoryAgentSetup-x.y.z.exe` na hosta.
2. Dwa kliki → UAC → Next / Next / Install.
3. Pod koniec instalacji Inno Setup wywołuje
   `lanventory-agent.exe --register-task --interval 60` (rejestracja
   zadania SYSTEM, BootTrigger + co 60 min).
4. Na stronie Finish zostaw zaznaczone „Launch LanVentory configuration
   now" → otwiera się konfigurator.
5. W konfiguratorze: Backend URL + Enrollment Token → „Save & enroll".

## Codzienna obsługa

Menu Start → **LanVentory Agent Status** → manager: status, tail logu,
„Scan now" (`schtasks /Run`), „Re-enroll"
(`lanventory-agent.exe --enroll-only --force-enroll`).

## Uninstall

Add or Remove Programs → LanVentory Agent. Inno Setup zapyta czy usunąć
też `%ProgramData%\LanVentory` (domyślnie No — re-instalacja odzyska
to samo urządzenie).

## CLI agenta

```
lanventory-agent.exe --once                # jeden skan + exit (Task Scheduler)
lanventory-agent.exe --watch               # pętla z interval_minutes
lanventory-agent.exe --dry-run             # wypisz payload na stdout
lanventory-agent.exe --enroll-only         # tylko enrollment
lanventory-agent.exe --force-enroll        # discard state.json + enroll
lanventory-agent.exe --encrypt-stdin       # stdin -> dpapi:<base64> (configurator)
lanventory-agent.exe --register-task --interval 60   # Inno Setup post-install
lanventory-agent.exe --unregister-task     # Inno Setup uninstall
lanventory-agent.exe --sections hardware,security,events
```

## Pliki na hoście

| Ścieżka | Co tam jest |
|---------|-------------|
| `C:\Program Files\LanVentory\agent\lanventory-*.exe` | Trzy binarki |
| `C:\ProgramData\LanVentory\agent\config.json` | Backend URL + token (DPAPI), ACL: SYSTEM + Administrators |
| `C:\ProgramData\LanVentory\agent\state.json`  | device_id + sekret (DPAPI), po enrollu |
| `C:\ProgramData\LanVentory\agent\agent.log`   | Log skaner + enroll |

DPAPI scope = `LocalMachine` → skopiowanie pliku na inny host nic nie da.

## Per-scan HMAC

| Nagłówek           | Wartość                                                                 |
|--------------------|-------------------------------------------------------------------------|
| `X-Device-Id`      | UUID urządzenia                                                         |
| `X-Timestamp`      | `YYYY-MM-DDTHH:MM:SS.sssZ` (drift okno: 5 min)                          |
| `X-Nonce`          | 16 bajtów hex (losowe; `BCryptGenRandom`)                                |
| `X-Signature`      | `HMAC_SHA256(sha256(secret), "${timestamp}|${nonce}|${rawBody}")` (hex) |
| `X-Idempotency-Key`| równe `X-Nonce`                                                          |

Walidacja w [backend/src/guards/agentGuard.guard.ts](../backend/src/guards/agentGuard.guard.ts).

## Co zostało wyrzucone z poprzedniej wersji

- Cały `agent/` w Pythonie (8 skanerów + transport + enrollment +
  config + fingerprint + installer_core). Zastąpione przez `agent-cpp/`.
- Zależność od pywin32 / psutil / requests w runtime agenta (GUI dalej
  korzystają z PySide6).

## Co zostało celowo niedokończone w v1 C++

- `software.appx_packages` i `software.windows_features` — wymagałyby
  COM PackageManager + DISM API. Frontend już radzi sobie z pustymi
  listami.
- `network.connections` (przypisanie procesów) i
  `network.firewall_rules` — wymagałyby GetTcpTable2 + MSFT_NetFirewallRule
  parsingu. Frontend pokazuje puste tabele.
- `security.bitlocker.CapacityGB` — wymaga dodatkowego query
  `Win32_LogicalDisk` per volume. Frontend pokazuje 0 GB.

Wszystkie tabele na froncie obsługują puste dane bez crasha (`NoData`
panel lub pusty grid).
