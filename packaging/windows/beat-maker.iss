; Inno Setup script for the Beat Maker Windows installer.
; Built by tools\package.ps1, which stages the app and passes the defines below:
;   /DAppVersion=1.0.0-beta.1  /DNumericVersion=1.0.0  /DSourceDir=<staged folder>
;   /DOutputDir=<dist>  /DOutputBaseName=<file name without .exe>  [/DIconFile=<icon.ico>]
; Requires Inno Setup 6.3 or later (winget install JRSoftware.InnoSetup).

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point at the staged app folder (run tools\package.ps1)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBaseName
  #define OutputBaseName "Beat Maker-" + AppVersion + "-windows-x64-setup"
#endif

[Setup]
; AppId identifies the app for upgrades and uninstall: never change it.
AppId={{1F566C1F-1728-4C0B-BCBD-2ABA29AD6602}
AppName=Beat Maker
AppVersion={#AppVersion}
AppVerName=Beat Maker {#AppVersion}
AppPublisher=Beat Maker
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#NumericVersion}
VersionInfoDescription=Beat Maker installer
DefaultDirName={autopf}\Beat Maker
DefaultGroupName=Beat Maker
DisableProgramGroupPage=yes
; Admin install to Program Files by default; the user can choose a per-user install instead.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Beat Maker.exe
UninstallDisplayName=Beat Maker {#AppVersion}
#ifdef IconFile
SetupIconFile={#IconFile}
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The staged folder: Beat Maker.exe (+ .pdb for crash backtraces), assets\, docs\, LICENSE, README.md, CHANGELOG.md.
; The app finds assets\ and docs\ next to its own executable.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Beat Maker"; Filename: "{app}\Beat Maker.exe"
Name: "{autoprograms}\Beat Maker User Guide"; Filename: "{app}\docs\index.html"; Check: UserGuideInstalled
Name: "{autodesktop}\Beat Maker"; Filename: "{app}\Beat Maker.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Beat Maker.exe"; Description: "{cm:LaunchProgram,Beat Maker}"; Flags: nowait postinstall skipifsilent

[Code]
function UserGuideInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\docs\index.html'));
end;
