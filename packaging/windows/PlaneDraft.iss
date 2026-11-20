; SPDX-License-Identifier: LGPL-2.1-or-later
; Payload, Output and AppVersion are supplied by make-installer.ps1. Never harvest the build tree.
[Setup]
AppId={{CE740F19-5B5B-4B60-8092-0BE3DF39E9F5}
AppName=PlaneDraft 2D
AppVersion={#AppVersion}
AppPublisher=PlaneDraft contributors
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={localappdata}\Programs\PlaneDraft
DefaultGroupName=PlaneDraft
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\bin\PlaneDraft.exe
ChangesAssociations=yes
OutputDir={#Output}
OutputBaseFilename=PlaneDraft-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.DxfDescription=View and edit DXF drawings
russian.DxfDescription=Просмотр и редактирование чертежей DXF
english.DxfTask=Offer PlaneDraft in Open with for .dxf drawings
russian.DxfTask=Добавить PlaneDraft в меню «Открыть с помощью» для чертежей .dxf

[Tasks]
Name: "assocdxf"; Description: "{cm:DxfTask}"; Flags: unchecked

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; Also retain the notice of the compiler actually used to generate the installer engine.
Source: "{#CompilerPath}\license.txt"; DestDir: "{app}\LICENSES"; DestName: "Inno-Setup.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\PlaneDraft 2D"; Filename: "{app}\bin\PlaneDraft.exe"

[Registry]
; Opt-in, per-user. Don't overwrite another CAD's default or Windows UserChoice.
Root: HKCU; Subkey: "Software\Classes\PlaneDraft.Drawing"; ValueType: string; ValueName: ""; ValueData: "PlaneDraft DXF"; Flags: uninsdeletekey; Tasks: assocdxf
Root: HKCU; Subkey: "Software\Classes\PlaneDraft.Drawing\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\PlaneDraft.exe,0"; Tasks: assocdxf
Root: HKCU; Subkey: "Software\Classes\PlaneDraft.Drawing\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\PlaneDraft.exe"" ""%1"""; Tasks: assocdxf
Root: HKCU; Subkey: "Software\Classes\.dxf\OpenWithProgids"; ValueType: string; ValueName: "PlaneDraft.Drawing"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocdxf
Root: HKCU; Subkey: "Software\PlaneDraft\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "PlaneDraft 2D"; Flags: uninsdeletekey; Tasks: assocdxf
Root: HKCU; Subkey: "Software\PlaneDraft\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:DxfDescription}"; Tasks: assocdxf
Root: HKCU; Subkey: "Software\PlaneDraft\Capabilities\FileAssociations"; ValueType: string; ValueName: ".dxf"; ValueData: "PlaneDraft.Drawing"; Tasks: assocdxf
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "PlaneDraft 2D"; ValueData: "Software\PlaneDraft\Capabilities"; Flags: uninsdeletevalue; Tasks: assocdxf
