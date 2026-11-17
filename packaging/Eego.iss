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
Source: "Enable-EdgeAssistant.ps1"; DestDir: "{app}\browser"; Flags: ignoreversion

#define EdgeHost "Software\Microsoft\Edge\NativeMessagingHosts\org.handmadebt.bridge"
#define ChromeHost "Software\Google\Chrome\NativeMessagingHosts\org.handmadebt.bridge"

[Registry]
Root: HKCU; Subkey: "{#EdgeHost}"; ValueType: string; ValueData: "{app}\browser\native-host.json"
Root: HKCU; Subkey: "{#ChromeHost}"; ValueType: string; ValueData: "{app}\browser\native-host.json"

[Icons]
Name: "{userprograms}\Eego\Eego"; Filename: "{app}\HandmadeBTQuick.exe"; WorkingDir: "{app}"
Name: "{userprograms}\Eego\启用 Edge 下载助手"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File ""{app}\browser\Enable-EdgeAssistant.ps1"""
Name: "{userprograms}\Eego\Browser Setup"; Filename: "{app}\Browser-Setup.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File ""{app}\browser\Enable-EdgeAssistant.ps1"""; Description: "在 Microsoft Edge 中启用 Eego 下载助手"; Flags: postinstall nowait skipifsilent; Check: not IsEegoUpdate
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
