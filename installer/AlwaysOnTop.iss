#ifndef MyAppVersion
#define MyAppVersion "0.0.0"
#endif

#define MyAppName "AlwaysOnTop"
#define MyAppPublisher "Jacob Pedersen"
#define MyAppURL "https://github.com/bleze/AlwaysOnTop"
#define MyAppExeName "AlwaysOnTop.exe"

[Setup]
AppId={{6C6B8C6E-6E9B-4A9F-9E7A-6E7B7B6C6B8C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
OutputDir=..\dist
OutputBaseFilename=AlwaysOnTop-Setup-{#MyAppVersion}
SetupIconFile=..\resources\app.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start AlwaysOnTop automatically when Windows starts"; GroupDescription: "Additional options:"

[Files]
Source: "..\build\Release\AlwaysOnTop.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\AlwaysOnTopHook.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "AlwaysOnTop"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now"; Flags: nowait postinstall
