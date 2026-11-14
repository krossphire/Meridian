; Inno Setup script for Meridian. The release workflow passes /DSourceDir and /DAppVersion.

#ifndef SourceDir
#define SourceDir "dist"
#endif
#ifndef AppVersion
#define AppVersion "0.1.0"
#endif

[Setup]
AppId={{A7B3E2C1-5D44-4F0A-9C21-6E8F0A1B2C3D}
AppName=Meridian
AppVersion={#AppVersion}
AppVerName=Meridian {#AppVersion}
AppPublisher=Andrew, K5PO
DefaultDirName={autopf}\Meridian
DefaultGroupName=Meridian
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename=Meridian-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
UninstallDisplayIcon={app}\meridian.exe

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Meridian"; Filename: "{app}\meridian.exe"
Name: "{autodesktop}\Meridian"; Filename: "{app}\meridian.exe"

[Run]
Filename: "{app}\meridian.exe"; Description: "Launch Meridian"; Flags: nowait postinstall skipifsilent
