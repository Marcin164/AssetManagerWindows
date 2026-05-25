; Inno Setup script for the LanVentory Windows agent.
;
; One install flow: the operator runs the installer with /BACKENDURL=...
; and /TOKEN=... params (from the admin UI snippet). Installer copies
; the agent, registers a SYSTEM scheduled task, and performs the initial
; enrollment in the background.

#define MyAppName       "LanVentory Agent"
#define MyAppVersion    "0.1.0"
#define MyAppPublisher  "LanVentory"
#define MyAppURL        "https://lanventory.example.com"
#define MyAppExeName    "lanventory-agent.exe"

[Setup]
AppId={{8D5C8A66-2E2C-4F87-9F9B-1B5C6D3B41A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\LanVentory\agent
DefaultGroupName=LanVentory Agent
DisableProgramGroupPage=yes
DisableDirPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputBaseFilename=LanVentoryAgentSetup-{#MyAppVersion}
OutputDir=Output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Dirs]
Name: "{commonappdata}\LanVentory\agent"; \
    Permissions: admins-full system-full; Flags: uninsneveruninstall

[Files]
Source: "..\dist\{#MyAppExeName}"; DestDir: "{app}"; \
    Flags: ignoreversion restartreplace uninsrestartdelete

[Icons]
; Start Menu folder gives the operator something visible after install.
; The agent runs headless via Task Scheduler -- these shortcuts are just
; for operators to inspect state and uninstall cleanly.
Name: "{group}\View agent log"; \
    Filename: "{win}\system32\notepad.exe"; \
    Parameters: """{commonappdata}\LanVentory\agent\agent.log"""; \
    IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\Open agent data folder"; \
    Filename: "{commonappdata}\LanVentory\agent"; \
    IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\Run a scan now"; \
    Filename: "schtasks.exe"; Parameters: "/Run /TN LanVentoryAgent"; \
    IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall LanVentory Agent"; \
    Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--register-task --interval 60"; \
    StatusMsg: "Registering scheduled task..."; Flags: runhidden waituntilterminated

Filename: "{app}\{#MyAppExeName}"; Parameters: "--enroll-only"; \
    StatusMsg: "Enrolling with backend..."; \
    Flags: runhidden waituntilterminated; \
    Check: ConfigPresent

[UninstallRun]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--unregister-task"; \
    Flags: runhidden waituntilterminated

[Code]
const
  ConfigFile = '{commonappdata}\LanVentory\agent\config.json';

function ConfigPath: String;
begin
  Result := ExpandConstant(ConfigFile);
end;

function GetCmdParam(const Name: String): String;
var
  I: Integer;
  S, Prefix: String;
begin
  Result := '';
  Prefix := '/' + Uppercase(Name) + '=';
  for I := 1 to ParamCount do begin
    S := ParamStr(I);
    if (Pos(Prefix, Uppercase(S)) = 1) then begin
      Result := Copy(S, Length(Prefix) + 1, MaxInt);
      Exit;
    end;
  end;
end;

procedure WriteCliConfig(const BackendUrl, Token: String);
var
  Lines: TArrayOfString;
begin
  ForceDirectories(ExpandConstant('{commonappdata}\LanVentory\agent'));
  SetArrayLength(Lines, 8);
  Lines[0] := '{';
  Lines[1] := '  "backend_url": "' + BackendUrl + '",';
  Lines[2] := '  "enrollment_token": "' + Token + '",';
  Lines[3] := '  "interval_minutes": 60,';
  Lines[4] := '  "verify_tls": true,';
  Lines[5] := '  "ca_bundle": null,';
  Lines[6] := '  "log_path": "C:\\ProgramData\\LanVentory\\agent\\agent.log"';
  Lines[7] := '}';
  SaveStringsToUTF8File(ConfigPath, Lines, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  BackendUrl, Token: String;
begin
  if CurStep = ssPostInstall then begin
    BackendUrl := GetCmdParam('BACKENDURL');
    Token      := GetCmdParam('TOKEN');
    if (BackendUrl <> '') and (Token <> '') and (not FileExists(ConfigPath)) then begin
      WriteCliConfig(BackendUrl, Token);
    end;
  end;

  // After everything's wired up: if there's still no config (user
  // double-clicked .exe without /BACKENDURL= /TOKEN=), tell them where
  // to get those values. The agent is installed and scheduled but
  // cannot do anything useful until config.json exists.
  if CurStep = ssDone then begin
    if not FileExists(ConfigPath) then begin
      MsgBox(
        'LanVentory Agent installed -- but NOT YET configured.' + #13#10 + #13#10 +
        'To finish setup:' + #13#10 +
        '  1. Open your LanVentory web UI' + #13#10 +
        '  2. Go to Settings -> Windows Agent' + #13#10 +
        '  3. Copy the PowerShell snippet shown there' + #13#10 +
        '  4. Run it on this host in an elevated PowerShell' + #13#10 + #13#10 +
        'The agent will then enroll automatically and start sending scans.',
        mbInformation, MB_OK);
    end;
  end;
end;

function ConfigPresent: Boolean;
begin
  Result := FileExists(ConfigPath);
end;

function InitializeUninstall(): Boolean;
var
  Confirm: Integer;
begin
  Result := True;
  Confirm := MsgBox(
    'Also remove the agent state (device id + enrollment secret) under ' +
    ExpandConstant('{commonappdata}\LanVentory') + ' ?' + #13#10 + #13#10 +
    'Click No to keep the state -- re-installing later will resume scanning ' +
    'against the same device record.',
    mbConfirmation, MB_YESNO);
  if Confirm = IDYES then
    DelTree(ExpandConstant('{commonappdata}\LanVentory'), True, True, True);
end;
