# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Launches the game (uncooked, -game) in a 1600x900 window.
.DESCRIPTION
	Runs <UE_ROOT>/Engine/Binaries/Win64/UnrealEditor.exe <uproject> -game -windowed -ResX=1600 -ResY=900 -log.
	The editor target must be compiled first (Tools/Scripts/build.ps1).
.EXAMPLE
	Tools/Scripts/play.ps1
	Tools/Scripts/play.ps1 -ResX 1920 -ResY 1080 -Wait
#>
[CmdletBinding()]
param(
	[ValidateRange(320, 7680)]
	[int]$ResX = 1600,

	[ValidateRange(240, 4320)]
	[int]$ResY = 900,

	# Wait for the game to close and return its exit code.
	[switch]$Wait,

	# Extra arguments appended verbatim to the command line (e.g. a map name or -ExecCmds=...).
	[string]$ExtraArgs = ''
)

. "$PSScriptRoot\common.ps1"

$editor = Get-LREngineFile 'Binaries\Win64\UnrealEditor.exe'
$project = Get-LRProjectFile

$arguments = "`"$project`" -game -windowed -ResX=$ResX -ResY=$ResY -log"
if (-not [string]::IsNullOrWhiteSpace($ExtraArgs)) { $arguments += " $ExtraArgs" }

Write-LRInfo "> `"$editor`" $arguments"
if ($Wait) {
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory (Get-LRRepoRoot) -PassThru -Wait
	exit $process.ExitCode
}
Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory (Get-LRRepoRoot) | Out-Null
Write-LRInfo "Game launched."
exit 0
