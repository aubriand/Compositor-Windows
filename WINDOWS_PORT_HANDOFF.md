# Compositor Windows Port — Codex Handoff

## Goal

Port Compositor to Windows while preserving as much of the existing rendering/document logic as is technically sensible. Do **not** try to compile the existing SwiftUI/AppKit application directly for Windows. The validated direction is to extract a platform-neutral core and provide platform-specific graphics/UI adapters.

Repository: `aubriand/Compositor-Windows`

The original application is a native macOS application written primarily in Swift/SwiftUI, with AppKit/CoreGraphics/CoreImage/ImageIO/Metal dependencies and a collection of lower-level C pixel-processing implementations.

## Current state

PR #1 completed the Windows feasibility work and is now present on `main`.

The proof of concept lives under:

`WindowsPoC/`

Build on Windows from a Visual Studio Developer PowerShell:

```powershell
cmake -S WindowsPoC -B WindowsPoC/build -A x64
cmake --build WindowsPoC/build --config Release
.\WindowsPoC\build\Release\CompositorWindowsPoC.exe
```

CI workflow:

`.github/workflows/windows-poc.yml`

All three POC stages have passed Windows CI.

## What has been proven

### POC 1 — native Windows + existing C code

Validated that:

- a native Windows executable can be built using MSVC/CMake;
- images can be decoded through Windows Imaging Component (WIC);
- images can be displayed using Direct2D;
- drag-and-drop image loading works;
- existing Compositor C rendering code can be compiled by MSVC and invoked from the Windows executable;
- `Compositor/Rendering/LevelsPixels.c` is reused directly rather than copied/reimplemented.

The levels operation initially uses an identity LUT intentionally. The purpose was to prove source portability, not to alter the image.

### POC 2 — platform-neutral raster and layer compositing

Introduced a platform-neutral `RasterImage` owning premultiplied RGBA pixels without including AppKit, CoreGraphics, WIC, Direct2D or other platform headers.

Validated:

- raster ownership outside Apple frameworks;
- multiple raster layers;
- layer position;
- visibility;
- opacity;
- CPU alpha compositing;
- upload of the final composited raster to Direct2D.

### POC 3 — mapping the real Compositor document concepts

The spike was compared against the real Compositor document/session model.

The temporary model mirrors important concepts rather than only having a flat raster layer:

- `PortableDocument` ≈ portable subset of `CanvasDocument`;
- `DocumentLayer` ≈ portable subset of `ImageLayer`;
- `LayerTransformModel` ≈ platform-neutral transform data;
- parent/child hierarchy;
- group/folder layers;
- inherited visibility;
- inherited opacity;
- hierarchy validation;
- selected-layer pixel processing.

The demo exercises:

```text
Document
├── Background             opacity 100%
└── Folder                 opacity 75%
    └── Noise overlay      opacity 60%
```

The effective child opacity is 45% (`0.75 * 0.60`), matching Compositor's current folder-opacity semantics.

A second original C operation is reused directly:

`Compositor/Rendering/NoisePixels.c`

Noise is applied to a selected raster layer before portable document compositing.

## Architectural conclusion

The port is technically viable, but it is an **architectural port**, not a target change from macOS to Windows.

Recommended long-term boundary:

```text
                         Compositor Core
                  (platform-neutral domain)

    Document / Layers / Hierarchy / History / Selection
    Transform data + math / Opacity / Adjustment settings
    RasterImage / PSD parsing / portable pixel algorithms
                         |
              +----------+----------+
              |                     |
         macOS adapters        Windows adapters
              |                     |
       CoreGraphics / Metal     WIC / Direct2D
       AppKit / SwiftUI         Direct3D / Windows UI
       ImageIO                  Windows codecs/input
```

The shared core must not depend on AppKit, SwiftUI, CoreGraphics, CoreImage, ImageIO, Metal, WIC, Direct2D, Direct3D, Win32 or another platform UI/rendering API.

## What appears reusable

### High confidence

The existing C pixel algorithms are the strongest portable asset. Examples under `Compositor/Rendering` include:

