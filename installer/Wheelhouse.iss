; Wheelhouse - Windows installer (Inno Setup 6). Installs the standalone app and the VST3 plugin.
; Built by the GitHub workflow:
;   iscc /DAppVersion=0.1.0 /DBuildDir=..\build installer\Wheelhouse.iss
; Output: installer\Output\Wheelhouse-Setup-<version>.exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

#define AppName "Wheelhouse"
#define AppPublisher "Lorenzo Frank"
#define AppUrl "https://github.com/lfrank95/wheelhouse"
#define AppExe "Wheelhouse.exe"

[Setup]
; never change the AppId: it is how Windows recognises updates and the uninstaller
AppId={{279F814A-64D1-481E-A935-9F50A41B17C5}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=Wheelhouse-Setup-{#AppVersion}
SetupIconFile=wheelhouse.ico
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern
WizardImageFile=wizard-large.bmp
WizardSmallImageFile=wizard-small.bmp
Compression=lzma2/max
SolidCompression=yes
; 64-bit only, installs into "Program Files" and "Common Files\VST3": needs admin rights
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
; an update closes a running Wheelhouse (and a DAW holding the plugin) via the Restart Manager
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.CompApp=Standalone-App (Wheelhouse.exe)
en.CompApp=Standalone app (Wheelhouse.exe)
de.CompVst3=VST3-Plugin (für DAWs, in Common Files\VST3)
en.CompVst3=VST3 plugin (for DAWs, in Common Files\VST3)
de.TypeFull=Vollständig (App und Plugin)
en.TypeFull=Full (app and plugin)
de.TypeApp=Nur Standalone-App
en.TypeApp=Standalone app only
de.TypeVst3=Nur VST3-Plugin
en.TypeVst3=VST3 plugin only
de.TypeCustom=Benutzerdefiniert
en.TypeCustom=Custom

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "app"; Description: "{cm:TypeApp}"
Name: "vst3"; Description: "{cm:TypeVst3}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "app"; Description: "{cm:CompApp}"; Types: full app custom
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full vst3 custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: app; Flags: unchecked

[Files]
Source: "{#BuildDir}\Wheelhouse_artefacts\Release\{#AppExe}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; the VST3 is a bundle folder: copied as a whole
Source: "{#BuildDir}\WheelhousePlugin_artefacts\Release\VST3\Wheelhouse.vst3\*"; DestDir: "{commoncf64}\VST3\Wheelhouse.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; an update replaces the whole bundle (no stale files from an older plugin layout)
Type: filesandordirs; Name: "{commoncf64}\VST3\Wheelhouse.vst3"; Components: vst3

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; Components: app
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Components: app; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Wheelhouse.vst3"
; settings and presets in %APPDATA%\Wheelhouse are deliberately kept
