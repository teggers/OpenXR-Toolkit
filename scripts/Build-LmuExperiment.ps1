<# Run from a VS 2022 x64 Native Tools terminal. Builds no installer and registers no layer. #>
[CmdletBinding()]
param([ValidateSet('v142', 'v143')][string]$Toolset = 'v143')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if ($repoRoot -match '\s') {
    throw 'Upstream post-build commands require a checkout path without spaces; use e.g. C:\src\OpenXR-Toolkit-LMU.'
}
foreach ($tool in @('git', 'nuget', 'python', 'msbuild', 'cl')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing $tool. Use VS x64 Native Tools, with Python 3 and NuGet CLI on PATH."
    }
}
function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with $LASTEXITCODE" }
}
Push-Location $repoRoot
try {
    Run-Checked 'git' @('submodule', 'update', '--init', '--recursive')
    Run-Checked 'nuget' @('restore', 'XR_APILAYER_MBUCCHIA_toolkit\packages.config', '-PackagesDirectory', 'packages', '-NonInteractive')

    $testDirectory = Join-Path $repoRoot 'bin\lmu-tests'
    New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
    Run-Checked 'cl' @('/nologo', '/std:c++17', '/EHsc', '/W4', '/WX',
        'tests\eye_target_tracker_tests.cpp', "/Fe$testDirectory\eye-target-tests.exe", "/Fo$testDirectory\eye-target-tests.obj")
    Run-Checked "$testDirectory\eye-target-tests.exe" @()

    $properties = @('/m', '/p:Configuration=Release', '/p:Platform=x64',
        "/p:PlatformToolset=$Toolset", "/p:SolutionDir=$repoRoot\")
    Run-Checked 'msbuild' (@('external\FW1FontWrapper\FW1FontWrapper.vcxproj') + $properties)
    Run-Checked 'msbuild' (@('XR_APILAYER_MBUCCHIA_toolkit\XR_APILAYER_MBUCCHIA_toolkit.vcxproj') + $properties)
    Write-Output "Experimental build: $repoRoot\bin\x64\Release. No installation was performed."
} finally {
    Pop-Location
}
