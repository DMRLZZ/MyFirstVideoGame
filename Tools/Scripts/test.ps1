# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Runs the LEFTRIGHT Automation Specs headless and summarizes the report.
.DESCRIPTION
	Runs <UE_ROOT>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests <Filter>; Quit"
	-unattended -nopause -nullrhi -NoSound -nosplash -log -ReportExportPath=<repo>/Saved/TestReports,
	then parses Saved/TestReports/index.json and prints passed / failed counts plus every failing test with its errors.
	Exits 1 when any test fails, when no test ran or when the report is missing.
	The editor target must be compiled first (Tools/Scripts/build.ps1).
.EXAMPLE
	Tools/Scripts/test.ps1
	Tools/Scripts/test.ps1 -Filter LeftRight.Core.
#>
[CmdletBinding()]
param(
	# Automation test name prefix (e.g. 'LeftRight.', 'LeftRight.Traffic.').
	[string]$Filter = 'LeftRight.'
)

. "$PSScriptRoot\common.ps1"

if ($Filter -match '[";]') {
	Stop-LRScript "The filter must not contain quotes or ';' (got '$Filter')."
}

$repo = Get-LRRepoRoot
$editorCmd = Get-LREngineFile 'Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Get-LRProjectFile
$reportDir = Join-Path $repo 'Saved\TestReports'
$log = Join-Path (Get-LRLogDirectory) 'test.log'

# Remove the previous report so a stale index.json is never mistaken for this run.
if (Test-Path -LiteralPath $reportDir) {
	Remove-Item -LiteralPath $reportDir -Recurse -Force -Confirm:$false
}
$reportDir = New-LRDirectory $reportDir

$arguments = "`"$project`" -ExecCmds=`"Automation RunTests $Filter; Quit`" -unattended -nopause -nullrhi -NoSound -nosplash -log -stdout -FullStdOutLogOutput -ReportExportPath=`"$reportDir`""

Write-LRInfo "Running automation tests '$Filter' (log: $log)"
$started = Get-Date
$editorExit = Invoke-LRProcess -FilePath $editorCmd -Arguments $arguments -LogFile $log
$elapsed = [int]((Get-Date) - $started).TotalSeconds
Write-LRInfo "Editor exited with code $editorExit after $elapsed s."

$indexPath = Join-Path $reportDir 'index.json'
if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) {
	Write-LRError "No test report found at '$indexPath'. The editor probably failed to start or crashed. See $log"
	exit 1
}

try {
	$report = ConvertFrom-LRJsonFile -Path $indexPath
}
catch {
	Write-LRError "Could not parse '$indexPath': $($_.Exception.Message)"
	exit 1
}

function Get-LRField($Object, [string]$Key, $Default) {
	if ($null -ne $Object -and $Object.ContainsKey($Key) -and $null -ne $Object[$Key]) { return $Object[$Key] }
	return $Default
}

$tests = @(Get-LRField $report 'tests' @())
$passed = 0
$passedWithWarnings = 0
$failed = 0
$notRun = 0
$failures = New-Object System.Collections.Generic.List[object]

foreach ($test in $tests) {
	$state = [string](Get-LRField $test 'state' '')
	$warnings = [int](Get-LRField $test 'warnings' 0)
	switch ($state) {
		'Success' {
			$passed++
			if ($warnings -gt 0) { $passedWithWarnings++ }
		}
		'Fail' {
			$failed++
			$failures.Add($test)
		}
		default { $notRun++ }
	}
}

# Fall back to the report counters when the per-test list is absent.
if ($tests.Count -eq 0) {
	$passed = [int](Get-LRField $report 'succeeded' 0) + [int](Get-LRField $report 'succeededWithWarnings' 0)
	$passedWithWarnings = [int](Get-LRField $report 'succeededWithWarnings' 0)
	$failed = [int](Get-LRField $report 'failed' 0)
	$notRun = [int](Get-LRField $report 'notRun' 0)
}

Write-Host ''
Write-Host '================ Automation report ================'
Write-Host ("Filter : {0}" -f $Filter)
Write-Host ("Passed : {0} ({1} with warnings)" -f $passed, $passedWithWarnings) -ForegroundColor Green
if ($failed -gt 0) {
	Write-Host ("Failed : {0}" -f $failed) -ForegroundColor Red
}
else {
	Write-Host ("Failed : {0}" -f $failed)
}
Write-Host ("Not run: {0}" -f $notRun)
Write-Host ("Report : {0}" -f $indexPath)

foreach ($test in $failures) {
	$name = [string](Get-LRField $test 'fullTestPath' (Get-LRField $test 'testDisplayName' '<unnamed>'))
	Write-Host ''
	Write-Host "FAIL $name" -ForegroundColor Red
	foreach ($entry in @(Get-LRField $test 'entries' @())) {
		$evt = Get-LRField $entry 'event' $null
		if ($null -eq $evt) { continue }
		$type = [string](Get-LRField $evt 'type' '')
		if ($type -ne 'Error') { continue }
		$message = [string](Get-LRField $evt 'message' '')
		$file = [string](Get-LRField $entry 'filename' '')
		$line = [int](Get-LRField $entry 'lineNumber' 0)
		if ($file -ne '') {
			Write-Host ("    {0} ({1}:{2})" -f $message, $file, $line) -ForegroundColor Red
		}
		else {
			Write-Host ("    {0}" -f $message) -ForegroundColor Red
		}
	}
}
Write-Host '==================================================='

if (($passed + $failed) -eq 0) {
	Write-LRError "No tests ran for filter '$Filter'. Check the test names (LeftRight.<Area>.<Case>) and that the editor target is compiled."
	exit 1
}
if ($failed -gt 0) {
	exit 1
}
Write-LRInfo "All tests passed."
exit 0
