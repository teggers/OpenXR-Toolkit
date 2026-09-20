# Test the LMU experimental DLL

The Windows build and ownership tests passed. This DLL has **not** yet been validated in LMU.
It is based on official OpenXR Toolkit 1.3.2 and is intended for an existing 1.3.2 installation.

Download the `OpenXR-Toolkit-LMU-experiment-2` artifact from:
https://github.com/teggers/OpenXR-Toolkit/actions/runs/35528743144

The Mac workspace ZIP includes these instructions as `START-HERE.md`.

## Install and enable

1. Close LMU and SteamVR.
2. Find the installed Toolkit folder. The installer default is `C:\Program Files\OpenXR-Toolkit`.
3. Copy the original `XR_APILAYER_MBUCCHIA_toolkit.dll` to a backup location. Keep that original file for rollback.
4. Replace it with the experimental DLL. Leave the existing manifest, dependencies and shaders in place. This DLL belongs in the Toolkit installation, not the LMU game directory.
5. Enable the experiment for the **native LMU profile**. In Registry Editor, navigate to `HKEY_CURRENT_USER\SOFTWARE\OpenXR_Toolkit\<native LMU application name>`, and create/set DWORD (32-bit) **`lmu_eye_target_mode` to `2`**. Set **`eye_tracking` to `0`** for the initial fixed-foveation test.
6. Restart SteamVR and launch LMU in native OpenXR. Keep **LMU VR FOV scaling off**. 4x MSAA may remain enabled.

Installing the DLL alone leaves stock mode active. The per-app `lmu_eye_target_mode=2` setting is required to apply the experiment.

If you do not know the exact native profile name, launch native LMU once and read `running` under `HKEY_CURRENT_USER\SOFTWARE\OpenXR_Toolkit`; record the value before exiting the game. The matching application's `module` value should point to LMU. Close LMU before editing that profile. Do not select an `OpenComposite_...` profile.

The included helper is an alternative to Registry Editor. From the extracted binary package, run it with the exact profile name:

```powershell
.\Set-LmuEyeTargetMode.ps1 -AppName '<native LMU application name>' -Mode Apply
```

## Verify the test

The Toolkit log is at `%LOCALAPPDATA%\OpenXR-Toolkit\logs\XR_APILAYER_MBUCCHIA_toolkit.log`.
Check for `LMU-experiment-1` and `LMU eye-target experiment v1: mode=2`.

First repeat the eye-identity test while stationary: Custom foveation, expert settings enabled, Inner/Middle/Outer resolution all `1/2`. With correct eye assignment, `+4 L` should coarsen only the left eye and `+4 R` only the right eye. Then restore Left/Right Bias to `none` and normal ring resolutions, and check whether both foveation regions are correctly aligned. Keep gaze disabled for this first test.

If foveation disappears entirely, that is **not success**: this experiment deliberately leaves unknown targets at full shading rather than selecting a guessed eye. Save the log; its `learned`, `bindL`, `bindR`, `unknown`, and `overflow` fields help show whether LMU's rendering layout was recognized. Capture a short Toolkit trace if the ordinary log is insufficient.

After the fixed masks work correctly, eye tracking can be restored and tested separately. Correct masks alone do not prove an FPS improvement.

## Roll back

Close LMU and SteamVR, restore the backed-up official 1.3.2 DLL, and set `lmu_eye_target_mode` to `0` in the same native LMU profile. Restore any deliberately changed foveation settings.

## Build identity

- Build: https://github.com/teggers/OpenXR-Toolkit/actions/runs/35528743144
- Source commit: `e577cfe068c22b8141716532fce31605eebee402`
- DLL SHA-256: `28519305b899f8572a9db110e36bb6067cb1bc456393ea72722cda1d83f95888`
- Architecture: Windows x64 (PE32+ DLL)
