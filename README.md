# SchwarzschildRayTracer

## Workspace

A fresh workspace shows only the **Viewport**, with the menu and status bars.
Open optional panels from **View**: Scene docks left, Inspector / Camera / Render
Settings dock right, and Timeline docks below. The viewport is always visible and
cannot be hidden or undocked. Resize areas with the splitters.
**View > Lock Layout** controls undocking of optional panels; **View > Reset Layout**
returns to the viewport-only default. Existing saved layouts and panel choices
are preserved between sessions, so use Reset Layout to try the new default.
The status bar shows camera distance to the black-hole center and horizon radius
on the right. Temporary render-update text clears when rendering finishes.

- **File > Save Image...** (P) captures the displayed image and opens a native PNG
  save dialog. **File > Export Animation...** opens video/sequence settings.
- **Edit** offers animation undo/redo and deselection. Escape stops playback,
  cancels an active export, or deselects; use **File > Exit** to close the app.
- **View** toggles optional panels and gizmos. F1 temporarily hides the interface,
  preserving the chosen panels and layout when restored.
- **Help > Controls and Shortcuts** explains navigation and keyframing.

Inspector edits the selected object's or camera's position. Camera navigation
and observer physics live in **Camera**. Observer sliders include negative and
positive velocity with an exact **Zero** button; the combined speed limit only
clamps the edited axis. Render Settings contains image quality and resolution;
integration and seed are under **Advanced**. Docked panels have no close buttons.

Navigation operates inside the focused viewport. Fixed render resolutions retain
their aspect ratio with letterboxing; **Match viewport resolution** follows the
available viewport area at the display's pixel density. Images exclude UI and gizmos.

## Object transforms and interaction tools

Choose a tool in **Inspector > Interaction tool**:

- **Pointer** selects bodies and provides the existing translation arrows and center handle.
  Left-drag outside the handles looks around; a click selects a body.
- **Hand** left-drags a body directly in the camera's view plane, without grabbing a handle.
- **Rotate** selects bodies and displays red X, green Y, and blue Z rotation rings.
  Drag a ring to rotate about that world axis. Right-drag looks around in every tool.

The Inspector accepts **Rotation (degrees)** around X, Y, and Z. These describe
`Rz * Ry * Rx`: X is applied first, then Y, then Z. Equivalent angle triples may
appear after a gizmo rotation, especially near Euler gimbal lock. The stored
orientation uses a quaternion, so rotation and animation remain continuous.
**Reset position** and **Reset rotation** restore those parts of the original pose
independently. Transform edits participate in timeline capture and Undo/Redo.

Rotating Earth, Moon, or Sun turns its surface textures; Earth's normal map turns
with them. Rotating the black hole turns its accretion disk, including its emission
pattern. It does not turn the spherical Schwarzschild gravitational field into a
spinning (Kerr) black hole. Gizmo centers follow the rendered body image in both
observer modes; the handles show world editing axes rather than gravitationally
lensed geometry.

## Animation timeline and video export

The **Timeline** window animates body positions, body rotations, and the camera pose independently.
Animation projects currently live only in memory: closing the app loses the timeline.

1. Select a body in the view or its timeline row (or select **Camera**).
2. Choose a frame using the ruler, frame field, or navigation buttons.
3. Move or rotate the object with the Inspector fields/gizmos, or navigate the camera.
4. Press **Add [object] keyframe**. At an existing key, **Update [object] keyframe** replaces that pose.
5. Repeat at another frame. Drag a diamond to retime it, or select it and press
   **Remove** / Delete. Keys cannot overlap within a track.

Click the viewport background, an empty timeline lane/space, or **Edit > Deselect** / Escape to
clear the selected body and hide its transform gizmo. Deselecting preserves keys
and uncaptured poses. Select a track again before adding/removing keys; use the
ruler to scrub without changing the track selection.

Position interpolation is linear; body and camera orientations follow the shortest rotation.
Outside a track's keys, the nearest key is held. A track with no keys stays at its
base pose. The first key does not create an implicit key at frame zero.
Edits to animated tracks show **Pose not captured** until captured. Seeking, playing,
or exporting discards these edits; **Revert poses** discards them immediately, and Undo can recover them. Unanimated tracks remain
editable as static objects. The history retains 100 commands, grouping each drag
or navigation gesture. Ctrl+Z/Ctrl+Y undo/redo when not editing text; Space toggles
playback when the timeline has focus. F1 hides/shows panels and gizmos.

