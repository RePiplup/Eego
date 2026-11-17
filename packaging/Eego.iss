#ifndef Payload
  #error Define Payload as the extracted Eego release directory
#endif
#ifndef ReleaseVersion
  #error Define ReleaseVersion as the desktop release version
#endif

[Setup]
AppId=RePiplup.Eego
AppName=Eego
AppVersion={#ReleaseVersion}
VersionInfoVersion={#ReleaseVersion}.0
DefaultDirName={localappdata}\Programs\Eego
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputBaseFilename=Eego-{#ReleaseVersion}-windows-x64-setup
SetupIconFile=..\apps\Eego\assets\eego.ico
UninstallDisplayIcon={app}\HandmadeBTQuick.exe
Compression=lzma2/fast
WizardStyle=modern
RestartApplications=no

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Eego\Eego"; Filename: "{app}\HandmadeBTQuick.exe"; WorkingDir: "{app}"
Name: "{userprograms}\Eego\Browser Setup"; Filename: "{app}\Browser-Setup.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{app}\HandmadeBTQuick.exe"; WorkingDir: "{app}"; Flags: nowait; Check: IsEegoUpdate

[Code]
function IsEegoUpdate: Boolean;
begin
  Result := ExpandConstant('{param:EEGOUPDATE|0}') = '1';
end;
