<#
User-invoked local beta launcher. CheckOnly performs validation without starting
anything. This does not install files, change settings, or alter launcher state.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][string]$ModDir,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedPerfSha256,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedIniSha256,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
function Get-BetaHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

try {
    $gamePath = (Resolve-Path -LiteralPath $GameDir).ProviderPath
    $modPath = (Resolve-Path -LiteralPath $ModDir).ProviderPath
    $exe = Join-Path $gamePath 'lotrbfme2ep1.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The game executable is missing.' }
    if (-not (Test-Path -LiteralPath (Join-Path $modPath 'data') -PathType Container)) {
        throw 'The selected Realms mod folder is missing its data folder.'
    }
    if ($modPath.Contains('"')) { throw 'The mod path cannot contain a quotation mark.' }
    if ((Get-BetaHash (Join-Path $gamePath 'mss\bfme2perf.asi')) -ne $ExpectedPerfSha256) {
        throw 'The performance beta was replaced or changed. Restore the tested beta before this test.'
    }
    if ((Get-BetaHash (Join-Path $gamePath 'mss\bfme2perf.ini')) -ne $ExpectedIniSha256) {
        throw 'The beta test settings changed. Check the settings before this test.'
    }
    $running = @(Get-Process | Where-Object {
        $_.ProcessName -match '^(game|game\.dat|lotrbfme2|lotrbfme2ep1|aotr_launcher)$' -or
        $_.ProcessName -match '(?i)realms.*launcher'
    })
    if ($running.Count) { throw 'Close BFME2 and its launchers before starting the beta test.' }
    if ($CheckOnly) {
        Write-Output 'Beta binary, settings, game path and mod path verified. Nothing launched.'
        exit 0
    }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.WorkingDirectory = $gamePath
    $start.Arguments = '-mod "' + $modPath + '"'
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Normal
    [void][Diagnostics.Process]::Start($start)
} catch {
    if (-not $CheckOnly) {
        Add-Type -AssemblyName PresentationFramework
        [void][Windows.MessageBox]::Show($_.Exception.Message, 'Realms beta test')
    }
    Write-Error $_.Exception.Message
    exit 1
}
