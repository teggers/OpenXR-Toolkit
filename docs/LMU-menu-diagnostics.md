# LMU menu diagnostic build: LMU-experiment-2

This build keeps the eye-target mapping that the user reports fixes both-eye gaze alignment in native OpenXR LMU. It adds bounded ordinary-log diagnostics for the left-eye-only Toolkit menu. It is **not a confirmed menu fix**.

The Windows x64 Release build and ownership tests passed in [run 35537672929](https://github.com/teggers/OpenXR-Toolkit/actions/runs/35537672929). [Download its DLL package](https://github.com/teggers/OpenXR-Toolkit/actions/runs/35537672929/artifacts/10613189458). The artifact is named `OpenXR-Toolkit-LMU-experiment-3` because this was CI run number 3; the DLL identifies as **LMU-experiment-2**. Source commit: `ef86f110eacc10b16ddc48268e57dab65a8721c8`. DLL SHA-256: `99f87c18bffaad958074ddf33f5d1ba6ec3f8e6f1aff61ec17e22cd6ec8f6a7e`. This diagnostic DLL has not yet been tested in the headset.

The Companion visibility control, an explicit native-profile `menu_eye=0`, and switching the legacy menu option have not resolved the reported symptom. The user subsequently clarified that the **right-eye menu is present but displaced far left**, with only its edge visible. The exact direction of the legacy-menu switch and the FPS overlay's right-eye visibility are not yet confirmed.

## Placement check with the existing DLL

With **Use legacy menu → Yes**, decrease **Menu eye offset** in steps of 250 pixels (for example, 0 → -250 → -500). Toolkit subtracts this value from its computed right-eye position, so decreasing it moves that menu to the right. Record the initial and final values. This setting affects menu/overlay placement, not gaze or the foveation masks. If the control cannot produce comfortable alignment, restore the initial value and capture the diagnostic log below. No automatic correction or exact offset has been established yet.

## Separate right-eye flicker comparison

The user also reports slightly greater text flicker in the right eye when looking at a fixed point. This observation does not establish incorrect depth. The eye-target patch changes mask selection without changing gaze projection, eye poses or depth-buffer contents.

First compare the same stationary cockpit text with **Foveated rendering → Off**, keeping mode 2, MSAA, resolution and other settings unchanged. If the extra flicker disappears, compare foveation with eye tracking disabled to separate moving-mask effects from fixed-mask/shading behavior. If it remains with foveation off, investigate the rendering baseline and other Toolkit processing before attributing it to gaze. No comparison result or flicker fix is confirmed yet.

## Install and capture one short run

1. Close LMU and SteamVR. Back up the currently working experimental DLL so you can restore it.
2. Replace `XR_APILAYER_MBUCCHIA_toolkit.dll` in the existing Toolkit installation, normally `C:\Program Files\OpenXR-Toolkit`. Do not put it in the LMU game directory.
3. Keep `lmu_eye_target_mode=2`, `menu_eye=0`, LMU VR FOV scaling off, and your working foveation/eye-tracking settings. No additional diagnostic registry option is needed.
4. Start native OpenXR LMU and enter the cockpit. Open the Toolkit menu and keep it visible for around ten seconds. Note whether the menu and FPS overlay appear in each eye. The displayed version should be **LMU-experiment-2**.
5. While the menu is open, change **Menu → Use legacy menu** once and keep the menu visible for another ten seconds. Note the old and new values and the result. This setting is read every frame, so the same log can record both paths. No further settings resets are needed.
6. Exit LMU. Copy the log below before launching another VR application that uses Toolkit, then share the copy for inspection:

```text
%LOCALAPPDATA%\OpenXR-Toolkit\logs\XR_APILAYER_MBUCCHIA_toolkit.log
```

## What the log tells us

- `LMU menu frame`: exact application profile, effective menu path and settings, safe mode, visibility and input layer count.
- `LMU menu target`: the swapchain/image, texture, slice, viewport and FOV for each final eye image. Eye 0 is left; eye 1 is right.
- `LMU menu quad`: the actual OpenXR eye-visibility enum and shared menu image submitted in normal mode.
- `LMU menu legacy draw`: whether each direct eye draw completed in legacy mode.
- `LMU menu layout`: menu bounds, right-eye placement offset and projection centers.
- `LMU menu submit`: submitted layer count and the runtime's `xrEndFrame` result (0 is success).

These records show Toolkit's CPU-side draw and submission decisions, not proof that the runtime displayed the pixels. They help select the next targeted fix. Logging is limited to 60 frame batches and 40 layout records per process; the existing eye-target diagnostics remain unchanged.

The only rendering behavior correction in this build is bounds checking for invalid `menu_eye` values. Valid values 0, 1 and 2 behave as before, so this is not expected to fix the reported problem with `menu_eye=0`.

## Roll back

Close LMU and SteamVR and restore the backed-up **LMU-experiment-1** DLL. Keep mode 2 to retain the working eye-target correction. Restore your preferred legacy-menu option if changed for the comparison.
