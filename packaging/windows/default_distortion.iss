#ifndef AppVersion
  #error AppVersion must be provided with /DAppVersion=x.y.z
#endif
#ifndef SourceRoot
  #error SourceRoot must point to the staged package directory
#endif
#ifndef OutputDir
  #error OutputDir must point to the installer output directory
#endif

[Setup]
AppId={{BB9376F5-A1D0-431B-91DB-12CFE4A9FA57}
AppName=default_distortion
AppVersion={#AppVersion}
AppPublisher=default_audio
AppPublisherURL=https://default-audio.github.io/
AppSupportURL=https://default-audio.github.io/
DefaultDirName={autopf}\default_audio\default_distortion
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\LICENSE.md
OutputDir={#OutputDir}
OutputBaseFilename=default_distortion-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=default_distortion {#AppVersion}

[Files]
Source: "{#SourceRoot}\VST3\default_distortion.vst3\*"; DestDir: "{commoncf}\VST3\default_distortion.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\Standalone\default_distortion.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILDING.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSES\*"; DestDir: "{app}\LICENSES"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\default_distortion"; Filename: "{app}\default_distortion.exe"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\default_distortion.vst3"
