# Compositor Windows portability PoC

This prototype answers one narrow question: can Compositor reuse part of its existing rendering core in a native Windows application without rewriting everything first?

## What it proves

- Native Windows executable built with CMake/MSVC.
- Image decoding through Windows Imaging Component (WIC), avoiding Apple ImageIO/CoreImage.
- Rendering through Direct2D, avoiding AppKit/CoreGraphics for the presentation layer.
- Direct reuse of `Compositor/Rendering/LevelsPixels.c` from the existing Compositor codebase.
- Drag-and-drop of an image file into the PoC window.

The current levels lookup tables are identity tables on purpose: the displayed pixels should remain visually unchanged while still executing the original Compositor `levels_apply()` implementation.

## What it does not prove yet

- Portability of the Swift document/session model.
- PSD import on Windows.
- Brushes, selections, transforms or text.
- GPU effects currently implemented with Metal.
- Color-management parity with macOS.
- Production UI architecture.

## Build on Windows

Requirements:

- Windows 10/11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24+

From a Developer PowerShell:

```powershell
cmake -S WindowsPoC -B WindowsPoC/build -A x64
cmake --build WindowsPoC/build --config Release
.\WindowsPoC\build\Release\CompositorWindowsPoC.exe
```

Drop a PNG, JPEG, BMP or TIFF onto the window.

## Next validation milestone

If this PoC builds successfully, the next useful spike is to introduce a platform-neutral raster structure and port a small document containing two composited layers. That will tell us how much of Compositor's document/rendering architecture can be shared before deciding on the final Windows UI stack.
