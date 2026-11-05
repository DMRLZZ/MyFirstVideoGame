# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Compiles LEFTRIGHT with UnrealBuildTool (Build.bat).
.DESCRIPTION
	Runs <UE_ROOT>/Engine/Build/BatchFiles/Build.bat <Target> Win64 <Configuration> -Project=<uproject> -WaitMutex -NoHotReload,
	copies the output to Saved/Logs/build.log and exits with the UBT exit code.
.EXAMPLE
	Tools/Scripts/build.ps1
	Tools/Scripts/build.ps1 -Target LeftRight -Configuration Shipping
#>
[CmdletBinding()]
param(
	[ValidateSet('LeftRightEditor', 'LeftRight')]
	[string]$Target = 'LeftRightEditor',

	[ValidateSet('Development', 'DebugGame', 'Shipping')]
	[string]$Configuration = 'Development'
)

. "$PSScriptRoot\common.ps1"

if ($Target -eq 'LeftRightEditor' -and $Configuration -eq 'Shipping') {
	Stop-LRScript "The editor target cannot be built in Shipping. Use -Target LeftRight -Configuration Shipping."
}

$buildBat = Get-LREngineFile 'Build\BatchFiles\Build.bat'
$project = Get-LRProjectFile
$log = Join-Path (Get-LRLogDirectory) 'build.log'

$arguments = "$Target Win64 $Configuration -Project=`"$project`" -WaitMutex -NoHotReload"
Write-LRInfo "Building $Target Win64 $Configuration (log: $log)"

$started = Get-Date
$exitCode = Invoke-LRProcess -FilePath $buildBat -Arguments $arguments -LogFile $log
$elapsed = [int]((Get-Date) - $started).TotalSeconds

if ($exitCode -ne 0) {
	Write-LRError "Build FAILED with exit code $exitCode after $elapsed s. See $log"
	exit $exitCode
}
Write-LRInfo "Build succeeded in $elapsed s."
exit 0
