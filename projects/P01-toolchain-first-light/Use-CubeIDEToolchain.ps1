<#
.SYNOPSIS
  Put CubeIDE's bundled cross toolchain on PATH for this PowerShell session.

.DESCRIPTION
  STM32CubeIDE ships its own arm-none-eabi-gcc, cmake and ninja, and registers
  none of them system-wide. So a fresh terminal on the build laptop cannot even
  run cmake, and the first symptom is

    cmake : The term 'cmake' is not recognized as the name of a cmdlet

  which reads as a missing install and is not. Nothing is missing. The tools are
  several directories deep inside the IDE's plugin tree, under names that carry
  their own version numbers, so the path cannot be written down once and reused
  after an IDE update.

  This script finds them by name and prepends their directories to PATH for the
  current session only. Nothing is installed, nothing is written outside the
  process, and a new terminal starts clean again.

  Dot-source it, do not run it. Running it in a child scope sets PATH in that
  scope and the change is gone when it exits, which looks exactly like the
  script having done nothing:

    . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1

  Written Friday 2 October 2026 on win11 skyhorizon, the day the second fresh
  terminal hit the same error as the first.

.PARAMETER SearchRoot
  Where to look. Defaults to the usual CubeIDE location, C:\ST .
#>
[CmdletBinding()]
param(
    [string] $SearchRoot = 'C:\ST'
)

$wanted = 'arm-none-eabi-gcc.exe', 'cmake.exe', 'ninja.exe'

if (-not (Test-Path -LiteralPath $SearchRoot)) {
    Write-Host "no such directory: $SearchRoot" -ForegroundColor Red
    Write-Host 'CubeIDE normally installs under C:\ST . Pass -SearchRoot if it is elsewhere.'
    return
}

Write-Host "searching $SearchRoot for the bundled toolchain, one pass" -ForegroundColor Cyan

# One recursive pass for all three. Three separate passes over a CubeIDE tree
# costs about three times as long for no benefit, and this tree is large.
$hits = Get-ChildItem -LiteralPath $SearchRoot -Recurse -Include $wanted -File -ErrorAction SilentlyContinue

$dirs = @()
foreach ($name in $wanted) {
    $found = $hits | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    if ($found) {
        Write-Host ("  found    {0,-24} {1}" -f $name, $found.DirectoryName) -ForegroundColor Green
        $dirs += $found.DirectoryName
    } else {
        Write-Host ("  MISSING  {0}" -f $name) -ForegroundColor Red
    }
}

if ($dirs.Count -eq 0) {
    Write-Host 'nothing found, PATH unchanged' -ForegroundColor Red
    return
}

$dirs = $dirs | Select-Object -Unique
$env:PATH = ($dirs -join ';') + ';' + $env:PATH

Write-Host ''
Write-Host 'PATH set for this session only. The tools now report:' -ForegroundColor Cyan
foreach ($pair in @(
        @('arm-none-eabi-gcc', '-dumpversion'),
        @('cmake',             '--version'),
        @('ninja',             '--version'))) {
    $exe = $pair[0]; $arg = $pair[1]
    $c = Get-Command $exe -ErrorAction SilentlyContinue
    if ($c) {
        $v = (& $exe $arg 2>&1 | Select-Object -First 1)
        Write-Host ("  {0,-20} {1}" -f $exe, $v)
    } else {
        Write-Host ("  {0,-20} still not on PATH" -f $exe) -ForegroundColor Red
    }
}
