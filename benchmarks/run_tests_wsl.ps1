<#
.SYNOPSIS
    Run the cc89 test suite inside WSL2 (Ubuntu) from Windows/PowerShell.

.DESCRIPTION
    Thin wrapper around benchmarks/run_tests.sh so you don't have to remember
    the `wsl.exe -d Ubuntu bash -lc "cd /mnt/c/... && ..."` incantation. It
    derives the repo's /mnt/<drive>/... path automatically and forwards every
    argument straight to run_tests.sh.

    With no arguments it runs the full codegen execution suite
    (--stage run over benchmarks/nlsandler/parse_valid), which assembles,
    links, and runs each program with the WSL gcc and compares exit codes
    against a reference gcc build.

.EXAMPLE
    .\benchmarks\run_tests_wsl.ps1
    Full codegen execution suite.

.EXAMPLE
    .\benchmarks\run_tests_wsl.ps1 --stage run benchmarks/nlsandler/parse_valid/chapter_5
    Just one chapter.

.EXAMPLE
    .\benchmarks\run_tests_wsl.ps1 -v --stage validate benchmarks/sa
    Any other stage / path, verbose.
#>
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $ForwardedArgs)

$ErrorActionPreference = 'Stop'

# Repo root = parent of this script's directory, translated to a WSL path:
#   C:\Users\vedant\Projects\C-Compiler  ->  /mnt/c/Users/vedant/Projects/C-Compiler
$repoWin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$drive   = $repoWin.Substring(0, 1).ToLower()
$rest    = $repoWin.Substring(2).Replace('\', '/')
$repoWsl = "/mnt/$drive$rest"

# Default to the full codegen execution suite when no args are given.
if (-not $ForwardedArgs -or $ForwardedArgs.Count -eq 0) {
    $ForwardedArgs = @('--stage', 'run', 'benchmarks/nlsandler/parse_valid')
}

$forwarded = $ForwardedArgs -join ' '
wsl.exe -d Ubuntu bash -lc "cd '$repoWsl' && bash benchmarks/run_tests.sh $forwarded"
exit $LASTEXITCODE
