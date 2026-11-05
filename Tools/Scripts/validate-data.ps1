# Copyright LEFTRIGHT. All Rights Reserved.
<#
.SYNOPSIS
	Validates every Data/**/*.json file against Data/Schema (no engine needed).
.DESCRIPTION
	Thin wrapper over 'python -I Tools/Data/validate_data.py'. Requires Python 3 with the packages of
	Tools/Data/requirements.txt (pip install -r Tools/Data/requirements.txt). Exits 1 on validation errors.
.EXAMPLE
	Tools/Scripts/validate-data.ps1
	Tools/Scripts/validate-data.ps1 -Strict
#>
[CmdletBinding()]
param(
	# Treat warnings (e.g. missing schema for a type) as errors.
	[switch]$Strict
)

. "$PSScriptRoot\common.ps1"

$repo = Get-LRRepoRoot
$validator = Join-Path $repo 'Tools\Data\validate_data.py'
if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) {
	Stop-LRScript "Validator not found: '$validator'."
}

# Prefer 'python', fall back to the Windows 'py -3' launcher.
$pythonExe = $null
$pythonPrefix = @()
$cmd = Get-Command python -ErrorAction SilentlyContinue
if ($null -ne $cmd) {
	$pythonExe = $cmd.Source
}
else {
	$cmd = Get-Command py -ErrorAction SilentlyContinue
	if ($null -ne $cmd) {
		$pythonExe = $cmd.Source
		$pythonPrefix = @('-3')
	}
}
if ($null -eq $pythonExe) {
	Stop-LRScript "Python 3 not found in PATH. Install Python 3.10+ and run: pip install -r Tools/Data/requirements.txt"
}

# -I (isolated mode): ignore PYTHON* env vars, user site-packages and the script directory on sys.path.
$pyArgs = @()
$pyArgs += $pythonPrefix
$pyArgs += @('-I', $validator, '--repo-root', $repo)
if ($Strict) { $pyArgs += '--strict' }

& $pythonExe @pyArgs
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
	Write-LRError "Data validation failed (exit code $exitCode)."
}
exit $exitCode
