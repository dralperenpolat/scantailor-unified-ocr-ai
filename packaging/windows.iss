#ifndef StageDir
  #error StageDir must point to the verified clean distribution
#endif
#ifndef ReleaseDir
  #error ReleaseDir must point to the release output directory
#endif
[Setup]
AppId={{B1C80F8C-0C43-463A-B351-093BC6E10C43}
AppName=ScanTailor OCR Unified
AppVersion=0.4.4
AppPublisher=Alperen Polat and ScanTailor contributors
AppPublisherURL=https://alperenpolat.net
DefaultDirName={localappdata}\Programs\scantailor-ocr-unified
DefaultGroupName=ScanTailor OCR Unified
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#ReleaseDir}
OutputBaseFilename=scantailor-ocr-unified-0.4.4-windows-x64-setup
Compression=lzma2/fast
SolidCompression=yes
LZMANumBlockThreads=2
WizardStyle=modern
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\native\bin\scantailor-ocr-unified.exe
SetupIconFile=..\native\scantailor-experimental\src\resources\win32\icon.ico
LicenseFile=..\LICENSE
CloseApplications=no
RestartApplications=no

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[CustomMessages]
turkish.DesktopShortcut=Masaüstüne kısayol ekle
english.DesktopShortcut=Create a desktop shortcut
turkish.LaunchProgram=ScanTailor OCR Unified'ı aç
english.LaunchProgram=Open ScanTailor OCR Unified

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ScanTailor OCR Unified"; Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ScanTailor OCR Unified"; Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent unchecked

; No user projects, OCR models or output PDFs are deleted during uninstall.
; They reside under {localappdata}\scantailor-ocr-unified, outside {app}.
