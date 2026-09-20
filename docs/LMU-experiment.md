# LMU eye-target experiment 1

Based on official Toolkit **1.3.2**, commit `c568a3cdb8f12c355f4d1b23a795663202a5c8a7`.
This is an experimental source patch, not a confirmed LMU fix or an official Toolkit release.

## Reason for this experiment

On PSVR2, native-OpenXR LMU with Toolkit 1.3.2 has a gaze-following right-eye foveation region displaced far to the right. Left-eye alignment is correct. Calibration did not change the symptom, and PimaxMagic4All is correctly aligned through OpenVR. Disabling LMU VR FOV scaling was necessary to activate Toolkit foveation. Turning 4x MSAA off **did not change the offset**, so an MSAA-specific explanation is insufficient. The user's preferred configuration uses 4x MSAA.

The right-eye offset also persists after removing PSVR2Toolkit (the mask becomes stationary) and resetting Toolkit to defaults. In the subsequent Left/Right Bias comparison, **+4 L coarsens both eyes, while +4 R has no visible effect**. This strongly supports the left-eye shading configuration reaching both views. It does not prove a particular frame-analyzer failure, establish LMU OpenXR nonconformance, or validate this patch for LMU's actual texture layout.

Toolkit's existing analyzer switches its eye prediction when an image is copied to an OpenXR swapchain. This assumes the copy separates the two eyes' rendering. It may be too late if both eyes are rendered first. Also, release 1.3.2 does not intercept D3D11 `ResolveSubresource`, which transfers multisampled rendering into a single-sampled image. Neither omission has yet been proven to cause LMU's problem.

## What changes

- Optional D3D11 resolve observation, alongside existing copies.
- A graph that traces whole-image copies/resolves back from explicitly identified, separate OpenXR eye images. No changes to gaze values, calibration, gaze offsets or shaders.
- Intermediate targets require the same unique eye assignment in two consecutive completed frames. A reused target feeding both eyes is rejected. Missing observations expire the mapping; held resource references prevent stale native-pointer reuse.
- Apply mode uses the learned eye and disables VRS for unrecognized targets. It does not substitute the static generic mask or guess that every unknown target is left-eye.
- Bounded logs/ETW diagnostics. Resource tracking is limited to 128 resources and 256 transfer events per frame; overflow invalidates the frame's learned mapping. Resources can be retained for one additional frame, increasing transient VRAM use.
- Default behavior is stock. The experiment has no HKLM/global setting fallback and must be explicitly enabled in the selected application's HKCU profile. Restart the game between modes.
- Build compatibility: the existing hand-joint cache uses `std::array` instead of a raw array inside `std::pair`, allowing the modern MSVC standard library to move/assign deque entries. The OpenXR joint-buffer interface remains unchanged.

This is intended only for LMU's native D3D11 OpenXR path. No automatic LMU application-name guess is used: its exact OpenXR profile name must come from the log. Do not enable it in an OpenComposite profile or another game.

## Validation so far

Portable C++ tests have passed on macOS with AddressSanitizer and UndefinedBehaviorSanitizer. Tests cover delayed eye copies, an MSAA-resolve/copy chain, confirmation delay, shared intermediates, partial-copy rejection, cycles, ownership changes, expiry, collection overflow and reset.

