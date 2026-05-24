; Inno Setup installer for the chromacal Color Match Premiere/AE effect (Windows).
; The Windows analog of make_installer.sh (macOS .pkg). Builds a single
; chromacal-<ver>-win-x64.exe that installs chromacal.aex + its bundled DLLs into
; the shared Adobe MediaCore plug-ins folder, where Premiere Pro and After Effects
; both load it.
;
; Build it (after running build_windows.ps1, which stages dist\chromacal):
;   iscc /DAppVersion=0.1.0 plugin\effect\chromacal.iss
; Output: dist\chromacal-<ver>-win-x64.exe
;
; To distribute without SmartScreen warnings, sign the resulting .exe with your
; Windows code-signing certificate (signtool sign /fd sha256 /tr <ts> ...).

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
; Folder staged by build_windows.ps1 (relative to this .iss file: plugin\effect).
#ifndef StageDir
  #define StageDir "..\..\dist\chromacal"
#endif

[Setup]
AppId={{C4A1E7D2-3B6F-4A9C-9E21-CHROMACAL0001}
AppName=chromacal Color Match
AppVersion={#AppVersion}
AppPublisher=Kevin Blackburn-Matzen
DefaultDirName={commonpf64}\Adobe\Common\Plug-ins\7.0\MediaCore\chromacal
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName=chromacal Color Match
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=chromacal-{#AppVersion}-win-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
; The .aex and every dependency DLL staged next to it.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
WelcomeLabel2=This will install the chromacal Color Match effect for Adobe Premiere Pro and After Effects.%n%nClose Premiere Pro / After Effects before continuing.

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  // The staged folder must exist (run build_windows.ps1 first).
  if not DirExists(ExpandConstant('{#StageDir}')) then
  begin
    MsgBox('Staged plugin not found at {#StageDir}.' + #13#10 +
           'Run plugin\effect\build_windows.ps1 first.', mbError, MB_OK);
    Result := False;
  end;
end;
