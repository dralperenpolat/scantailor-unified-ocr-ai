#ifndef StageDir
  #error StageDir must point to the verified clean distribution
#endif
#ifndef Variant
  #define Variant "full"
#endif
#ifndef ReleaseDir
  #error ReleaseDir must point to the release output directory
#endif
[Setup]
AppId={{B1C80F8C-0C43-463A-B351-093BC6E10C43}
AppName=ScanTailor OCR Unified
AppVersion=0.6.5
AppPublisher=Alperen Polat and ScanTailor contributors
AppPublisherURL=https://alperenpolat.net
DefaultDirName={localappdata}\Programs\scantailor-ocr-unified
DefaultGroupName=ScanTailor OCR Unified
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#ReleaseDir}
OutputBaseFilename=scantailor-ocr-unified-0.6.5-windows-x64-{#Variant}-setup
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

[CustomMessages]
turkish.DesktopShortcut=Masaüstüne kısayol ekle
english.DesktopShortcut=Create a desktop shortcut
turkish.LaunchProgram=ScanTailor OCR Unified'ı aç
english.LaunchProgram=Open ScanTailor OCR Unified
turkish.AppRunning=Bu kurulum klasöründeki ScanTailor veya işçisi çalışıyor. Mevcut işlemi uygulamadan güvenle bitirip pencereyi kapattıktan sonra kurulumu yeniden çalıştırın. Çalışan işlem durdurulmadı.
english.AppRunning=ScanTailor or its worker is running from this installation folder. Finish the operation safely and close the application before retrying setup. The running process was not stopped.
turkish.ProcessCheckFailed=Çalışan uygulamalar kontrol edilemedi. Kurulum dosyaları değiştirilmedi; kurulumu yeniden deneyin.
english.ProcessCheckFailed=Could not check running applications. Installation files were not changed; retry setup.
turkish.MigrationFailed=Önceki ayarlar aktarılamadı. Kurulum günlüğünü kontrol edin; eski projeler ve ayarlar korunuyor.
english.MigrationFailed=Could not migrate previous settings. Check the setup log; old projects and settings are preserved.

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; Only application-owned dependencies are replaced on upgrade. This also
; prevents a full -> minimal upgrade from retaining large AI libraries.
[InstallDelete]
Type: filesandordirs; Name: "{app}\runtime"
Type: filesandordirs; Name: "{app}\resources\paddle-models"
Type: filesandordirs; Name: "{app}\resources\UVDoc"

[Icons]
Name: "{group}\ScanTailor OCR Unified"; Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ScanTailor OCR Unified"; Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\native\bin\scantailor-ocr-unified.exe"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent unchecked

; No user projects, OCR models or output PDFs are deleted during uninstall.
; They reside under {localappdata}\scantailor-ocr-unified, outside {app}.

[Code]
var
  LegacyLinks, LinkBackups: TStringList;
  MigrationFailed: Boolean;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Locator, Services, Processes, Process: Variant;
  I: Integer;
  Root, FileName: String;
begin
  Result := '';
  if not DirExists(ExpandConstant('{app}')) then Exit;
  Root := Lowercase(AddBackslash(ExpandConstant('{app}')));
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ExecutablePath FROM Win32_Process WHERE Name=''python.exe'' OR Name=''pythonw.exe'' OR Name=''scantailor-ocr-unified.exe'' OR Name=''scantailor-experimental-cli.exe''');
    for I := 0 to Processes.Count - 1 do begin
      Process := Processes.ItemIndex(I);
      if not VarIsNull(Process.ExecutablePath) then begin
        FileName := Process.ExecutablePath;
        FileName := Lowercase(FileName);
        if Pos(Root, FileName) = 1 then begin
          Result := CustomMessage('AppRunning');
          Exit;
        end;
      end;
    end;
  except
    Log(GetExceptionMessage);
    Result := CustomMessage('ProcessCheckFailed');
  end;
end;

procedure BackupLinks(const Folder, Backup: String);
var
  Found: TFindRec;
  Shell, Link: Variant;
  Path, Target, Args, Saved: String;
begin
  Shell := CreateOleObject('WScript.Shell');
  if FindFirst(AddBackslash(Folder) + '*Scan*Tailor*.lnk', Found) then begin
    try
      repeat
        Path := AddBackslash(Folder) + Found.Name;
        Link := Shell.CreateShortcut(Path);
        Target := Link.TargetPath;
        Target := Lowercase(Target);
        Args := Link.Arguments;
        Args := Lowercase(Args);
        if (ExtractFileName(Target) = 'scantailor-ocr-unified.exe') or
           ((ExtractFileName(Target) = 'baslat.cmd') and (Pos('scantailor', Target) > 0)) or
           ((Pos('python', ExtractFileName(Target)) = 1) and (Pos('scantailor', Args) > 0)) then begin
          Saved := AddBackslash(Backup) + IntToStr(LegacyLinks.Count) + '-' + Found.Name;
          if not FileCopy(Path, Saved, True) then RaiseException('Could not back up shortcut: ' + Path);
          LegacyLinks.Add(Path);LinkBackups.Add(Saved);
        end;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Backup, Path: String;
  I, Code: Integer;
begin
  if CurStep = ssInstall then begin
    LegacyLinks := TStringList.Create;LinkBackups := TStringList.Create;
    Backup := ExpandConstant('{localappdata}\scantailor-ocr-unified\upgrade-backups\shortcuts-') + GetDateTimeString('yyyymmdd-hhnnss', '-', ':');
    I := 0;
    while DirExists(Backup + '-' + IntToStr(I)) do I := I + 1;
    Backup := Backup + '-' + IntToStr(I);
    if not ForceDirectories(Backup) then RaiseException('Could not create shortcut backup folder');
    BackupLinks(ExpandConstant('{autodesktop}'), Backup);
    BackupLinks(ExpandConstant('{userprograms}\ScanTailor OCR Unified'), Backup);
  end;
  if CurStep = ssPostInstall then begin
    if not Exec(ExpandConstant('{app}\runtime\python.exe'), '-I "' + ExpandConstant('{app}\packaging\install_state.py') + '"', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then Code := -1;
    MigrationFailed := Code <> 0;
    if MigrationFailed then begin
      Log(CustomMessage('MigrationFailed'));
      if not WizardSilent then MsgBox(CustomMessage('MigrationFailed'), mbError, MB_OK);
      Exit;
    end;
    for I := 0 to LegacyLinks.Count - 1 do begin
      Path := LegacyLinks[I];
      if (CompareText(Path, ExpandConstant('{autodesktop}\ScanTailor OCR Unified.lnk')) <> 0) and
         (CompareText(Path, ExpandConstant('{userprograms}\ScanTailor OCR Unified\ScanTailor OCR Unified.lnk')) <> 0) and
         FileExists(Path) then begin
        if GetSHA256OfFile(Path) = GetSHA256OfFile(LinkBackups[I]) then
          if not DeleteFile(Path) then RaiseException('Could not remove the backed-up obsolete shortcut: ' + Path);
      end;
    end;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if MigrationFailed then Result := 1 else Result := 0;
end;

procedure DeinitializeSetup;
begin
  if Assigned(LegacyLinks) then LegacyLinks.Free;
  if Assigned(LinkBackups) then LinkBackups.Free;
end;