- `LevelsPixels.c`
- `NoisePixels.c`
- `AdjustPixels.c`
- `BrushPixels.c`
- `HealPixels.c`
- `WandPixels.c`
- `ContentFill.c`
- `LensPixels.c`

Do not rewrite these merely because the Windows application is new. Audit each for platform assumptions and reuse directly where possible.

### Reusable conceptually / after abstraction

Preserve the semantics of:

- document model;
- image layers and layer hierarchy;
- layer visibility and opacity;
- transform state/math;
- history/undo model;
- adjustment settings;
- selections and masks where their representation can be made portable;
- PSD parsing logic;
- project/document serialization concepts.

Current Swift implementations often expose Apple-specific types, so they cannot simply be used as the Windows frontend model unchanged.

### Platform-specific or heavily coupled today

Expect adapters or replacements for:

- SwiftUI;
- AppKit / `NSView` / `NSWindow` / `NSImage` / cursor and event handling;
- `CGImage`, `CGPoint`, `CGSize`, `CGRect` and other CoreGraphics types leaking into domain models;
- CoreImage operations;
- ImageIO / UTType import/export;
- Metal compute/render pipelines and shaders;
- `LayerRenderer` where it depends on CoreGraphics/CoreImage;
- `SeparableBlend` where it depends on Apple graphics types;
- `RasterSnapshot` where it stores Apple image types;
- masks/import/export code that assumes `CGImage`;
- Sparkle and macOS DMG/notarization infrastructure.

## Important implementation principle

Do not replace Apple dependencies with Windows dependencies inside the shared model.

Wrong direction:

```text
CGImage -> ID2D1Bitmap
CGPoint -> POINT
```

Correct direction:

```text
RasterImage
Point / Size / Rect
LayerTransformModel
DocumentLayer
PortableDocument
```

Then adapt only at the platform boundary:

```text
macOS:   RasterImage -> CGImage / Metal texture
Windows: RasterImage -> WIC / Direct2D / Direct3D texture
```

## Pixel format warning

The POC uses premultiplied RGBA storage. WIC currently decodes to `GUID_WICPixelFormat32bppPRGBA`, and Direct2D receives a premultiplied bitmap.

Before expanding the core, explicitly define and test the canonical pixel layout and alpha convention. Do not let filters/importers/renderers silently disagree about RGBA/BGRA or straight/premultiplied alpha.

This matters especially for PSD, masks, blend modes and GPU acceleration.

## Recommended next phase

The feasibility phase is complete. **Do not keep growing `WindowsPoC` into the production application.**

Create a new implementation branch from `main` and start extracting the real portable core.

### Phase 1 — establish `CompositorCore`

1. Define permanent portable geometry types (`Point`, `Size`, `Rect`, transforms as required).
2. Promote/refine `RasterImage` into the shared core.
3. Define canonical pixel format and alpha semantics.
4. Move portable document/layer hierarchy concepts from the POC into the real core.
5. Add validation and unit tests independent of any native window.
6. Wrap/reuse existing C algorithms from the core.
7. Keep the macOS application working during extraction; avoid a flag-day rewrite.

### Phase 2 — production Windows application shell

Create the actual Windows application separately from `WindowsPoC`.

Initial target:

- application window;
- document/canvas viewport;
- raster image open/import;
- Direct2D/Direct3D presentation;
- pan/zoom;
- basic layer list;
- layer selection;
- visibility and opacity changes.

Do not port every toolbar/panel before the core boundary is stable.

### Phase 3 — document fidelity

Incrementally add:

- transforms;
- blend modes;
- masks;
- selections;
- history/undo/redo;
- project serialization;
- PSD import/export;
- adjustments and filters.

Where practical, add parity tests against macOS: same input document + same operation should produce equivalent document state/pixels within defined tolerances.

### Phase 4 — interactive editing

Then port:

- brush engine;
- healing/content fill/wand;
- text;
- shapes;
- guides;
- richer canvas interactions;
- keyboard shortcuts;
- clipboard and drag/drop.

