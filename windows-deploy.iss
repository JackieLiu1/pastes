; Build with the package CMake target.
#ifndef DeployDir
  #error DeployDir must point to the clean deployment directory
#endif
#ifndef MyAppVersion
  #error MyAppVersion must match the CMake project version
#endif
#ifndef OutputDir
  #define OutputDir SourcePath + "build\dist"
#endif
#define MyAppName "Pastes"
#define MyAppURL "https://github.com/JackieLiu1/pastes"

[Setup]
AppId={{0BF3E82A-DF86-47CB-82C4-E75052416F49}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Jackie Liu
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
; The database lives beside pastes.exe, so this must be user-writable.
DefaultDirName={localappdata}\Programs\{#MyAppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Pastes-{#MyAppVersion}-windows-x64-setup
SetupIconFile=resources\pastes.ico
UninstallDisplayIcon={app}\pastes.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "packaging\windows\ChineseSimplified.isl"

[CustomMessages]
english.StartAtLogin=Start Pastes when I sign in
chinesesimplified.StartAtLogin=登录 Windows 时启动 Pastes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
Name: "startup"; Description: "{cm:StartAtLogin}"; Flags: unchecked

[Files]
; This directory contains only staged binaries, plugins and licenses.
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\pastes.exe"; IconFilename: "{app}\pastes.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\pastes.exe"; IconFilename: "{app}\pastes.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\pastes.exe"; Parameters: "--show"; Description: "{cm:LaunchProgram,Pastes}"; Flags: nowait postinstall skipifsilent

[Registry]
; Elevated legacy upgrades can remove the obsolete machine-wide startup value.
Root: HKLM32; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Pastes"; Flags: deletevalue; Check: IsAdmin
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Pastes"; Flags: deletevalue; Check: IsAdmin and IsWin64
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Pastes"; ValueData: """{app}\pastes.exe"""; Flags: uninsdeletevalue; Tasks: startup

; No UninstallDelete: preserve user-created history on uninstall/reinstall.
