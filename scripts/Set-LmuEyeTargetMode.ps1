<#
Configure only the explicitly named, existing native-OpenXR LMU profile.
Use the exact Application name from XR_APILAYER_MBUCCHIA_toolkit.log.
Requires the experimental DLL; stock Toolkit ignores this setting.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$AppName,
    [Parameter(Mandatory)][ValidateSet('Stock', 'Observe', 'Apply')][string]$Mode
)
$ErrorActionPreference = 'Stop'
if ($AppName -match '[\\/]' -or $AppName.StartsWith('OpenComposite_')) {
    throw 'Supply the native OpenXR application name from the LMU log, not an OpenComposite profile or a path.'
}
$profilePath = "HKCU:\SOFTWARE\OpenXR_Toolkit\$AppName"
if (-not (Test-Path -LiteralPath $profilePath)) {
    throw "Profile does not exist: $profilePath. Run LMU once with Toolkit and check its Application name in the log."
}
$value = @{ Stock = 0; Observe = 1; Apply = 2 }[$Mode]
if ($PSCmdlet.ShouldProcess($profilePath, "Set lmu_eye_target_mode=$value ($Mode)")) {
    New-ItemProperty -LiteralPath $profilePath -Name 'lmu_eye_target_mode' -PropertyType DWord -Value $value -Force | Out-Null
    Write-Output "Mode $Mode set for '$AppName'. Fully restart LMU for this change to take effect."
}
