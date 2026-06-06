<#
.SYNOPSIS
    Drive the cc89 test runner inside Docker from Windows/PowerShell.

.DESCRIPTION
    Thin forwarder: bind-mounts the repo into the cc89-test image and passes every
    argument straight through to tools/tt.py. No nested-shell escaping.

    First time (or after changing the Dockerfile):
        .\tt.ps1 build-image

    Then:
        .\tt.ps1 run 9                  # chapter 9, end-to-end
        .\tt.ps1 run path\to\foo.c      # a single file
        .\tt.ps1 parse 1-9              # any stage over a chapter range
        .\tt.ps1 asm path\to\foo.c      # print the assembly cc89 produced
        .\tt.ps1 run 9 --extra-credit   # include the extra-credit tree
        .\tt.ps1 -v parse               # verbose, all implemented chapters
#>
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Rest)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot

# Windows tab-completion fills paths with backslashes, but the args run inside the
# Linux container where '\' isn't a separator. Normalize so either slash works.
if ($Rest) { $Rest = @($Rest | ForEach-Object { $_ -replace '\\', '/' }) }

if ($Rest.Count -ge 1 -and $Rest[0] -eq 'build-image') {
    docker build -t cc89-test "$repo"
    exit $LASTEXITCODE
}

docker run --rm -v "${repo}:/work" cc89-test @Rest
exit $LASTEXITCODE
