; Inno Setup script for Taskbar Media Player (https://jrsoftware.org/isinfo.php).
; Per-user install, no administrator rights: installs to %LOCALAPPDATA%\Programs and registers an
; uninstaller in Settings > Apps.
; Build with installer\build-installer.ps1 (or: ISCC /DAppVersion=2.0.0 installer\TaskbarMediaPlayer.iss).

#ifndef AppVersion
  #define AppVersion "2.0.0"
#endif
#define Root ".."
#define AppExe "taskbar-mediaplayer.exe"

[Setup]
AppId={{EBA4A1D3-2F9E-40B9-AA52-7BA1E0EC43B8}
AppName=Taskbar Media Player
AppVersion={#AppVersion}
AppVerName=Taskbar Media Player {#AppVersion}
AppPublisher=Jeremie-pires
DefaultDirName={autopf}\Taskbar Media Player
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=TaskbarMediaPlayer-Setup-{#AppVersion}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=Taskbar Media Player
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.AutostartTask=Démarrer Taskbar Media Player avec Windows
english.AutostartTask=Start Taskbar Media Player when I sign in to Windows
french.LaunchNow=Lancer Taskbar Media Player
english.LaunchNow=Start Taskbar Media Player now

[Tasks]
Name: "autostart"; Description: "{cm:AutostartTask}"

[Files]
Source: "{#Root}\build\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; DestName: "THIRD-PARTY-NOTICES.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Taskbar Media Player"; Filename: "{app}\{#AppExe}"

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchNow}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitApp"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'TaskbarMediaPlayer';

{ Same command line the app's own "Démarrer avec Windows" menu item writes, so the two stay in sync. }
function InstalledRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\{#AppExe}') + '"';
end;

{ Removes the startup entry only if it points at this installed copy, so a portable copy's
  "Démarrer avec Windows" setting is left alone. }
procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, RunValue, Value) and (CompareText(Value, InstalledRunValue()) = 0) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  { Upgrade: close the running copy so its exe can be replaced. }
  if (CurStep = ssInstall) and FileExists(ExpandConstant('{app}\{#AppExe}')) then
    Exec(ExpandConstant('{app}\{#AppExe}'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('autostart') then
      RegWriteStringValue(HKCU, RunKey, RunValue, InstalledRunValue())
    else
      RemoveOwnAutostart();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOwnAutostart();
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\TaskbarMediaPlayer');
  end;
end;