The default is 300 frames at 30 FPS (frames 0–299, ten seconds). **Animation settings > Apply**
changes the frame count/FPS; FPS changes preserve key frame numbers. Move/remove
outlying keys before reducing the frame count. Resize the Timeline window, use
Zoom, and scroll horizontally to navigate longer clips.

**Play** renders every frame at quarter resolution and one sample. Expensive frames
slow playback rather than being skipped. Pausing refines the current frame.

**Export...** offers numbered PNG frames or H.264 MP4, with destination, resolution,
sample count, and bitrate (default 20 Mbps). It uses committed render settings,
the same seed for each frame, and all timeline frames. MP4 dimensions must be even.
Choose a new destination; existing files/directories are never silently replaced.
Press **Choose location and export...** to open the native Save dialog for MP4,
or a parent-folder picker for PNG sequences (a new animation subfolder is created).
Cancelling the picker returns to export settings without rendering or creating files.

- Windows MP4 uses the system Media Foundation encoder; no FFmpeg installation is
  needed. If media features/encoding are unavailable, the app reports the error
  before rendering and PNG export is still available.
- Linux MP4 uses `ffmpeg` from `PATH` with `libx264` and the `color` source filter.
  The backend performs an encoder/configuration preflight before scene rendering.
  Native Linux validation is deferred to a future Linux session.
- PNG exports use `frame-000000.png` onward in a new directory. Completed PNGs
  remain after cancellation. MP4 exports publish only after finalization and
  remove incomplete temporary output when cancelled.

The UI remains responsive during export, including Cancel, and exporting continues
while minimized. Scene editing is locked until completion/cancellation, after which
the pre-export playhead pose and interactive rendering settings are restored.
Images use the same color processing as Save Image and contain no ImGui overlays.

The portable `FrameSink` interface consumes top-down RGBA8 frames with frame indices
and rational timestamps. It supports asynchronous startup, bounded submission,
progress, finalization, and cancellation. `FrameExport` owns frame ordering and
backpressure without platform APIs; desktop file/process/COM details stay in
`DesktopFrameSink`. A future WebCodecs sink can implement this same contract.

Validation commands: `--test-animation`, `--test-export`, and `--test-export-gpu`.
The export test checks Media Foundation encoding/decoding only on Windows. CTest
also includes the existing CPU/GPU reference suites. Test outputs are placed in
unique directories under the executable's `Output` folder.

## Texture credits

