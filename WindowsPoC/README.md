# Compositor Windows portability PoC

This spike validates a native Windows path while preserving the portable parts of Compositor's existing rendering and document semantics.

## What it proves

- A native Windows executable builds with MSVC/CMake.
- Images decode through Windows Imaging Component (WIC).
- Existing Compositor C rendering code compiles and runs unchanged on Windows (`LevelsPixels.c` and `NoisePixels.c`).
- `RasterImage` owns premultiplied RGBA pixels without AppKit, CoreGraphics, WIC or Direct2D.
- A platform-neutral document model can mirror the useful subset of `CanvasDocument`, `ImageLayer` and `LayerTransform`.
- Layers remain bottom-to-top, can belong to pass-through folders, and inherit parent visibility and opacity like Compositor's `LayerHierarchy` / `LayerOpacity` logic.
- A pixel operation can target one layer's portable raster before the document is composited.
- Direct2D remains only the Windows presentation adapter.

The demo loads one image and creates a real document-shaped hierarchy: a base layer plus a second layer inside a 75% opacity folder. The child is 60% opaque, so its effective opacity is 45%, matching Compositor's folder-opacity semantics. The child also receives Compositor's existing monochromatic Add Noise implementation before flattening.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S WindowsPoC -B WindowsPoC/build -A x64
cmake --build WindowsPoC/build --config Release
.\WindowsPoC\build\Release\CompositorWindowsPoC.exe
```

## Architecture direction validated

```text
Portable document/core
  PortableDocument          ~= CanvasDocument
  DocumentLayer             ~= ImageLayer
  LayerTransformModel       ~= LayerTransform data
  RasterImage               replaces CGImage at the core boundary
  hierarchy + effective opacity
  CPU compositing
  existing C pixel algorithms
        |
        +---- macOS adapters
        |       CGImage / CoreGraphics / Metal / AppKit
        |
        +---- Windows adapters
                WIC / Direct2D / Direct3D
```

The important result is that the *semantics* of a meaningful part of the Swift document model are portable even though its current concrete types are not. `CGPoint`, `CGSize`, `CGImage`, `CGBlendMode`, SwiftUI/Observation and other Apple types should not define the future shared-core boundary.

## Audit findings from the real model

### Good candidates to preserve conceptually

- `CanvasDocument`: dimensions, resolution, ordered layers and guides.
- `ImageLayer`: IDs, names, visibility, hierarchy, opacity and most metadata.
- `LayerTransform`: origin/size/rotation/flips/sampling as data and most geometry math.
- `LayerHierarchy` and `LayerOpacity`: platform-independent algorithms.
- `DocumentHistory`: snapshot/undo/redo design, once image identity/accounting no longer depends on `CGImage`.
- `LevelsSettings` and other adjustment parameter models.
- PSD parsing/channel decoding logic: most parsing is Foundation/data logic; image construction is the Apple-specific seam.
- Existing C pixel kernels such as Levels, Noise, Brush, Heal, Wand and Content Fill should be evaluated and reused individually.

### Must be adapted behind platform-neutral types

- `ImportedImage` and `RasterSnapshot` currently expose `CGImage` directly.
- `LayerMask` stores and manipulates `CGImage`/`CGContext`.
- Project persistence uses ImageIO/UTType/NSFileCoordinator for PNG/package I/O.
- PSD records currently store `CGRect` and `CGImage`; decoded channel bytes can instead feed `RasterImage`.
- `CanvasViewport` and transform math use CoreGraphics geometry types even though most calculations are portable.

### Windows-specific replacements required

- `LayerRenderer` and `SeparableBlend` are built on CoreGraphics/CoreImage.
- Image import/export uses CoreImage/ImageIO/UTType.
- `PixelInvert` uses Accelerate/vImage.
- GPU effects use Metal.
- SwiftUI/AppKit UI and `EditorSession` presentation state cannot be reused directly as the Windows UI.

## Scope deliberately not claimed by PoC 3

PoC 3 only renders translated 1:1 raster layers with Normal blend mode. Rotation, scaling/resampling, flips, masks, clipping masks, all blend modes, adjustment layers, sparse brush snapshots, text/vector layers and GPU effects still need dedicated portable/rendering implementations. The model already reserves the transform/blend fields so those features can be added without changing the document boundary again.

## Architectural conclusion

Do not attempt to compile the current SwiftUI/AppKit application for Windows. Extract a platform-neutral document/raster core with data structures equivalent to the existing Swift model, keep the proven C kernels, and implement platform adapters on each OS. This preserves Compositor's behavior and project semantics while removing Apple framework types from the core.

The next implementation milestone should stop being a throwaway feasibility spike: create the initial shared-core directory/library, move `RasterImage` and the portable document types there, add automated core tests, then implement transforms and blend modes incrementally behind that stable boundary.
