# Building the 3ds Max plug-in on Windows

## Requirements

| | |
|---|---|
| 3ds Max | 2023 or newer (2022+ SDK naming API; 2021 should also work via `SdkCompat.h`) |
| 3ds Max SDK | Same release as the 3ds Max you target (installed from the Max installer or the Autodesk developer site) |
| Compiler | Visual Studio 2022 (v143 toolset), "Desktop development with C++" |
| CMake | 3.20 or newer |

A plug-in only loads in the 3ds Max release whose SDK it was built with. Build
one `.dlm` per release you support (for example `TopoWeave_2025.dlm`).

## Build

```bat
cd 3ds-max-spline-morph-modifire-plugin
cmake -S . -B build-max -G "Visual Studio 17 2022" -A x64 ^
      -DTOPOWEAVE_BUILD_MAX=ON ^
      -DMAXSDK_DIR="C:/Program Files/Autodesk/3ds Max 2025 SDK/maxsdk"
cmake --build build-max --config Release --target TopoWeave
```

- If `MAXSDK_DIR` is not given, CMake looks for the `ADSK_3DSMAX_SDK_<year>`
  environment variables that the SDK installer sets.
- Add `-DMAX_PLUGIN_DIR="C:/Program Files/Autodesk/3ds Max 2025/Plugins"` to
  copy the plug-in automatically after each build. Visual Studio must then run
  as administrator.
- The output is `build-max/max/Release/TopoWeave.dlm`.

## Install

1. Close 3ds Max.
2. Copy `TopoWeave.dlm` directly into
   `C:\Program Files\Autodesk\3ds Max <release>\Plugins\`.
3. Start 3ds Max, select a poly object, and add **TopoWeave** from the modifier
   list (category *TopoWeave*).

To update, close Max, replace the `.dlm` and restart.

## Debugging

Open `build-max/TopoWeave.sln`, set **TopoWeave** as the start-up project, and
under *Debugging > Command* point to `3dsmax.exe`. Build the
`RelWithDebInfo` configuration: Max plug-ins must link the release CRT.

## If the first build fails

The plug-in layer was written against the official SDK reference and
syntax-checked against `max/devtools/sdk_stub`. It has not yet been compiled
against a real SDK, so a small API mismatch in a specific SDK release is
possible. Send the compiler output and it can be fixed quickly. All the
geometry lives in `core/`, which is built and tested on every commit.
