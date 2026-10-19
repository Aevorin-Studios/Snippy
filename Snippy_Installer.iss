; Inno Setup Script for Snippy
; Compiles Snippy into Windows Setup Installer (Snippy_Setup.exe)

#define MyAppName "Snippy"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Aevorin Studios"
#define MyAppURL "https://github.com/Aevorin-Studios/Snippy-for-Windows"
#define MyAppExeName "Snippy.exe"

[Setup]
; Use identical AppId from v1.0.7 so it cleanly upgrades existing installations in place
AppId={{9A8B7C12-3D4E-5F6A-7B8C-9D0E1F2A3B4C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={localappdata}\Programs\{#MyAppName}
LicenseFile=LICENSE
DisableProgramGroupPage=yes
OutputBaseFilename=Snippy_Setup
OutputDir=InstallerOutput
SetupIconFile=src\windows\resources\icons\snippy.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
CloseApplications=yes
CloseApplicationsFilter=Snippy.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Automatically launch Snippy when Windows boots up"; GroupDescription: "Windows Startup Options:"

[Files]
Source: "build\Snippy.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "src\windows\resources\icons\app.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "src\windows\resources\icons\app.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "src\windows\resources\icons\favicon.svg"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Snippy"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
