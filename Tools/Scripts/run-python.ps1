# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Runs an editor Python script headless (UnrealEditor-Cmd -run=pythonscript).
.DESCRIPTION
	Executes <UE_ROOT>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<Script>" -unattended,
	logs to Saved/Logs/run-python.log and propagates the exit code.
	Requires the 'Python Editor Script Plugin' (PythonScriptPlugin) to be enabled for the project.
	Only run reviewed scripts from Tools/Editor/.
.EXAMPLE
	Tools/Scripts/run-python.ps1 -Script Tools/Editor/create_materials.py
#>
[CmdletBinding()]
param(
	# Path to the .py script, absolute or relative to the repository root.
	[Parameter(Mandatory = $true)]
	[string]$Script,

	# Extra arguments appended verbatim to the editor command line.
	[string]$ExtraArgs = ''
)

. "$PSScriptRoot\common.ps1"

$repo = Get-LRRepoRoot
$scriptPath = $Script
if (-not [System.IO.Path]::IsPathRooted($scriptPath)) {
	$scriptPath = Join-Path $repo $scriptPath
}
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
	Stop-LRScript "Python script not found: '$scriptPath'."
}
$scriptPath = (Resolve-Path -LiteralPath $scriptPath).ProviderPath

$editorCmd = Get-LREngineFile 'Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Get-LRProjectFile
$log = Join-Path (Get-LRLogDirectory) 'run-python.log'

$arguments = "`"$project`" -run=pythonscript -script=`"$scriptPath`" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput"
if (-not [string]::IsNullOrWhiteSpace($ExtraArgs)) { $arguments += " $ExtraArgs" }

$exitCode = Invoke-LRProcess -FilePath $editorCmd -Arguments $arguments -LogFile $log
if ($exitCode -ne 0) {
	Write-LRError "Python script failed with exit code $exitCode. See $log"
	exit $exitCode
}
Write-LRInfo "Python script finished OK."
exit 0