**Windows x64 Release build succeeded**, including the ownership tests under MSVC, in [GitHub Actions run 35528743144](https://github.com/teggers/OpenXR-Toolkit/actions/runs/35528743144). Code commit: `e577cfe068c22b8141716532fce31605eebee402`. The downloaded DLL matches the artifact's SHA-256: `28519305b899f8572a9db110e36bb6067cb1bc456393ea72722cda1d83f95888`.

The build script ran successfully on Windows. D3D interception, the registry configuration helper and headset behavior have not been runtime-tested. Synthetic ownership tests do not establish that LMU uses these copy chains. See [the quick-start instructions](LMU-quick-start.md) for the binary test.

## Build on Windows

The `LMU experimental Windows DLL` GitHub Actions workflow builds on a Windows 2022 runner and uploads a replacement DLL, symbols, settings helper and this guide. It requires no signing secrets and runs the standalone ownership tests before compiling the layer. The artifact is intended to replace the DLL in an existing official 1.3.2 installation for controlled testing. A successful build still requires an LMU headset test.

Use a checkout without spaces in its path, e.g. `C:\src\OpenXR-Toolkit-LMU`. Install Visual Studio 2022 C++ desktop tools with an appropriate Windows SDK, Python 3, Git and the NuGet CLI. In an **x64 Native Tools** terminal, start PowerShell and run:

```powershell
# If starting from upstream, clone the matching tag and apply the supplied source patch first:
git clone --branch 1.3.2 https://github.com/mbucchia/OpenXR-Toolkit.git C:\src\OpenXR-Toolkit-LMU
Set-Location C:\src\OpenXR-Toolkit-LMU
git switch -c codex/lmu-eye-target-mapping
git apply C:\Downloads\openxr-toolkit-1.3.2-lmu-experiment.patch
.\scripts\Build-LmuExperiment.ps1
```

The script fetches pinned submodules, restores the layer's NuGet dependencies, runs the portable tests, builds FW1FontWrapper and the Release layer, and copies upstream runtime dependencies. It defaults to toolset v143; `-Toolset v142` uses installed VS 2019 C++ tools. It skips the upstream MSI/signing workflow. Result: `bin\x64\Release\XR_APILAYER_MBUCCHIA_toolkit.dll` plus dependencies/shaders. No installation or registry registration is performed.

This branch changes no shaders or runtime dependencies relative to 1.3.2. For a controlled test, close LMU/VR applications, retain a copy of the installed official 1.3.2 Toolkit DLL, and replace that DLL with the successfully built experimental DLL. Use the installed layer location, **not the LMU game folder**. Do not register a second copy of the same layer. Restore the saved official DLL to roll back. Verify the log identifies `LMU-experiment-1` before relying on the experiment setting.

## Configure and compare

The stock MSAA-off comparison is complete: the user reports no improvement. Keep LMU VR FOV scaling **off** and use the preferred 4x MSAA setting for the following comparisons. A resolve hook alone is not a supported explanation or fix; learning ownership across ordinary copies is the broader hypothesis being tested.

For the custom build, get the exact native LMU `Application name` from:

```text
%LOCALAPPDATA%\OpenXR-Toolkit\logs\XR_APILAYER_MBUCCHIA_toolkit.log
```

The setting is `HKCU\SOFTWARE\OpenXR_Toolkit\<exact application name>\lmu_eye_target_mode` (DWORD). Close LMU, then use the included helper with that name:

```powershell
# Replace the placeholder with the exact Application name from the LMU log.
.\scripts\Set-LmuEyeTargetMode.ps1 -AppName '<exact application name>' -Mode Observe
```

| Mode | Value | Behavior |
| --- | ---: | --- |
| Stock | 0 | Original analyzer; resolve hook disabled |
| Observe | 1 | Collect mapping evidence while retaining original mask selection |
| Apply | 2 | Select learned masks; leave unrecognized targets at full shading |

Keep LMU FOV scaling off and other settings identical. Capture a stationary cockpit run for Observe, exit, save the log, then select Apply and restart for the same scene. Save separate logs before another run overwrites them. Briefly use a small inner ring and coarse/Cull outer region to inspect each eye; restore normal settings afterward. Compare Stock and Apply on the same MSAA setting, then inspect text, mirrors, menus and session restart.

Look for `LMU eye root`, `LMU eye transfer` and periodic `LMU eye frame` lines. `learned`, `bindL`, `bindR`, `unknown`, `resolves` and `overflow` describe recognition, not GPU-time benefit. Roots without learned intermediates suggest the copy graph is incomplete. Zero right-eye bindings mean this approach has not identified the right-eye scene; disappearance of foveation alone is **not** a fix. Companion's ETW capture includes `LMU_EyeTargetTransfer` and `LMU_EyeTargetBind` for deeper investigation.

## Limits and next decisions

Only separate, whole-eye textures and complete base-level transfers on the immediate D3D11 context are learned. Texture arrays, packed viewports, partial copies, mip operations and deferred-context recording are not handled. Shader-based post-processing between the scene buffer and swapchain is not a copy edge, so this patch cannot infer that connection. A shader resolve can likewise bypass the new API hook.

Mappings describe earlier frames, not an API guarantee: a resource repurposed in the current frame can use its previously learned eye until that change is observed. Two-frame confirmation reduces accidental assignments but cannot predict all renderer transitions. Whole-frame copy graphs conservatively reject shared buffers and can miss useful targets. Stock candidate-size/aspect-ratio filtering remains unchanged; this does not fix the separate LMU FOV-scaling interaction.

If both eyes acquire correct unique masks but right-eye alignment remains wrong, investigate Toolkit's gaze projection and actual LMU viewports next. If no usable map is learned, use the logs/trace to design a narrower LMU render-pass rule; do not compensate with a guessed right-eye offset.
