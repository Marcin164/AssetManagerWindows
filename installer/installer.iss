; Inno Setup script for the LanVentory Windows agent.
;
; Compile with:  iscc.exe installer\installer.iss
; (or via scripts\build.ps1 which handles the full pipeline incl. baked
; config and Python GUI builds).
;
; Two build flavours, controlled by whether installer\baked\config.json
; exists at compile time:
;
;   - baked:   build.ps1 produces baked\config.json from LV_BACKEND_URL +
;              LV_ENROLLMENT_TOKEN env vars. The installer ships that
;              file, auto-enrolls during install, never shows the
;              configurator. This is the GitHub-Actions Release flow.
;
;   - generic: no baked\config.json -> the installer just deploys binaries
;              and the operator runs the configurator post-install. This
;              is the dev-laptop flow / fallback for unconfigured CI.

#define MyAppName       "LanVentory Agent"
#define MyAppVersion    "0.1.0"
#define MyAppPublisher  "LanVentory"
#define MyAppURL        "https://lanventory.example.com"
#define MyAppExeName    "lanventory-agent.exe"
#define MyAppConfigExe  "lanventory-configurator.exe"
#define MyAppManagerExe "lanventory-manager.exe"

#define BakedConfigPath "..\installer\baked\config.json"
#if FileExists(AddBackslash(SourcePath) + BakedConfigPath)
  #define HasBakedConfig
#endif

[Setup]
AppId={{8D5C8A66-2E2C-4F87-9F9B-1B5C6D3B41A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\LanVentory\agent
DefaultGroupName=LanVentory
DisableProgramGroupPage=yes
DisableDirPage=auto
UninstallDisplayIcon={app}\{#MyAppManagerExe}
OutputBaseFilename=LanVentoryAgentSetup-{#MyAppVersion}
OutputDir=Output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startmenu"; Description: "Create Start Menu shortcuts"; GroupDescription: "Additional shortcuts:"; Flags: checkablealone
Name: "desktop";   Description: "Create a desktop shortcut for the manager"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Dirs]
; Pre-create %ProgramData%\LanVentory\agent with SYSTEM+Administrators
; ACL so the baked config.json lands in a locked-down dir from the
; start. ``uninsneveruninstall`` keeps the dir on uninstall unless the
; user opts in (see [Code] InitializeUninstall).
Name: "{commonappdata}\LanVentory\agent"; \
    Permissions: admins-full system-full; Flags: uninsneveruninstall

[Files]
; ``restartreplace`` schedules a reboot replacement if the file is
; locked (rare -- agent runs briefly from Task Scheduler).
Source: "..\dist\{#MyAppExeName}";     DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\dist\{#MyAppConfigExe}";   DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\dist\{#MyAppManagerExe}";  DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete

#ifdef HasBakedConfig
; Baked deployment: drop the pre-filled config straight into
; %ProgramData%. Agent's first read upgrades the plaintext token to
; DPAPI in place, so the on-disk copy ends up identically protected to
; what the configurator would have produced.
;
; ``onlyifdoesntexist`` is critical -- a re-install must NOT clobber an
; already-enrolled host's config (which holds a DPAPI-encrypted token).
Source: "..\installer\baked\config.json"; \
    DestDir: "{commonappdata}\LanVentory\agent"; \
    DestName: "config.json"; \
    Flags: onlyifdoesntexist uninsneveruninstall
#endif

[Icons]
Name: "{group}\LanVentory Agent Configuration"; Filename: "{app}\{#MyAppConfigExe}";  Tasks: startmenu
Name: "{group}\LanVentory Agent Status";        Filename: "{app}\{#MyAppManagerExe}"; Tasks: startmenu
Name: "{group}\Uninstall LanVentory Agent";     Filename: "{uninstallexe}";           Tasks: startmenu
Name: "{autodesktop}\LanVentory Agent";         Filename: "{app}\{#MyAppManagerExe}"; Tasks: desktop

[Run]
; Register the SYSTEM scheduled task.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--register-task --interval 60"; \
    StatusMsg: "Registering scheduled task..."; Flags: runhidden waituntilterminated

#ifdef HasBakedConfig
; Baked flavour: auto-enroll using the pre-filled config. We don't fail
; the install on enrollment failure -- the scheduled task will retry
; every 60 minutes once the backend is reachable.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--enroll-only"; \
    StatusMsg: "Enrolling with backend..."; \
    Flags: runhidden waituntilterminated runascurrentuser
#else
; Generic flavour: pop the configurator on Finish so the operator can
; fill backend URL + token.
Filename: "{app}\{#MyAppConfigExe}"; \
    Description: "Launch LanVentory configuration now"; \
    Flags: postinstall nowait skipifsilent
#endif

[UninstallRun]
; Order matters -- remove the scheduled task BEFORE [Files] deletion.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--unregister-task"; \
    Flags: runhidden waituntilterminated

[Code]
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
