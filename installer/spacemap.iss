#define AppName "Space Map"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

[Setup]
AppId={{6F4C2D1A-8B3E-4E6A-9C57-2D0A5E7B91F4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Space Map
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=SpaceMap-{#AppVersion}-windows-x64-setup
SetupIconFile=..\res\spacemap.ico
UninstallDisplayIcon={app}\spacemap.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\spacemap.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\spacemap.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\spacemap.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\spacemap.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
