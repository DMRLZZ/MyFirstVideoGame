# Copyright LEFTRIGHT. All Rights Reserved.
#
# Shared helpers for the Tools/Scripts/*.ps1 wrappers. Dot-source it:
#     . "$PSScriptRoot\common.ps1"
#
# Compatible with Windows PowerShell 5.1 (no '&&', '||' or '??' operators).

Set-StrictMode -Version 2.0

$script:LRDefaultUERoot = 'C:\Program Files\Epic Games\UE_5.8'

function Write-LRInfo([string]$Message)  { Write-Host "[LR] $Message" -ForegroundColor Cyan }
function Write-LRWarn([string]$Message)  { Write-Host "[LR] WARNING: $Message" -ForegroundColor Yellow }
function Write-LRError([string]$Message) { Write-Host "[LR] ERROR: $Message" -ForegroundColor Red }

function Stop-LRScript {
	<# Prints an error and terminates the calling script with the given exit code. #>
	param([string]$Message, [int]$ExitCode = 1)
	Write-LRError $Message
	exit $ExitCode
}

function Get-LRRepoRoot {
	<# Absolute path of the repository root (two levels above Tools/Scripts). #>
	return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
}

function Get-LRUERoot {
	<# Unreal Engine install root: $env:UE_ROOT, or the default UE 5.8 location. Fails with a clear message. #>
	$root = $env:UE_ROOT
	$fromEnv = $true
	if ([string]::IsNullOrWhiteSpace($root)) {
		$root = $script:LRDefaultUERoot
		$fromEnv = $false
	}
	$root = $root.Trim().Trim('"').TrimEnd('\', '/')
	if (-not (Test-Path -LiteralPath (Join-Path $root 'Engine') -PathType Container)) {
		if ($fromEnv) {
			Stop-LRScript "UE_ROOT='$root' does not contain an 'Engine' folder. Point `$env:UE_ROOT at the Unreal Engine 5.8 install (e.g. '$script:LRDefaultUERoot')."
		}
		Stop-LRScript "Unreal Engine not found at '$root'. Install UE 5.8 there or set `$env:UE_ROOT to the engine install folder."
	}
	return $root
}

function Get-LRProjectFile {
	<# Absolute path of LeftRight.uproject at the repository root. Fails with a clear message. #>
	$path = Join-Path (Get-LRRepoRoot) 'LeftRight.uproject'
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		Stop-LRScript "Project file not found: '$path'. Run the scripts from a LEFTRIGHT checkout."
	}
	return $path
}

function Get-LREngineFile {
	<# Absolute path of a file inside <UE_ROOT>/Engine. Fails with a clear message when it does not exist. #>
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path (Join-Path (Get-LRUERoot) 'Engine') $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		Stop-LRScript "Engine file not found: '$path'. Is the engine fully installed (and compiled, for source builds)?"
	}
	return $path
}

function New-LRDirectory {
	<# Creates a directory (and parents) if missing; returns its absolute path. #>
	param([Parameter(Mandatory = $true)][string]$Path)
	if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
		New-Item -ItemType Directory -Force -Path $Path | Out-Null
	}
	return (Resolve-Path -LiteralPath $Path).ProviderPath
}

function Get-LRLogDirectory {
	<# <repo>/Saved/Logs, created on demand. #>
	return New-LRDirectory (Join-Path (Get-LRRepoRoot) 'Saved\Logs')
}

function Invoke-LRProcess {
	<#
	Runs a native program with a verbatim argument string, streams its output to the console,
	optionally copies it to a log file and returns the process exit code.

	A single pre-quoted argument string is used on purpose: Windows PowerShell 5.1 mangles native
	arguments that contain embedded quotes (e.g. -ExecCmds="Automation RunTests X; Quit").
	Batch files (.bat/.cmd) are launched through 'cmd.exe /d /s /c' so paths with spaces survive.
	#>
	param(
		[Parameter(Mandatory = $true)][string]$FilePath,
		[string]$Arguments = '',
		[string]$LogFile = ''
	)

	$ext = [System.IO.Path]::GetExtension($FilePath).ToLowerInvariant()
	$exe = $FilePath
	$argLine = $Arguments
	if ($ext -eq '.bat' -or $ext -eq '.cmd') {
		$exe = $env:ComSpec
		if ([string]::IsNullOrWhiteSpace($exe)) { $exe = 'cmd.exe' }
		$argLine = '/d /s /c ""' + $FilePath + '" ' + $Arguments + '"'
	}

	Write-LRInfo "> `"$FilePath`" $Arguments"

	$psi = New-Object System.Diagnostics.ProcessStartInfo
	$psi.FileName = $exe
	$psi.Arguments = $argLine
	$psi.UseShellExecute = $false
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.CreateNoWindow = $false
	$psi.WorkingDirectory = Get-LRRepoRoot

	$writer = $null
	if (-not [string]::IsNullOrWhiteSpace($LogFile)) {
		New-LRDirectory (Split-Path -Parent $LogFile) | Out-Null
		$writer = New-Object System.IO.StreamWriter($LogFile, $false, (New-Object System.Text.UTF8Encoding($false)))
		$writer.AutoFlush = $true
	}

	$process = New-Object System.Diagnostics.Process
	$process.StartInfo = $psi
	try {
		if (-not $process.Start()) {
			Stop-LRScript "Could not start '$FilePath'."
		}
		# Drain stderr asynchronously so neither pipe can fill up and block the child.
		$stderrTask = $process.StandardError.ReadToEndAsync()
		while ($true) {
			$line = $process.StandardOutput.ReadLine()
			if ($null -eq $line) { break }
			Write-Host $line
			if ($null -ne $writer) { $writer.WriteLine($line) }
		}
		$process.WaitForExit()
		$stderr = $stderrTask.Result
		if (-not [string]::IsNullOrEmpty($stderr)) {
			Write-Host $stderr -ForegroundColor DarkYellow
			if ($null -ne $writer) { $writer.Write($stderr) }
		}
		return $process.ExitCode
	}
	finally {
		if ($null -ne $writer) { $writer.Dispose() }
		$process.Dispose()
	}
}

function ConvertFrom-LRJsonFile {
	<#
	Reads a (possibly large, possibly BOM-prefixed) JSON file into nested dictionaries / lists,
	accessed with $obj['key']. Works on Windows PowerShell 5.1 (whose ConvertFrom-Json chokes on
	files over ~2 MB) and on PowerShell 7+.
	#>
	param([Parameter(Mandatory = $true)][string]$Path)
	$text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
	if ($PSVersionTable.PSVersion.Major -ge 6) {
		return ($text | ConvertFrom-Json -AsHashtable)
	}
	Add-Type -AssemblyName System.Web.Extensions
	$serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
	$serializer.MaxJsonLength = [int]::MaxValue
	$serializer.RecursionLimit = 256
	return $serializer.DeserializeObject($text)
}
