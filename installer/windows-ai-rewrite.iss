#define AppId "{{C54C5132-94B4-4D41-9157-680DA9A0F329}"
#define AppName "Windows AI Rewrite"
#define AppVersion "0.1.0"
#define AppPublisher "Windows AI Rewrite"
#define AppExeName "WindowsAIRewrite.exe"
#define BuildOutputDir "..\\build\\windows-msvc-x64\\Release"
#define SourceExePath BuildOutputDir + "\\" + AppExeName

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=..\resources\app.ico
OutputDir=..\build\installer
OutputBaseFilename=windows-ai-rewrite-setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Run Windows AI Rewrite at startup"; GroupDescription: "Additional tasks:"; Flags: unchecked

[Files]
Source: "{#SourceExePath}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autoprograms}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "WindowsAIRewrite"; ValueData: """{app}\{#AppExeName}"""; Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent unchecked
