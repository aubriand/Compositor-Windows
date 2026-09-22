# Compositor Windows portability PoC

This spike validates a native Windows path without rewriting Compositor's existing portable rendering code first.

## What it proves

- A native Windows executable can be built with MSVC/CMake.
- Images can be decoded with Windows Imaging Component (WIC).
- Existing Compositor C rendering code can be compiled and invoked unchanged (`LevelsPixels.c`).
- A platform-neutral `RasterImage` can own premultiplied RGBA pixels without depending on AppKit, CoreGraphics, WIC or Direct2D.
- A tiny document model can composite multiple raster layers with position, visibility and opacity on the CPU.
- The final composited raster can be uploaded to Direct2D for display.

The current demo loads one dropped image and builds a two-layer document from it: a full-opacity base layer and an offset 45% opacity copy. The duplication is intentional so the compositing result is immediately visible without requiring additional assets.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S WindowsPoC -B WindowsPoC/build -A x64
cmake --build WindowsPoC/build --config Release
.\WindowsPoC\build\Release\CompositorWindowsPoC.exe
```

## Architecture direction validated by the spike

```text
Portable core
  RasterImage
  RasterLayer
  CPU compositing
  Existing C pixel algorithms
        |
        +---- macOS adapters (future)
        |       CoreGraphics / Metal / AppKit
        |
        +---- Windows adapters
                WIC / Direct2D / Direct3D
```

The important boundary is that the portable core does not include Windows or Apple framework headers.

## Still not validated

- Porting Compositor's Swift document/session model.
- PSD import through the portable raster type.
- Selections, masks, transforms, text and brushes.
- GPU rendering/effects replacing Metal on Windows.
- Final Windows UI framework and application architecture.

## Next spike

Map a small subset of the real Compositor document/layer model onto `RasterImage` rather than using the temporary `RasterLayer` struct, then exercise one additional existing C operation on a selected layer.
