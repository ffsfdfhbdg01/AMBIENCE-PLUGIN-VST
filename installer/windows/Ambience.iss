; Inno Setup 6 script for the Ambience plugin (Windows x64).
; Build:  iscc /DAppVersion=1.0.0 /DBuildDir=..\..\build\Ambience_artefacts\Release installer\windows\Ambience.iss
;
; - VST3 goes to the standard folder: C:\Program Files\Common Files\VST3 (all users) or
;   %LOCALAPPDATA%\Programs\Common\VST3 (current user only). The folder can be changed on the
;   "Select Destination Location" page.
; - Optional Standalone application goes to Program Files\Ambience Audio\Ambience.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Ambience_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName "Ambience"
#define Publisher "Ambience Audio"

[Setup]
AppId={{746B5355-6551-467E-A00E-96B12B11C003}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#Publisher}
VersionInfoDescription={#AppName} VST3 plug-in installer
; Destination = VST3 folder. {autocf} = Common Files (admin) or per-user Common Files.
DefaultDirName={autocf}\VST3
AppendDefaultDirName=no
DirExistsWarning=no
DisableDirPage=no
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
UninstallFilesDir={autopf}\{#Publisher}\{#AppName}\Uninstall
UninstallDisplayName={#AppName} (VST3)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-Setup
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
MinVersion=10.0

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[CustomMessages]
en.TypeFull=Full installation
pl.TypeFull=Pełna instalacja
en.TypeCustom=Custom installation
pl.TypeCustom=Instalacja niestandardowa
en.CompVst3=VST3 plug-in
pl.CompVst3=Wtyczka VST3
en.CompStandalone=Standalone application
pl.CompStandalone=Aplikacja samodzielna (Standalone)
en.DirHint=Choose the VST3 plug-in folder. The default is the standard VST3 location searched by all hosts (Nuendo, Cubase, Reaper, Resolve, Premiere...).
pl.DirHint=Wybierz folder wtyczek VST3. Domyślny to standardowa lokalizacja VST3, w której szukają wszystkie hosty (Nuendo, Cubase, Reaper, Resolve, Premiere...).

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "{cm:CompStandalone}"; Types: full

[Files]
Source: "{#BuildDir}\VST3\Ambience.vst3\*"; DestDir: "{app}\Ambience.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Ambience.exe"; DestDir: "{autopf}\{#Publisher}\{#AppName}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{autopf}\{#Publisher}\{#AppName}\Ambience.exe"; Components: standalone

[UninstallDelete]
Type: dirifempty; Name: "{autopf}\{#Publisher}\{#AppName}"
Type: dirifempty; Name: "{autopf}\{#Publisher}"

[Code]
procedure InitializeWizard;
begin
  WizardForm.SelectDirLabel.Caption := CustomMessage('DirHint');
end;
