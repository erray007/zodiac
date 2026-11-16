; The Zodiac setup wizard (spec 8.7). Built by the CMake target zodiac_installer through tools/build_installer.py:
;   ISCC.exe /DZodiacVersion=<version> /DKitDir=<dist\Zodiac> [/DSign=1 /Szodiacsign=<command>] zodiac.iss
; Only standard Inno Setup pages and controls are used, so the whole wizard works with a screen reader and the keyboard alone.
; Uninstall removes exactly what this script adds: the kit files, the PATH entry, the .zs association, the right-click verb and the shortcuts.

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
OutputBaseFilename=zodiac-setup-{#ZodiacVersion}
Compression=lzma2/max
SolidCompression=yes
#ifdef Sign
SignTool=zodiacsign
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add Zodiac to the PATH, so the zodiac command works in every new terminal"
Name: "associate"; Description: "Open .zs files with Zodiac: double-clicking a .zs file runs it"
Name: "buildmenu"; Description: "Add ""Build with Zodiac"" to the right-click menu of .zs files"
Name: "shortcuts"; Description: "Add Start menu shortcuts to the Zodiac manual and examples"

[InstallDelete]
; An upgrade replaces the kit's own folders, so no file of an older version (an old module, library or DLL) survives in them.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\modules"
Type: filesandordirs; Name: "{app}\include"

[Files]
Source: "{#KitDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
; HKA is HKLM\Software\Classes for all users and HKCU\Software\Classes for only me.
Root: HKA; Subkey: "Software\Classes\.zs"; ValueType: string; ValueName: ""; ValueData: "Zodiac.Script"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
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
// The PATH entry is {app}\bin, in the user's Environment key (only me) or the system one (all users).
// It is added once (never twice, compared without case and trailing backslash), removed when an upgrade unticks addtopath, and removed on uninstall.

function PathRoot: Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
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
  if (Others = '') or (Others[Length(Others)] = ';') then
    WritePath(Others + Dir)
  else
    WritePath(Others + ';' + Dir);
end;

procedure RemoveFromPath(const Dir: String);
var
  Path, Others: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(PathRoot, PathKey, 'Path', Path) then
    Exit;
  Others := PathWithout(Path, Dir, Found);
  if Found then
    WritePath(Others);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('addtopath') then
      AddToPath(ExpandConstant('{app}\bin'))
    else
      RemoveFromPath(ExpandConstant('{app}\bin'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
