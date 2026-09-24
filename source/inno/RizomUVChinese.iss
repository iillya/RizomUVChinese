; Inno Setup is the sole supported installer. Runtime payload is allowlisted.
#ifndef PayloadInclude
  #error Build using source\build_inno.bat
#endif
#define ProductId "RizomUVChinese.Inno"
#define ProductName "RizomUV 中文补丁"
#ifdef TestMode
  #define ProductId "RizomUVChinese.Inno.IsolatedTests"
  #define ProductName "RizomUV Inno Isolated Tests"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#PackageVersion}
AppPublisher=神说要凑数
AppPublisherURL=https://space.bilibili.com/281243426
AppSupportURL=https://github.com/iillya/RizomUVChinese
DefaultDirName={autopf}\Rizom Lab\RizomUV 2025.0
AppendDefaultDirName=no
DisableDirPage=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UsePreviousTasks=no
UninstallFilesDir={app}\ChineseLauncher\.inno
UninstallDisplayIcon={app}\ChineseLauncher\RizomUVChineseLauncher.exe
OutputDir={#PackageOutput}
OutputBaseFilename=RizomUVChineseInstaller
SetupIconFile=..\..\icon\rizomuv.ico
SetupArchitecture=x64
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.14393
WizardStyle=modern light windows11
WizardSizePercent=110
WizardImageFile=
WizardSmallImageFile=
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
UninstallLogging=no
#ifdef TestMode
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[LangOptions]
DialogFontName=Microsoft YaHei UI
DialogFontSize=10

[Messages]
SelectDirLabel3=请选择包含 rizomuv.exe 的软件目录。补丁只写入其下的 ChineseLauncher 文件夹。
FinishedLabel=中文补丁已安装。请通过“RizomUV 中文版”快捷方式启动软件。%n%n卸载请使用 Windows“已安装的应用”。卸载会完整删除 ChineseLauncher 文件夹及其中的全部文件，不保留词典、设置或备份。

[Tasks]
Name: "desktopicon"; Description: "创建公共桌面快捷方式"
Name: "startmenuicon"; Description: "创建开始菜单快捷方式（所有用户）"


[Files]
Source: "{#SupportDll}"; DestDir: "{app}\ChineseLauncher\.inno"; DestName: "support.dll"; Flags: ignoreversion
#include PayloadInclude

[Icons]
#ifndef TestMode
Name: "{autodesktop}\RizomUV 中文版"; Filename: "{app}\ChineseLauncher\RizomUVChineseLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\ChineseLauncher\RizomUVChineseLauncher.exe"; IconIndex: 0; Tasks: desktopicon
Name: "{autoprograms}\RizomUV 中文版"; Filename: "{app}\ChineseLauncher\RizomUVChineseLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\ChineseLauncher\RizomUVChineseLauncher.exe"; IconIndex: 0; Tasks: startmenuicon
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}\ChineseLauncher"


[Code]
var
  PayloadNames: TArrayOfString;

function CheckTarget(Directory: String; CheckVersion: Boolean; Message: String; Capacity: Cardinal): Boolean;
external 'CheckTarget@files:support.dll stdcall setuponly';
function CheckTargetUninstall(Directory: String; CheckVersion: Boolean; Message: String; Capacity: Cardinal): Boolean;
external 'CheckTarget@{app}\ChineseLauncher\.inno\support.dll stdcall uninstallonly delayload';

procedure InitPayload;
begin
  { Generated from the same explicit payload allowlist as the native packer. }
  #include PayloadCode
end;

function InstallRoot: String;
begin
  Result := AddBackslash(ExpandConstant('{app}')) + 'ChineseLauncher';
end;

function StateFile: String;
begin
  Result := InstallRoot + '\.inno\install-state.ini';
end;

function BufferText(Buffer: String): String;
var
  Terminator: Integer;
