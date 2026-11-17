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

#define EdgeHost "Software\Microsoft\Edge\NativeMessagingHosts\org.handmadebt.bridge"
#define ChromeHost "Software\Google\Chrome\NativeMessagingHosts\org.handmadebt.bridge"

[Registry]
Root: HKCU; Subkey: "{#EdgeHost}"; ValueType: string; ValueData: "{app}\browser\native-host.json"
Root: HKCU; Subkey: "{#ChromeHost}"; ValueType: string; ValueData: "{app}\browser\native-host.json"

[Icons]
Name: "{userprograms}\Eego\Eego"; Filename: "{app}\HandmadeBTQuick.exe"; WorkingDir: "{app}"
Name: "{userprograms}\Eego\Browser Setup"; Filename: "{app}\Browser-Setup.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{app}\HandmadeBTQuick.exe"; WorkingDir: "{app}"; Flags: nowait; Check: IsEegoUpdate

[Code]
procedure RemoveBrowserHostIfOwned(Key: String);
var
  Manifest: String;
begin
  if RegQueryStringValue(HKCU, Key, '', Manifest) and
     (CompareText(Manifest, ExpandConstant('{app}\browser\native-host.json')) = 0) then
  begin
    RegDeleteValue(HKCU, Key, '');
    RegDeleteKeyIfEmpty(HKCU, Key);
  end;
end;

procedure CurUninstallStepChanged(Step: TUninstallStep);
begin
  if Step = usUninstall then
  begin
    RemoveBrowserHostIfOwned('{#EdgeHost}');
    RemoveBrowserHostIfOwned('{#ChromeHost}');
  end;
end;

function IsEegoUpdate: Boolean;
begin
  Result := ExpandConstant('{param:EEGOUPDATE|0}') = '1';
end;
