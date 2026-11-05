# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Builds, cooks, stages, paks and archives a Win64 build with RunUAT BuildCookRun.
.DESCRIPTION
	Runs <UE_ROOT>/Engine/Build/BatchFiles/RunUAT.bat BuildCookRun -project=<uproject> -platform=Win64
	-clientconfig=<Configuration> -build -cook -stage -pak -archive -archivedirectory=<repo>/Packaged.
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

$arguments = "BuildCookRun -project=`"$project`" -platform=Win64 -clientconfig=$Configuration -build -cook -stage -pak -archive -archivedirectory=`"$ArchiveDirectory`" -noP4 -utf8output -unattended"

Write-LRInfo "Packaging Win64 $Configuration into $ArchiveDirectory (log: $log)"
$started = Get-Date
$exitCode = Invoke-LRProcess -FilePath $runUat -Arguments $arguments -LogFile $log
$elapsed = [int]((Get-Date) - $started).TotalSeconds

if ($exitCode -ne 0) {
	Write-LRError "Packaging FAILED with exit code $exitCode after $elapsed s. See $log"
	exit $exitCode
}
Write-LRInfo "Packaging succeeded in $elapsed s. Output: $ArchiveDirectory"
exit 0