begin
  Terminator := Pos(#0, Buffer);
  if Terminator > 0 then Result := Copy(Buffer, 1, Terminator - 1)
  else Result := Buffer;
end;

function ValidateDirectory(Directory: String): String;
var
  Message: String;
begin
  SetLength(Message, 1024);
  if CheckTarget(Directory, True, Message, 1024) then Result := ''
  else Result := BufferText(Message);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Error: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then Exit;
  Error := ValidateDirectory(WizardDirValue);
  Result := Error = '';
  if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  PreviousDir, ExistingOwner: String;
  RegistryRoot: Integer;
begin
  Result := ValidateDirectory(ExpandConstant('{app}'));
  if Result <> '' then Exit;
  ExistingOwner := GetIniString('Install', 'Owner', '', StateFile);
  if (ExistingOwner <> '') and (ExistingOwner <> '{#ProductId}') and
       (Pos('RizomUVChinese.Inno', ExistingOwner) <> 1) then begin
    Result := 'ChineseLauncher 的安装记录属于另一安装器，已停止覆盖。';
    Exit;
  end;
  RegistryRoot := HKLM64;
  #ifdef TestMode
  RegistryRoot := HKCU;
  #endif
  if RegQueryStringValue(RegistryRoot,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductId}_is1',
    'InstallLocation', PreviousDir) and
    (CompareText(RemoveBackslashUnlessRoot(PreviousDir), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0) then begin
    Result := '当前安装器一次只管理一个 RizomUV 目录。请先卸载旧目录中的中文补丁，再更换安装位置。';
    Exit;
  end;

end;

procedure BackupExistingPayload;
var
  I, Suffix: Integer;
  Source, Target, Base, Backup: String;
begin
  Backup := '';
  for I := 0 to GetArrayLength(PayloadNames) - 1 do begin
    Source := InstallRoot + '\' + PayloadNames[I];
    if FileExists(Source) then begin
      if Backup = '' then begin
        Base := InstallRoot + '\.inno\backups\' + GetDateTimeString('yyyymmdd-hhnnss', '-', ':');
        Backup := Base;
        Suffix := 0;
        while DirExists(Backup) do begin
          Suffix := Suffix + 1;
          Backup := Base + '-' + IntToStr(Suffix);
        end;
      end;
      Target := Backup + '\' + PayloadNames[I];
      if not ForceDirectories(ExtractFileDir(Target)) or not CopyFile(Source, Target, True) or
        (GetSHA256OfFile(Source) <> GetSHA256OfFile(Target)) then
        RaiseException('无法完成升级前备份，未开始替换汉化文件：' + Source);
    end;
  end;
  if (Backup <> '') and FileExists(StateFile) then
    if not CopyFile(StateFile, Backup + '\install-state.ini', True) then
      RaiseException('无法备份旧安装记录，未开始替换汉化文件。');
  if Backup <> '' then Log('Pre-install recovery snapshot: ' + Backup);
end;

#include "cleanup.iss"

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    #ifndef TestMode
    if FileExists(ExpandConstant('{userdesktop}\RizomUV 中文版.lnk')) and
      not DeleteFile(ExpandConstant('{userdesktop}\RizomUV 中文版.lnk')) then
      RaiseException('无法清理旧版当前用户桌面快捷方式，请关闭占用程序后重试。');
    if FileExists(ExpandConstant('{userprograms}\RizomUV 中文版.lnk')) and
      not DeleteFile(ExpandConstant('{userprograms}\RizomUV 中文版.lnk')) then
      RaiseException('无法清理旧版当前用户开始菜单快捷方式，请关闭占用程序后重试。');
    #endif
    BackupExistingPayload;
  end;
  if CurStep = ssPostInstall then begin
    DeleteIniSection('Hashes', StateFile);
    if not SetIniString('Install', 'Owner', '{#ProductId}', StateFile) then
      RaiseException('文件已安装，但安装记录无法保存；请保留日志并重新运行安装器。');
      #ifndef TestMode
      { Remove alternate uninstall records from other installer IDs for the
        same product so they cannot later delete this shared ChineseLauncher. }
      if CompareText('{#ProductId}', 'RizomUVChinese.Inno') <> 0 then
        RegDeleteKeyIncludingSubkeys(HKLM64,
          'Software\Microsoft\Windows\CurrentVersion\Uninstall\RizomUVChinese.Inno_is1');
      RegDeleteKeyIncludingSubkeys(HKLM64,
        'Software\Microsoft\Windows\CurrentVersion\Uninstall\RizomUVChinese.Inno.Experimental_is1');
      #endif
    CleanupInstallerBackups;
  end;
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel1.Alignment := taLeftJustify;
  WizardForm.WelcomeLabel2.Alignment := taLeftJustify;
  WizardForm.FinishedHeadingLabel.Alignment := taLeftJustify;
  WizardForm.FinishedLabel.Alignment := taLeftJustify;
  WizardForm.WelcomeLabel1.Left := ScaleX(24);
  WizardForm.WelcomeLabel1.Width := WizardForm.WelcomePage.ClientWidth - ScaleX(48);
  WizardForm.WelcomeLabel2.Left := ScaleX(24);
  WizardForm.WelcomeLabel2.Width := WizardForm.WelcomePage.ClientWidth - ScaleX(48);
  WizardForm.FinishedHeadingLabel.Left := ScaleX(24);
  WizardForm.FinishedHeadingLabel.Width := WizardForm.FinishedPage.ClientWidth - ScaleX(48);
  WizardForm.FinishedLabel.Left := ScaleX(24);
  WizardForm.FinishedLabel.Width := WizardForm.FinishedPage.ClientWidth - ScaleX(48);
  InitPayload;
end;

function CheckUninstallTarget: Boolean;
var
  Message: String;
begin
  SetLength(Message, 1024);
  Result := CheckTargetUninstall(ExpandConstant('{app}'), False, Message, 1024);
  if not Result then SuppressibleMsgBox(BufferText(Message), mbError, MB_OK, IDOK);
end;

function InitializeUninstall: Boolean;
begin
  InitPayload;
  try
    Result := CheckUninstallTarget;
  finally
    UnloadDLL(InstallRoot + '\.inno\support.dll');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    try
      if not CheckUninstallTarget then RaiseException('请先正常关闭 RizomUV 后重试。');
    finally
      UnloadDLL(InstallRoot + '\.inno\support.dll');
    end;

    CleanupInstallerBackups;

  end;
end;
