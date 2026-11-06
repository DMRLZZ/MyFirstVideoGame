<#
.SYNOPSIS
	Runs the game with the development autopilot and prints a telemetry summary.

.DESCRIPTION
	Launches the game in a window with -LRDevDrive (see ULRDevAutopilotSubsystem). It plays by itself, restarts after
	crashes, writes screenshots to Saved/DevShots and quits. Use it to check real gameplay without touching the keyboard.

.EXAMPLE
	Tools/Scripts/devdrive.ps1 -Seconds 120 -ShotEvery 15 -CycleCameras
#>
param(
	[int]$Seconds = 120,
	[int]$ShotEvery = 15,
	[switch]$CycleCameras
)

. (Join-Path $PSScriptRoot 'common.ps1')

$UeRoot = Get-LRUeRoot
$Project = Get-LRProjectFile
$RepoRoot = Split-Path -Parent $Project
$Editor = Join-Path $UeRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$ShotDir = Join-Path $RepoRoot 'Saved\DevShots'
if (Test-Path $ShotDir) { Remove-Item -Recurse -Force $ShotDir -Confirm:$false }

$GameArgs = @("`"$Project`"", '-game', '-windowed', '-ResX=1280', '-ResY=720', '-log', '-nosplash', '-unattended',
	'-LRDevDrive', "-LRDevSeconds=$Seconds", "-LRDevShotEvery=$ShotEvery")
if ($CycleCameras) { $GameArgs += '-LRDevCycleCameras' }

$Process = Start-Process -FilePath $Editor -ArgumentList $GameArgs -PassThru
$Process.WaitForExit()

$Log = Join-Path $RepoRoot 'Saved\Logs\LeftRight.log'
Select-String -Path $Log -Pattern 'DevTelemetry|DevAutopilot|Handled ensure|Fatal error' | ForEach-Object { $_.Line }
Write-Output "Screenshots: $ShotDir"
exit $Process.ExitCode
