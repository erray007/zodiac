; TEST FIXTURE, never shipped: packaging/zodiac.iss exactly as it was at commit 1ad1bb7, before the setup was renamed to Zodiac Game Toolkit.
; CI (kit-windows) compiles it into zodiac-old-setup.exe so kit-clean-machine can install the old-named setup and upgrade it in place with the real one.
; Compile with: ISCC.exe /DZodiacVersion=<version> /DKitDir=<dist\Zodiac> /O<output folder> packaging\test\zodiac-old-name.iss

#ifndef ZodiacVersion
  #error Define ZodiacVersion, for example /DZodiacVersion=1.2.3
#endif
#ifndef KitDir
  #error Define KitDir, the installed kit folder, for example /DKitDir=D:\zodiac\dist\Zodiac
#endif

[Setup]
; Fixed forever: upgrades find the earlier install through it and replace it in place.
AppId={{98CB20B5-1340-41F1-80E7-8FD0E99FA8BC}
AppName=Zodiac
AppVersion={#ZodiacVersion}
AppVerName=Zodiac {#ZodiacVersion}
AppPublisher=Zodiac
VersionInfoVersion={#ZodiacVersion}
VersionInfoDescription=Zodiac setup
; "Only for me" by default (no administrator rights); the first page lets the user choose "for all users" instead.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\Zodiac
DisableProgramGroupPage=yes
WizardStyle=modern
ShowLanguageDialog=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesEnvironment=yes
ChangesAssociations=yes
UsePreviousTasks=yes
UninstallDisplayName=Zodiac {#ZodiacVersion}
UninstallDisplayIcon={app}\bin\zodiac.exe
OutputBaseFilename=zodiac-old-setup-{#ZodiacVersion}
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add Zodiac to the PATH, so the zodiac command works in every new terminal"
Name: "associate"; Description: "Open .zs files with Zodiac: double-clicking a .zs file runs it"
Name: "buildmenu"; Description: "Add ""Build with Zodiac"" to the right-click menu of .zs files"
Name: "shortcuts"; Description: "Add Start menu shortcuts to the Zodiac manual and examples"

[InstallDelete]
; An upgrade replaces the kit's own folders, so no file of an older version (an old module, library or DLL) survives in them.
; Only over an earlier Zodiac install in the same folder: a first install into an existing folder never deletes anything.
Type: filesandordirs; Name: "{app}\bin"; Check: IsUpgradeInPlace
Type: filesandordirs; Name: "{app}\lib"; Check: IsUpgradeInPlace
Type: filesandordirs; Name: "{app}\modules"; Check: IsUpgradeInPlace
Type: filesandordirs; Name: "{app}\include"; Check: IsUpgradeInPlace

[Files]
Source: "{#KitDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
; HKA is HKLM\Software\Classes for all users and HKCU\Software\Classes for only me.
; Software\Zodiac holds what the setup must know to undo exactly its own changes (see [Code]); uninstall deletes it.
Root: HKA; Subkey: "Software\Zodiac"; Flags: uninsdeletekey
; Uninstall gives the .zs default back to the program that had it before (see [Code]), so it is not an uninsdeletevalue entry.
Root: HKA; Subkey: "Software\Classes\.zs"; ValueType: string; ValueName: ""; ValueData: "Zodiac.Script"; Flags: uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.zs\OpenWithProgids"; ValueType: string; ValueName: "Zodiac.Script"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Zodiac.Script"; ValueType: string; ValueName: ""; ValueData: "Zodiac script"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Zodiac.Script\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\zodiac.exe"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Zodiac.Script\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\zodiac.exe"" run ""%1"""; Tasks: associate
; The verb hangs off SystemFileAssociations, so it is there for every .zs file whichever program opens them.
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zs"; Flags: uninsdeletekeyifempty; Tasks: buildmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zs\shell"; Flags: uninsdeletekeyifempty; Tasks: buildmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zs\shell\zodiacbuild"; ValueType: string; ValueName: ""; ValueData: "Build with Zodiac"; Flags: uninsdeletekey; Tasks: buildmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zs\shell\zodiacbuild"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\bin\zodiacw.exe"""; Tasks: buildmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.zs\shell\zodiacbuild\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\zodiacw.exe"" build ""%1"""; Tasks: buildmenu

[Icons]
Name: "{autoprograms}\Zodiac\Zodiac manual"; Filename: "{app}\docs"; Tasks: shortcuts
Name: "{autoprograms}\Zodiac\Zodiac examples"; Filename: "{app}\examples"; Tasks: shortcuts

[Code]
const
  StateKey = 'Software\Zodiac';
  ZsKey = 'Software\Classes\.zs';
  ZodiacProgId = 'Zodiac.Script';

function RootKey: Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function PathRoot: Integer;
begin
  Result := RootKey;
end;

function PathKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

function SamePathEntry(const A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(Trim(A)), RemoveBackslashUnlessRoot(Trim(B))) = 0;
end;

// True when this setup upgrades an earlier Zodiac install (same AppId) in the folder being installed to.
function IsUpgradeInPlace: Boolean;
begin
  Result := (WizardForm.PrevAppDir <> '') and SamePathEntry(WizardForm.PrevAppDir, ExpandConstant('{app}'));
end;

// Rebuilds Path without every entry equal to Dir, keeping every other entry (even an empty one) exactly as it was, in order.
function PathWithout(const Path, Dir: String; var Found: Boolean): String;
var
  Rest, Entry: String;
  Separator: Integer;
  First, Last: Boolean;
begin
  Result := '';
  Found := False;
  Rest := Path;
  First := True;
  Last := Path = '';
  while not Last do
  begin
    Separator := Pos(';', Rest);
    if Separator = 0 then
    begin
      Entry := Rest;
      Last := True;
    end
    else
    begin
      Entry := Copy(Rest, 1, Separator - 1);
      Delete(Rest, 1, Separator);
    end;
    if SamePathEntry(Entry, Dir) then
      Found := True
    else
    begin
      if not First then
        Result := Result + ';';
      Result := Result + Entry;
      First := False;
    end;
  end;
end;

procedure WritePath(const Value: String);
begin
  if Value = '' then
    RegDeleteValue(PathRoot, PathKey, 'Path')
  else if not RegWriteExpandStringValue(PathRoot, PathKey, 'Path', Value) then
    Log('Could not write the PATH');
end;

procedure AddToPath(const Dir: String);
var
  Path, Others: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(PathRoot, PathKey, 'Path', Path) then
    Path := '';
  Others := PathWithout(Path, Dir, Found);
  if Found then
    Exit;
  // Not found, so Others is Path unchanged. A Path ending in ';' keeps that empty last entry after Dir, so uninstall gives back exactly the old text.
  if Others = '' then
    WritePath(Dir)
  else if Others[Length(Others)] = ';' then
    WritePath(Others + Dir + ';')
  else
    WritePath(Others + ';' + Dir);
  if not RegWriteDWordValue(RootKey, StateKey, 'AddedToPath', 1) then
    Log('Could not record that the PATH entry was added');
end;

// Removes Dir from PATH only if this setup added it (AddedToPath=1), then clears the marker.
procedure RemoveAddedPathEntry(const Dir: String);
var
  Path, Others: String;
  Added: Cardinal;
  Found: Boolean;
begin
  if not (RegQueryDWordValue(RootKey, StateKey, 'AddedToPath', Added) and (Added = 1)) then
    Exit;
  if RegQueryStringValue(PathRoot, PathKey, 'Path', Path) then
  begin
    Others := PathWithout(Path, Dir, Found);
    if Found then
      WritePath(Others);
  end;
  RegDeleteValue(RootKey, StateKey, 'AddedToPath');
end;

// Before the [Registry] entries are written: save a .zs default that belongs to another program (an upgrade finds Zodiac.Script and keeps the saved one).
procedure SaveOtherZsProgId;
var
  Current: String;
begin
  if not WizardIsTaskSelected('associate') then
    Exit;
  if RegQueryStringValue(RootKey, ZsKey, '', Current) and (Current <> '') and (CompareText(Current, ZodiacProgId) <> 0) then
    if not RegWriteStringValue(RootKey, StateKey, 'PrevZsProgId', Current) then
      Log('Could not save the previous .zs ProgID');
end;

procedure RestoreZsProgId;
var
  Current, Previous: String;
begin
  if RegQueryStringValue(RootKey, ZsKey, '', Current) and (CompareText(Current, ZodiacProgId) = 0) then
  begin
    if RegQueryStringValue(RootKey, StateKey, 'PrevZsProgId', Previous) and (Previous <> '') then
      RegWriteStringValue(RootKey, ZsKey, '', Previous)
    else
      RegDeleteValue(RootKey, ZsKey, '');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    SaveOtherZsProgId
  else if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('addtopath') then
      AddToPath(ExpandConstant('{app}\bin'))
    else
      RemoveAddedPathEntry(ExpandConstant('{app}\bin'));
  end;
end;

// usUninstall comes before the uninstaller deletes the registry entries it logged (Software\Zodiac among them), so the saved state is still there.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveAddedPathEntry(ExpandConstant('{app}\bin'));
    RestoreZsProgId;
  end;
end;
