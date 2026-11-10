# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Builds, cooks, stages, paks and archives a Win64 build with RunUAT BuildCookRun.
.DESCRIPTION
	Runs <UE_ROOT>/Engine/Build/BatchFiles/RunUAT.bat BuildCookRun for Win64 and archives the result
	in <repo>/Packaged. The full argument list is built in one place below and printed before UAT
	starts, so this script is the single reference for the packaging command.

	The build carries the Visual C++ runtime installers (vc_redist.x64.exe and
	vc_redist.arm64.exe under Engine/Extras/Redist, about 30 MB). Only the bootstrap launcher at the
	root of the build (<archive>/Windows/LeftRight.exe) checks for the runtime and offers to install
	it; starting Binaries/Win64/LeftRight-Win64-Shipping.exe directly on a PC without the runtime
	fails with a missing DLL error and no prompt.

	Logs to Saved/Logs/package.log and propagates the exit code.
.EXAMPLE
	Tools/Scripts/package.ps1
	Tools/Scripts/package.ps1 -Configuration Development
#>
[CmdletBinding()]
param(
	[ValidateSet('Shipping', 'Development', 'DebugGame')]
	[string]$Configuration = 'Shipping',

	# Output folder for the archived build (default <repo>/Packaged).
	[string]$ArchiveDirectory = ''
)

. "$PSScriptRoot\common.ps1"

$repo = Get-LRRepoRoot
if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) {
	$ArchiveDirectory = Join-Path $repo 'Packaged'
}
$ArchiveDirectory = New-LRDirectory $ArchiveDirectory

$runUat = Get-LREngineFile 'Build\BatchFiles\RunUAT.bat'
$project = Get-LRProjectFile
$log = Join-Path (Get-LRLogDirectory) 'package.log'

$argumentList = @(
	'BuildCookRun'
	"-project=`"$project`""
	'-platform=Win64'
	"-clientconfig=$Configuration"
	'-build'
	'-cook'
	'-stage'
	'-pak'
	'-prereqs'
	'-archive'
	"-archivedirectory=`"$ArchiveDirectory`""
	'-noP4'
	'-utf8output'
	'-unattended'
)
$arguments = $argumentList -join ' '

Write-LRInfo "Packaging Win64 $Configuration into $ArchiveDirectory (log: $log)"
Write-LRInfo "RunUAT $arguments"
$started = Get-Date
$exitCode = Invoke-LRProcess -FilePath $runUat -Arguments $arguments -LogFile $log
$elapsed = [int]((Get-Date) - $started).TotalSeconds

if ($exitCode -ne 0) {
	Write-LRError "Packaging FAILED with exit code $exitCode after $elapsed s. See $log"
	exit $exitCode
}
Write-LRInfo "Packaging succeeded in $elapsed s. Output: $ArchiveDirectory"
exit 0
