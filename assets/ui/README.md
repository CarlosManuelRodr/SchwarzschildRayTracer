# Inspector tool icons

The SVG originals map to Pointer (`cursor.svg`), Hand (`hand.svg`), and Rotate
(`rotate.svg`). Their transparent 96px PNG copies are used by the OpenGL UI at
24 logical pixels, allowing crisp rendering on high-density displays.

To regenerate the PNG copies after editing an SVG, run from the repository root
with ImageMagick installed (only needed for asset conversion, not for builds):

```powershell
foreach ($name in 'cursor', 'hand', 'rotate') {
    magick -background none -density 384 "assets/ui/$name.svg" -resize 96x96 "assets/ui/$name.png"
}
```

CMake copies and installs both formats with the other runtime assets.