The realistic Earth textures (day, night, clouds, normal and specular maps) and
Sun texture were created by [Solar System Scope](https://www.solarsystemscope.com/textures/)
and are licensed under [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
The TIFF data maps were converted to lossless PNG runtime copies with identical
RGB pixels for use in this app.

## Experiment playground

Click Earth, Moon, Sun, the black-hole shadow, or its accretion disk to select it.
Picking traces the light path using the displayed camera and scene, including lensing.
The **Inspector** provides exact world XYZ coordinates and **Reset position** to restore that body's startup position.

Drag the red X, green Y or blue Z arrow to translate along a world axis. Drag the
yellow center to translate in the view plane. The gizmo origin is anchored to the
largest visible image of the selected body using the renderer's first-sample body
IDs, including observer aberration and gravitational lensing. With multiple lensed
images, the largest connected image is used. Arrows remain world-axis editing
directions, not bent light rays. Invisible bodies have no gizmo but remain editable
through the panel. Anchors update only when a complete new view is displayed.
Left-drag elsewhere or right-drag rotates the camera. Right-drag never selects or moves bodies. F1 hides the panels and gizmo.
Moving the black hole also moves its accretion disk and gravity field. Other bodies
move independently. Scene edits restart progressive rendering without reloading textures.

## Updated scene assets

The black hole is at `(0, 0, 0)`. Earth remains 7 scene units from it; the Sun's
position is `(8, 4, 2)`, preserving its previous distance and relative placement.
The camera and background were translated by the same offset. Interactive windows
start maximized; hidden validation/render windows remain unchanged.

Earth uses the supplied day, night, cloud, normal and specular maps. Solar elevation
smoothly enables city emission across the night-side terminator. Clouds blend over
the surface and attenuate city lights; they are a surface coverage approximation,
not a separate weather simulation. Normal and specular maps are linear data;
color textures decode from sRGB before bilinear filtering, with wrapped longitude.
The TIFF data maps have lossless PNG runtime copies with identical RGB pixels.
All supplied image resolutions are retained (the Sun file is 4096 x 2048).

The Moon is 0.2727 Earth radii, matching the [physical size ratio](https://science.nasa.gov/moon/by-the-numbers/).
Its separation is deliberately compressed for the illustrative composition;
The black hole's accretion flow is a finite-height gas volume, with flared vertical
structure, sheared 3-D clouds, thermal emission, and self-absorption. It has no
opaque disk plane: edge-on rays travel through its thickness, and thinner wisps
transmit background light. Moving or rotating the hole also transforms the gas.
See [the volumetric accretion model](docs/volumetric-accretion.md) for its equations,
parameters, and approximations.

Earth and Moon both receive direct sunlight, disk illumination and cast shadows.
The Sun map's luminance modulates thermal emission with limb darkening; its orange
false color does not tint the illumination. Lower Exposure to inspect the solar
surface; at normal scene exposure the photosphere saturates white. An optically
thin emissive halo follows the ray segments, so it can be occluded and lensed.
Its brightness and streamers are artistic corona approximations, not a plasma model.

High-resolution images use packed RGB bytes in GPU storage instead of four floats
per pixel. The full texture set needs roughly 0.8 GiB, plus ray state and framebuffers;
a GPU with a smaller per-buffer storage limit reports an explicit error.

## Observer velocity and views inside the horizon

**Camera > Observer physics** offers **Hovering** (default) and **Freely falling** observers.
Velocity is view-relative: **Right/Left**, **Up/Down**, and **Forward/Backward**.
The directions rotate with the view, including pitch. Hovering at zero velocity
preserves the original exterior image exactly; freely falling at zero means a
rain observer falling from rest at infinity. The combined speed is constrained to **0.999c** because a
camera rest frame does not exist at exactly c. The camera stays where you place
it; changing velocity modifies aberration and the measured color/brightness.

Observer types never switch automatically. Hovering is undefined at or inside
the horizon and displays black with an explanation in the panel. Select Freely
falling explicitly to visualize the interior; it uses full integration there.
Outside, the selected integration mode is retained. Freely falling with full-scene
integration on both sides provides a consistent horizon crossing.

The Inspector edits world position; Camera shows view-relative velocity and distance
to the black-hole center. The horizon radius is 1 scene unit. **Reset camera pose**
restores the startup camera without changing observer velocity or render settings.
F1 hides or restores the workspace.

See [Observer and horizon equations](docs/observer-and-horizon.md) for the English
derivation, reference-frame conventions, frequency shifts, validation, and model
limitations. Interior light sources and gravitational collapse history are not
modeled; some past directions therefore remain dark. Exposure can help inspect
faint incoming light.
This is a relativistic ray tracer based on the Schwarzschild metric.

The current renderer uses **OpenGL 4.3 compute shaders** to trace rays,
integrate their paths, and accumulate samples on the GPU. The application shows
a progressive render; C++ and SDL provide the controls and PNG export.

![ExampleImage](https://raw.githubusercontent.com/CarlosManuelRodr/SchwarzschildRayTracer/master/Images/Animation.gif)

## Download

The following download is for the legacy CPU version. To use the GPU renderer,
build the current code with the instructions below:

[Download](https://github.com/CarlosManuelRodr/SchwarzschildRayTracer/releases/download/v1.0/SchwarzschildRayTracer_x64.zip)

If the application fails to start, run the `vcredist_x64.exe` installer next to
the main executable.

## Design

The implementation is based on a ray tracer built from Peter Shirley's excellent
[book](http://in1weekend.blogspot.com/2016/01/ray-tracing-in-one-weekend.html "raytracing"), *Ray Tracing in One Weekend*.

The calculation of the ray-deflection equation is based on the derivation for the
[starless](http://rantonels.github.io/starless/) project and its
[documentation](http://spiro.fisica.unipd.it/~antonell/schwarzschild/). The
`docs/DesignDocument.nb` notebook contains the derivation steps.

Additional information is available in the following article:
*Orbits of massless particles in the Schwarzschild metric: Exact solutions American Journal of Physics 82, 564 (2014)*

## Building

The project uses CMake and [vcpkg](https://vcpkg.io/) on Windows and Linux. The
`vcpkg.json` manifest automatically installs SDL 3.4.16, Dear ImGui, and GLEW
during configuration. SDL is pinned to exactly version 3.4.16 in both the
manifest and CMake.

Requirements:

- CMake 3.21 or later
- A C++17-compatible compiler
- A GPU and driver supporting OpenGL 4.3 (NVIDIA, AMD, or Intel); CUDA is not required
- vcpkg, with the `VCPKG_ROOT` environment variable pointing to its directory

```sh
cmake --preset default
cmake --build --preset default
```

The executable is generated in `build/` (or `build/Release/` with multi-config
generators). CMake copies textures and shaders next to the executable on every
build. Assets are found next to the executable regardless of the working
directory; `--assets DIR` selects another asset folder.

Without presets, configure the toolchain explicitly:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

SDL manages the window, OpenGL context, keyboard/mouse events, and PNG export
through [`SDL_SavePNG`](https://wiki.libsdl.org/SDL3/SDL_SavePNG). JPEG textures
use the bundled `stb_image.h`, because SDL 3.4.16 natively loads only BMP and PNG.
SDL_image is not required. This migration retains the desktop OpenGL 4.3 backend;
WebAssembly support and a browser-compatible backend remain future work.

## Usage

At startup, a maximized window opens with docked **Scene**, **Viewport**,
**Inspector**, **Camera**, **Render Settings**, and **Timeline** panels. Valid
changes apply automatically; render options restart sampling after 150 ms without
edits while preserving the last complete image.

- **Render Settings:** samples, exposure, resolution, and redshift. Integration
  and seed are in **Advanced**. *Match viewport resolution* follows the available
  viewport area; a fixed resolution retains its aspect ratio.
- **Camera:** precision navigation and physical observer velocity. Ctrl-click a
  slider to enter an exact value.
- **File > Save Image...:** opens a dialog to save the UI-free PNG image.
- **View:** shows/hides panels, locks docking, and restores the layout. **F1**
  temporarily hides/restores the complete interface. The layout is saved.

Navigation keys work when the viewport is focused. Controls and shortcuts are
described in **Help > Controls and Shortcuts**.


Console options are retained for compatibility, initial configuration, testing,
and batch renders; interactive use requires no arguments:

```powershell
.\build\Release\SchwarzschildRayTracer.exe
.\build\Release\SchwarzschildRayTracer.exe --width 800 --height 600 --samples 30 --seed 1
.\build\Release\SchwarzschildRayTracer.exe --render --samples 96 --exposure 1
.\build\Release\SchwarzschildRayTracer.exe --no-redshift
```

On Linux, use `./build/SchwarzschildRayTracer` with the same options.

Integration modes (mutually exclusive; they also work with `--render` and
`--benchmark`):

| Option | Behavior |
| --- | --- |
| No additional option | **Default:** fixed radius 5.5, straight rays outside and integration inside, as before |
| `--full-scene-integration` | Continuous integration from the camera to objects/background |
| `--adaptative` | Cuts off according to estimated per-path error; integrates when curvature cannot be skipped at the configured tolerance |

The console and title show the active mode. Combining both flags produces an
explicit error, and navigation previews retain the selected mode. In
`--adaptative`, the maximum acceleration is estimated inside a tube around the
straight segment to the next object or background. The straight segment is
accepted when estimated displacement and direction-change bounds satisfy
`RenderSettings::absoluteTolerance` and `relativeTolerance` (1e-6 and 1e-4).
Segments passing within three horizon radii are never skipped. This is a
conservative approximation for each segment between surfaces, not a guarantee
of final per-pixel error or identical grazing-silhouette visibility. It can cost
as much as full integration when insufficient curvature can be discarded.

`--exposure N` controls linear exposure (positive; default 1). `--slow-step N`
sets the distance of a Shift tap in horizon radii (0 < N < 0.05; default 0.0005).
Holding a movement key moves at 20*N units per second; either Shift key can be
pressed or released while moving. `--no-redshift` disables gravitational and
Doppler shifts for comparison. `--render` saves a GPU render to
`Output/render-gpu.png` and exits without running the CPU benchmark.

| Control | Action |
| --- | --- |
| Left-click + drag | Rotate the view horizontally and vertically |
| Up / W, Down / S | Move forward / backward in the view direction |
| Left / A, Right / D | Move sideways relative to the view |
| Q / E | Raise / lower the camera and its target |
| Shift + movement keys | Precision movement (100 times slower by default) |
| P | Choose a location and save the displayed image as PNG |
| F1 | Hide / restore the interface |
| Escape | Stop playback, cancel export, or deselect |

During navigation, the application calculates one-sample previews at one quarter
of render width and height (200x150 for an 800x600 window). A preview is shown
only after every pixel completes, so the last complete image remains visible
until then. After 150 ms without movement, rendering returns to full resolution
and the sample target (30 by default). The title shows `preview` or `refine`,
working resolution, average samples, latest GPU-batch time, and invalid rays.
Camera movement and position fields prevent collisions with solid bodies,
including Earth, Sun, and Moon; the horizon, gravitational region, and star
background remain traversable. Very large windows are limited by the driver's
SSBO capacity and a 256 MiB ray-state budget while preserving aspect ratio.

On hybrid laptops, the executable requests the high-performance GPU using the
[NVIDIA](https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf)
and [AMD](https://gpuopen-librariesandsdks.github.io/doc/AMD-CrossFire-guide-for-Direct3D11-applications.pdf)
guidance. System/user selection takes precedence. If the required OpenGL context
cannot be created or a shader cannot compile, the program exits with an explicit
error; it does not silently switch to the CPU renderer. macOS is unsupported by
this OpenGL 4.3 backend.

## Numerical model and architecture

- `SceneData` stores spheres, materials, textures, and linear RGB texels with
  indices and 16-byte-aligned blocks. It supports Lambertian, Metal, Dielectric,
  DiffuseLight, Schwarzschild, Earth, Environment, Constant, Checker, and Image.
- `GpuRenderer` receives a scene and `RenderSettings`, resets the camera,
  dispatches work, polls for completion, and presents accumulation with a triangle.
  Only one batch is in flight; framebuffer download is reserved for export,
  validation, and benchmarking.
- Each invocation performs up to 64 ray transitions before preserving state in
  an SSBO. Long rays continue in later dispatches; memory barriers and a fence
  coordinate compute, presentation, and readback.
- `assets/shaders/TraceCore.inl` contains equations shared by GLSL (`float`) and
  the CPU reference (`double`). The random generator is independent per
  pixel/sample and reproducible with a fixed seed.
- With `--full-scene-integration`, gravity acts throughout the scene from camera
  to surface or background, with a horizon radius of 1. The historical 5.5
  Schwarzschild-coordinate radius is not used in this mode.
- `r = position - center` and `h^2 = |r x velocity|^2` give
  `acceleration = -1.5 h^2 r / |r|^5`. Adaptive RK4 compares one full step with
  two half steps using relative tolerance `1e-4`, absolute tolerance `1e-6`, and
  base step `0.05`. Velocity is not normalized between steps.
- The stellar-background sphere ends rays as an emissive surface. Rays support
  up to 50 interactions and 16,384 gravity-step attempts between scattering
  events. Nonfinite values or an exhausted budget terminate a ray and increment
  diagnostics; horizon capture is a normal outcome.
- Radiance accumulates in linear HDR. Display and PNG output apply exposure,
  soft highlight bloom, an ACES-like filmic curve, and sRGB encoding.

Legacy v1.0 tracer headers remain as historical reference and are not part of
the compiled pipeline.

## Disk, redshift, and lighting

The accretion disk is an emitting and absorbing gas volume from 3 to 5.2 horizon
radii, with a Gaussian height of 0.10 radii at its outer edge. Its inner edge is
the innermost stable circular Schwarzschild orbit. Temperature follows
`T^4 proportional to r^-3 (1 - sqrt(r_in/r))`, with a configurable 6000 K peak
in the initial scene. This is the profile of a
[zero-torque thin disk](https://www.aanda.org/articles/aa/pdf/2013/12/aa21424-13.pdf).
Integrated segments reveal the far side and gravitationally lensed secondary
images; static procedural emissivity perturbation adds irregular structure but
does not simulate fluid dynamics.

The gravitational factor between emitter and observer is `alpha(r) = sqrt(1 - 1/r)`.
Circular disk gas also receives relativistic Doppler shift, time dilation, and
asymmetric brightness. Thermal sources are evaluated at `g*T`; spectral transfer
preserves `I_nu/nu^3`. Planck-to-RGB conversion uses the
[Wyman, Sloan, and Shirley CIE approximations](https://jcgt.org/published/0002/02/01/paper.pdf).
RGB sources without a spectrum use the bolometric `g^4` approximation.

The Sun emits as a 5778 K blackbody with limb darkening. Earth receives direct
Sun and disk area lighting, shadows, geometric falloff, diffuse reflection, and
GGX ocean highlights. An exponential atmosphere adds extinction and simple
Rayleigh scattering. Lighting and shadow paths to sources are straight; the
renderer does not solve geodesics between every surface and light. Kerr rotation,
fluid dynamics, and temporal disk evolution are not modeled.

`SceneData::disk` configures radii, temperature, intensity, normal, scale height,
and extinction. `atmosphereHeight` controls atmospheric thickness. For
DiffuseLight materials, `parameters.y/z/w` represent temperature, radiance scale,
and limb coefficient; zero temperature preserves the material's original RGB
emission.

## Tests and performance

```sh
ctest --test-dir build -C Release --output-on-failure
# CPU-only tests on machines without a graphics context:
ctest --test-dir build -C Release -R cpu_reference --output-on-failure
```

`--test-cpu` and `--test-gpu` can also run directly. GPU tests require a graphics
session and create a hidden context. They cover capture, escape, grazing and
near-critical rays against double precision, translation invariance, CPU
convergence, materials/textures, cancellation, resizing, reproducibility, sRGB
presentation, PNG orientation, resource failures, disk thermal behavior,
gravitational/Doppler factors, solar limb darkening, Earth lighting, and CPU/GPU
image agreement. Small rounding and discontinuity-related path differences are
allowed; binary CPU/GPU equality is not required.

```powershell
.\build\Release\SchwarzschildRayTracer.exe --benchmark
```

The benchmark defaults to 800x600 and 30 samples, warms shaders, measures GPU
compute, GPU completion including readback, and the CPU reference under the same
scene, seed, and physical parameters. It writes `benchmark-gpu.png` and
`benchmark-cpu.png` to `Output/` and reports RMSE and invalid rays. Measurements
exclude initial compilation, asset loading, and PNG encoding. Performance depends
on hardware; AMD and Linux have not yet received local validation for the newest
lighting path.

Local comparison on September 9, 2026 (Release, RTX 4070 Laptop GPU), with
complete-pass presentation in both versions:

| 800x600, 30 samples | 5.5-radius cutoff | Full-scene integration |
| --- | ---: | ---: |
| GPU compute | 0.970 s | 2.420 s |
| GPU completion including readback | 1.047 s | 2.508 s |
| Largest GPU batch | 40.79 ms | 130.54 ms |
| Invalid GPU rays | 0 | 0 |

The new 200x150, one-sample preview took 54 ms including readback, with a
maximum batch of 16.52 ms. These are render times without presentation/VSync,
not a guarantee of interactive latency. At 30 samples, the CPU/GPU comparison
reported linear RGB RMSE 0.005835 and zero invalid rays in both paths.

An earlier historical measurement, before complete-pass presentation and
first-sample priority (Release with disk, redshift, and new lighting on an RTX
4070 Laptop GPU under Windows, September 8, 2026), was:

| Measurement, 800x600, 30 samples | Result |
| --- | ---: |
| GPU compute | 0.500 s |
| GPU including readback | 0.536 s |
| CPU reference, all cores, double | 18.25 s |
| Completion speedup | 34.1x |
| Largest GPU batch | 38.39 ms |
| Linear RGB RMSE | 0.000953 |
| Invalid CPU / GPU rays | 0 / 0 |

Interactive presentation also depends on VSync and system scheduling, so these
times are not equivalent to window FPS. The new lighting was validated on the
RTX 4070 Laptop GPU under Windows. The earlier backend was also tested on
integrated Intel Arc graphics, but the new lighting has not been retested there.
