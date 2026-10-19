{ Vendored common installer policy; see INSTALLER_POLICY.md. }
var
  InstallWarnings: String;
  AssociationReady: Boolean;
  LegacyReady: Boolean;

procedure AddInstallWarning(Detail: String);
begin
  if Detail = '' then Exit;
  Log('Optional operation warning: ' + Detail);
  if Pos(Detail, InstallWarnings) = 0 then begin
    if InstallWarnings <> '' then InstallWarnings := InstallWarnings + #13#10;
    InstallWarnings := InstallWarnings + Detail;
  end;
end;

procedure FinishInstallPolicy;
var Summary: String;
begin
  { Persist for suite aggregation and support, without changing core success. }
  Summary := InstallWarnings;
  StringChangeEx(Summary, #13#10, ' | ', True);
  if not SetIniString('Install', 'Warnings', Summary, StateFile) then
    AddInstallWarning('无法保存附加功能警告，请保留安装日志。');
end;

<event('CurPageChanged')>
procedure InstallPolicyPageChanged(CurPageID: Integer);
begin
  if CurPageID <> wpFinished then Exit;
  if InstallWarnings <> '' then begin
    WizardForm.FinishedHeadingLabel.Caption := '安装成功，部分附加功能未完成';
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      InstallWarnings + #13#10 + '可使用中文快捷方式启动；详情请查看安装日志。';
  end;
end;

function ForceInstallRequested: Boolean;
var I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/FORCEINSTALL') = 0 then Result := True;
end;

function EnsureInstallStopped(Directory: String): String;
var State, Answer: Integer; Message: String; Stopped: Cardinal;
begin
  Result := '';
  repeat
    Result := '';
    SetLength(Message, 1024);
    if not CheckTargetEx(Directory, True, False, Message, 1024) then begin
      Result := BufferText(Message);
      Exit;
    end;
    State := QueryInstallProcesses(Directory);
    if State = 0 then Exit;
    if State < 0 then begin
      Result := '无法确认目标软件的运行状态，请检查进程访问权限后重新检测。';
      if WizardSilent then Exit;
      if SuppressibleMsgBox(Result, mbError, MB_RETRYCANCEL, IDCANCEL) = IDRETRY then Continue;
      Exit;
    end;
    Answer := IDCANCEL;
    if ForceInstallRequested then Answer := IDYES
    else if not WizardSilent then
      Answer := SuppressibleMsgBox('所选目录的软件或中文启动器正在运行。' + #13#10#13#10 +
        '强制关闭会丢失未保存的工程，请先保存。' + #13#10 +
        '是：强制关闭并继续；否：重新检测；取消：返回安装向导。',
        mbConfirmation, MB_YESNOCANCEL or MB_DEFBUTTON2, IDCANCEL);
    if Answer = IDNO then Continue;
    if Answer <> IDYES then begin
      Result := '目标软件仍在运行，尚未替换汉化文件。保存并关闭后可重新安装。';
      Exit;
    end;
    Stopped := 0;
    SetLength(Message, 1024);
    if not StopInstallProcesses(Directory, Stopped, Message, 1024) then begin
      Result := BufferText(Message);
      if WizardSilent then Exit;
      if SuppressibleMsgBox(Result, mbError, MB_RETRYCANCEL, IDCANCEL) = IDRETRY then Continue;
      Exit;
    end;
    Log('Explicit force-close consent; target processes stopped: ' + IntToStr(Stopped));
    { Revalidate identity, compatibility and state after closing. }
  until False;
end;