### Phase 5 — GPU path

Do not translate Metal shaders blindly at the start.

Establish correct CPU/reference behavior first. Profile later and move performance-critical paths behind a Windows GPU backend, likely Direct3D/DirectCompute or another deliberately selected abstraction.

## Windows UI technology

No final UI framework was validated by the POC. It deliberately uses raw Win32 + WIC + Direct2D only to prove feasibility.

Do not infer that raw Win32 must be the final production UI.

Before committing to WinUI 3, raw Win32, Qt, or another framework, run a small decision spike based on:

- custom high-performance canvas integration;
- pointer/stylus support;
- keyboard shortcuts;
- drag/drop and clipboard;
- high DPI;
- accessibility;
- menus/panels/toolbars;
- packaging/deployment;
- long-term maintenance.

A native Windows approach is preferred over Electron/Tauri for this graphics-heavy editor unless later evidence changes that conclusion.

## Do not do these things

- Do not attempt a mechanical SwiftUI-to-Windows conversion.
- Do not make the portable core depend on Windows SDK headers.
- Do not duplicate existing C algorithms without a concrete reason.
- Do not rewrite the macOS app wholesale just to share code.
- Do not treat `WindowsPoC` classes as final APIs; they are validation scaffolding.
- Do not start with GPU shader parity before CPU/document semantics are stable.
- Do not break the currently functional macOS application while extracting shared concepts.

## Files to read first

```text
WINDOWS_PORT_HANDOFF.md
WindowsPoC/README.md
WindowsPoC/src/RasterImage.h
WindowsPoC/src/PortableDocument.h
WindowsPoC/src/main.cpp
WindowsPoC/CMakeLists.txt
.github/workflows/windows-poc.yml
```

Then compare with the current application, especially:

```text
Compositor/Document/EditorSession.swift
Compositor/Document/LayerTransform.swift
Compositor/Document/LayerAppearance.swift
Compositor/Document/LayerGroups.swift
Compositor/Document/DocumentHistory.swift
Compositor/Document/LayerMask.swift
Compositor/Document/LayerAdjustment.swift
Compositor/Rendering/LayerRenderer.swift
Compositor/Rendering/RasterSnapshot.swift
Compositor/Rendering/SeparableBlend.swift
Compositor/Rendering/MetalLayerEffects.swift
Compositor/IO/ImageImporter.swift
Compositor/IO/ProjectStore.swift
Compositor/IO/PSD/
Compositor/Rendering/*Pixels.c
```

## Definition of success for the first production milestone

The first real Windows milestone is **not** "all Compositor features ported".

Target this instead:

1. build the shared core and Windows app in CI;
2. open a normal raster image on Windows;
3. represent it as a real shared-core document/layer;
4. display it in the Windows canvas;
5. pan and zoom;
6. add/duplicate/select/reorder layers;
7. change layer visibility and opacity;
8. apply at least one reused existing C operation;
9. undo/redo that operation;
10. save/reopen enough document state to prove the architecture;
11. keep the macOS build unaffected.

Once this works, expand feature-by-feature.

## CI discipline

Keep Windows CI green after every incremental step. The POC already caught MSVC-specific C++ control-flow/header issues that were invisible from macOS.

For cross-platform core code, add tests that run without creating a native window. Prefer deterministic unit tests for compositing, hierarchy, transforms and pixel algorithms.

## Status at handoff — 2026-09-23

- PR #1 feasibility work is present on `main`.
- All three POC stages passed Windows CI.
- Native Windows rendering was demonstrated.
- Existing Compositor C code was reused successfully.
- A platform-neutral raster was demonstrated.
- A document/layer/group hierarchy with inherited visibility/opacity was demonstrated.
- A second existing C pixel operation was integrated.
- The recommended architecture is a shared portable core + separate platform adapters/frontends.

**Next task for Codex:** create a new branch from `main` and turn the validated concepts into the first production-quality `CompositorCore` extraction plus a production Windows application skeleton. Do not continue evolving `WindowsPoC` as if it were production code.
