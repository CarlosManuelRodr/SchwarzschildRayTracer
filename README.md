# SchwarzschildRayTracer

## Animation timeline and video export

The **Timeline** window animates body positions and the camera pose independently.
Animation projects currently live only in memory: closing the app loses the timeline.

1. Select a body in the view or its timeline row (or select **Camera**).
2. Choose a frame using the ruler, frame field, or navigation buttons.
3. Move the object with its existing gizmo/XYZ fields, or navigate the camera.
4. Press **Add Keyframe**. At an existing key, **Update Keyframe** replaces that pose.
5. Repeat at another frame. Drag a diamond to retime it, or select it and press
   **Remove Keyframe** / Delete. Keys cannot overlap within a track.

Click the viewport background, an empty timeline lane/space, or **Deselect** to
clear the selected body and hide its transform gizmo. Deselecting preserves keys
and uncaptured poses. Select a track again before adding/removing keys; use the
ruler to scrub without changing the track selection.

Position interpolation is linear; camera orientation follows the shortest rotation.
Outside a track's keys, the nearest key is held. A track with no keys stays at its
base pose. The first key does not create an implicit key at frame zero.
Edits to animated tracks show **Unkeyed changes** until captured. Seeking, playing,
or exporting discards these edits; Undo can recover them. Unanimated tracks remain
editable as static objects. The history retains 100 commands, grouping each drag
or navigation gesture. Ctrl+Z/Ctrl+Y undo/redo when not editing text; Space toggles
playback when the timeline has focus. F1 hides/shows panels and gizmos.

The default is 300 frames at 30 FPS (frames 0–299, ten seconds). **Apply timing**
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
Images use the same color processing as Save PNG and contain no ImGui overlays.

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
The **Body transform** panel provides exact world XYZ coordinates, a body selector,
and **Reset position** to restore that body's startup position.

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

The Physical state panel offers **Hovering** (default) and **Freely falling** observers.
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

The Physical state panel shows editable world position, view-relative velocity,
distance to the black-hole center, and the event horizon radius (1 scene unit).
Reset position and view restores the startup camera without changing velocity
or render settings. F1 hides or shows both panels.

See [Observer and horizon equations](docs/observer-and-horizon.md) for the English
derivation, reference-frame conventions, frequency shifts, validation, and model
limitations. Interior light sources and gravitational collapse history are not
modeled; some past directions therefore remain dark. Exposure can help inspect
faint incoming light.
Ray tracer con desviación relativista de rayos de luz basado en la métrica de Schwarzschild.

El renderizador actual usa **OpenGL 4.3 compute shaders** para trazar los rayos,
integrar sus trayectorias y acumular muestras en la GPU. La ventana muestra un
render progresivo; C++ y SDL se encargan de los controles y la exportación PNG.

![ExampleImage](https://raw.githubusercontent.com/CarlosManuelRodr/SchwarzschildRayTracer/master/Images/Animation.gif)

## Descargar
La siguiente descarga corresponde a la versión antigua de CPU. Para usar el
renderizador GPU, compila el código actual con las instrucciones de abajo:

[Descargar](https://github.com/CarlosManuelRodr/SchwarzschildRayTracer/releases/download/v1.0/SchwarzschildRayTracer_x64.zip)

En caso de que la aplicación falle al iniciar es necesario ejecutar el instalador `vcredist_x64.exe` que se encuentra junto al ejecutable principal.

## Diseño
La implementación está basada en un ray tracer construido a partir del excelente [libro](http://in1weekend.blogspot.com/2016/01/ray-tracing-in-one-weekend.html "raytracing") *Ray Tracing in One Weekend* de Peter Shirley.

El cálculo de la ecuación que simula la desviación de un rayo está basado en la derivación hecha para el proyecto [starless](http://rantonels.github.io/starless/) y su [documentación](http://spiro.fisica.unipd.it/~antonell/schwarzschild/).
La libreta que se encuentra "*References/DesignDocument.nb*" contiene todos los pasos del desarrollo de las ecuaciones.

Se puede encontrar información adicional en el siguiente artículo:
*Orbits of massless particles in the Schwarzschild metric: Exact solutions American Journal of Physics 82, 564 (2014)*

## Compilación

El proyecto usa CMake y [vcpkg](https://vcpkg.io/) en Windows y Linux. El
manifiesto `vcpkg.json` instala automáticamente SDL 3.4.16, Dear ImGui y GLEW durante la
configuración. SDL se fija a la versión exacta 3.4.16 tanto en el manifiesto
como en CMake.

Requisitos:

- CMake 3.21 o posterior
- Un compilador compatible con C++17
- Una GPU y controlador con OpenGL 4.3 (NVIDIA, AMD o Intel); no requiere CUDA
- vcpkg, con la variable de entorno `VCPKG_ROOT` apuntando a su directorio

```sh
cmake --preset default
cmake --build --preset default
```

El ejecutable se genera en `build/` (o en `build/Release/` con generadores
multiconfiguración). CMake copia las texturas y shaders junto al ejecutable en
cada compilación. Los recursos se buscan junto al ejecutable, independientemente
del directorio de trabajo. `--assets DIR` permite especificar otra carpeta.

Sin presets, se puede configurar explícitamente el toolchain:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

SDL gestiona la ventana, el contexto OpenGL, los eventos de teclado/ratón y
la exportación PNG con [`SDL_SavePNG`](https://wiki.libsdl.org/SDL3/SDL_SavePNG).
Las texturas JPEG usan `stb_image.h`, ya incluido en el repositorio: SDL 3.4.16
solo carga BMP/PNG de forma integrada. No se necesita SDL_image.
Esta migración conserva el backend OpenGL 4.3 de escritorio. La compilación
WebAssembly y un backend compatible con navegador quedan para una etapa posterior.

## Uso

Al iniciar se abre una ventana de **1440×900** con un panel **Dear ImGui** sobre el render. Los cambios válidos
se aplican automáticamente; las opciones de render reinician las muestras tras
150 ms sin editar, conservando la última imagen completa. No hay botón de aplicar.

- **Rendering:** selector de integración (Fixed radius, Full scene, Adaptive cutoff),
  redshift, muestras y exposición.
- **Resolution and sampling:** semilla y resolución. Desactiva *Match window resolution*
  para elegir una resolución interna independiente del tamaño de la ventana.
- **Navigation:** deslizador logarítmico de *Slow step*. Ctrl+clic permite escribir
  en el deslizador; *Step value* admite valores exactos (0 < valor < 0.05).
  La velocidad de precisión cambia inmediatamente, sin reiniciar el render.
- **Save PNG:** exporta la imagen mostrada, sin incluir el panel. Los errores y la
  ruta del archivo aparecen en el panel.

La flecha del título colapsa el panel; **F1** lo oculta o vuelve a mostrar, incluso
si se cerró con X. Las secciones también se pueden colapsar. Mientras se editan
controles, el teclado y el ratón quedan capturados por la interfaz y no mueven
la cámara. `SettingsPanel` mantiene los widgets y sus valores separados del bucle
de render, para añadir nuevas secciones sin mezclar su código con OpenGL.

Las opciones de consola se conservan para compatibilidad, configuración inicial,
pruebas y renders por lotes; el uso interactivo no necesita argumentos:

```powershell
.\build\Release\SchwarzschildRayTracer.exe
.\build\Release\SchwarzschildRayTracer.exe --width 800 --height 600 --samples 30 --seed 1
.\build\Release\SchwarzschildRayTracer.exe --render --samples 96 --exposure 1
.\build\Release\SchwarzschildRayTracer.exe --no-redshift
```

En Linux, usa `./build/SchwarzschildRayTracer` con las mismas opciones.

Modos de integración (excluyentes; también funcionan con `--render` y `--benchmark`):

| Opción | Comportamiento |
| --- | --- |
| Sin opción adicional | **Predeterminado:** radio fijo 5.5, rayos rectos fuera e integración dentro, como antes |
| `--full-scene-integration` | Integración continua desde la cámara hasta los objetos/fondo |
| `--adaptative` | Corte según error estimado por trayectoria; integra cuando no puede omitir la curvatura con la tolerancia configurada |

La consola y el título muestran el modo activo. Combinar los dos flags produce
un error explícito. Las vistas previas de navegación conservan el modo elegido.
En `--adaptative`, se estima la aceleración máxima dentro de un tubo alrededor
del segmento recto hasta el siguiente objeto, disco o fondo. Se acepta el tramo
recto si los límites estimados de desplazamiento y cambio de dirección satisfacen
`absoluteTolerance` y `relativeTolerance` de `RenderSettings` (1e-6 y 1e-4).
No se omiten tramos que pasan a menos de tres radios de horizonte. Es una
aproximación conservadora por tramo entre superficies, no una garantía de error
final por píxel ni de visibilidad idéntica en siluetas rasantes. Puede costar tanto
como la integración completa cuando no logra descartar suficiente curvatura.

`--exposure N` controla la exposición lineal (positivo; 1 por defecto).
`--slow-step N` configura la distancia de un toque con Shift, en radios de horizonte
(0 < N < 0.05; predeterminado 0.0005). Mantener las teclas mueve a 20*N unidades
por segundo; ambos Shift funcionan y pueden pulsarse/soltarse durante el movimiento.
Por ejemplo, `--slow-step 0.0001` permite pasos de 0.0001 y velocidad de 0.002
unidades por segundo mientras se mantiene Shift.
`--no-redshift` desactiva los cambios gravitatorios y Doppler para comparar.
`--render` guarda un render GPU en `Output/render-gpu.png` y termina, sin ejecutar
el benchmark CPU. La ventana interactiva conserva la exportación manual con P.

| Control | Acción |
| --- | --- |
| Clic izquierdo + arrastrar | Girar la vista (derecha/izquierda y arriba/abajo) |
| ↑ / W, ↓ / S | Avanzar / retroceder en la dirección de la vista |
| ← / A, → / D | Desplazarse lateralmente respecto a la vista |
| Q / E | Subir / bajar la cámara y su objetivo |
| Shift + flechas / WASD / Q / E | Movimiento de precisión (100 veces más lento por defecto) |
| P | Guardar las muestras disponibles como PNG en `Output/`, junto al ejecutable |
| F1 | Ocultar / mostrar el panel de ajustes |
| Escape | Salir (si la interfaz no está capturando el teclado) |

Durante la navegación se calculan vistas previas de una muestra a un cuarto del
ancho y alto de render (200×150 para una ventana de 800×600). Solo se muestran al
terminar todos sus píxeles; mientras tanto permanece la última imagen completa.
Cada vista previa termina antes de recoger la posición más reciente de la cámara,
evitando que el movimiento continuo cancele siempre los rayos lentos.
Después de 150 ms sin movimiento, al terminar la vista previa en curso, se vuelve
a resolución completa y al objetivo de muestras (30 por defecto). La primera
pasada completa también se exige antes de sustituir la vista previa. Los píxeles
rápidos esperan a los lentos antes de empezar la segunda muestra; después se
refina progresivamente. Esto elimina el falso crecimiento de la sombra provocado
por píxeles sin calcular, a cambio de la latencia de una pasada completa.
La barra de título indica `preview` o `refine`, resolución de trabajo, promedio de
muestras, tiempo del último lote GPU y rayos inválidos. P exporta la imagen
completa mostrada, con su resolución y exposición, incluso mientras se calcula
otra vista. No se guardan imágenes automáticamente en modo interactivo.
La rotación conserva la posición de la cámara y limita la inclinación a ±89°
para evitar giros invertidos. Arrastrar termina al soltar el botón, salir de la
ventana, cambiar su tamaño o perder el foco. WASD y las flechas son equivalentes;
mantener ambas teclas de la misma dirección no duplica la velocidad.
Al ampliar mucho la ventana, la resolución interna se ajusta al límite de SSBO
del controlador (y a un presupuesto de 256 MiB de estados), conservando la
proporción. La barra de título muestra la resolución interna; el PNG usa ese
tamaño. Esto evita que maximizar la ventana exceda la memoria admitida por el
shader. Las dimensiones explícitas de línea de comandos que excedan el límite
producen un error en lugar de cambiar silenciosamente el benchmark.

En portátiles híbridos, el ejecutable solicita la GPU de alto rendimiento mediante
las indicaciones de [NVIDIA](https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf)
y [AMD](https://gpuopen-librariesandsdks.github.io/doc/AMD-CrossFire-guide-for-Direct3D11-applications.pdf).
La selección del sistema/usuario tiene prioridad; confirma la GPU en el título.
Si no se puede crear el contexto OpenGL requerido o compilar un shader, el programa
termina con un error explícito; no cambia silenciosamente al renderizador CPU.
macOS no está soportado por este backend OpenGL 4.3.

## Modelo numérico y arquitectura

- `SceneData` almacena esferas, materiales, texturas y texels RGB lineales mediante
  índices y bloques con alineación de 16 bytes. Soporta Lambertian, Metal,
  Dielectric, DiffuseLight, Schwarzschild, Earth, Environment y texturas Constant,
  Checker e Image.
- `GpuRenderer` recibe una escena y `RenderSettings`, reinicia la cámara, despacha
  trabajo, consulta su finalización y presenta la acumulación con un triángulo.
  El propietario mantiene un contexto OpenGL activo hasta destruir el renderer.
  Se llama `poll()` entre despachos; solo hay un lote en vuelo. El framebuffer se
  descarga únicamente para exportación, validación o benchmark.
- Cada invocación realiza hasta 64 transiciones de un rayo antes de guardar su
  estado en un SSBO; también cede el control al completar una muestra para acotar
  los lotes con iluminación costosa. Los rayos largos continúan en despachos posteriores. Las
  barreras de memoria y un fence coordinan cómputo, presentación y lectura.
- `assets/shaders/TraceCore.inl` contiene las ecuaciones compartidas entre GLSL
  (float) y la referencia CPU (double). El RNG es independiente por píxel/muestra
  y reproducible con una semilla fija. La CPU de referencia usa todos los núcleos,
  sin el generador aleatorio global compartido del código anterior.
- Con `--full-scene-integration`, la gravedad actúa **en toda la escena**, desde la cámara hasta la superficie o
  el fondo, con un horizonte de radio 1. El radio histórico 5.5 del registro
  Schwarzschild no interviene en las trayectorias de este modo; no hay transición a rayos
  rectos. Se sigue admitiendo un único agujero negro sin rotación.
- `r = posición - centro` y `h² = |r × velocidad|²` dan
  `aceleración = -1.5 h² r / |r|⁵`. La integración RK4 adaptativa compara un paso
  completo con dos medios pasos: tolerancia relativa `1e-4`, absoluta `1e-6`, paso
  base `0.05`. En modo fijo, este valor es el máximo absoluto dentro de la región.
  En los otros modos, el límite crece suavemente como `max(1, r²/9)` lejos del agujero,
  acotado además por distancia radial, proximidad a superficies, espesor atmosférico
  y error de curvatura del segmento. La velocidad no se normaliza entre pasos.
  Los cruces de horizonte y superficies se detectan sobre segmentos aceptados;
  su localización converge al reducir el paso y las tolerancias.
- La esfera de fondo estelar (radio 200 en la escena inicial) termina los rayos
  como superficie emisiva. En escenas de prueba sin fondo envolvente, el cielo
  procedural se evalúa al salir de una esfera que contiene los objetos (radio
  mínimo 10); no es una continuación rectilínea ni una solución hasta el infinito.
- Se admiten objetos interceptados durante toda la integración. Se limitan las
  trayectorias a 50 interacciones y cada tramo entre dispersiones
  gravitatorio a 16,384 intentos. Agotar el presupuesto o producir valores no
  finitos termina el rayo negro y aumenta el diagnóstico; capturarlo por el
  horizonte es un resultado normal. Las órbitas casi críticas pueden necesitar
  más trabajo y ser sensibles a precisión/tolerancia.
- La radiancia se acumula en HDR lineal. Para pantalla y PNG se aplica exposición,
  bloom suave de altas luces, una curva fílmica tipo ACES y codificación sRGB.
  El bloom representa dispersión óptica de la cámara, no gas adicional.
  La lectura para validación conserva los valores lineales sin esos efectos.

Los headers antiguos del trazador v1.0 permanecen como referencia histórica;
no forman parte del pipeline compilado.

## Disco, redshift e iluminación

El disco es una superficie opaca de dos caras, entre 3 y 5.2 radios de horizonte.
Su borde interior coincide con la órbita circular estable más interna de
Schwarzschild. La temperatura sigue `T⁴ ∝ r⁻³ (1 − √(r_in/r))`, con un máximo
configurable de 6000 K en la escena inicial. Es el perfil de un
[disco delgado con torque nulo en el borde interior](https://www.aanda.org/articles/aa/pdf/2013/12/aa21424-13.pdf).
Las intersecciones siguen los segmentos integrados, por lo que aparecen la cara
lejana y las imágenes secundarias por lente gravitatoria. Una perturbación
procedural estática de la emisividad añade estructura irregular; no simula fluidos.

Se calcula el factor gravitatorio entre emisor y observador con
`α(r) = √(1 − 1/r)`. Para gas en órbita circular se combina con Doppler relativista,
incluyendo dilatación temporal y brillo asimétrico. La dirección del fotón se
evalúa en el marco ortonormal del observador estático local. Las fuentes térmicas
se evalúan a `g*T`: la transformación espectral conserva `Iν/ν³`, sin aplicar
otra vez un factor bolométrico. La conversión de Planck a RGB integra el espectro
visible usando las [aproximaciones CIE de Wyman, Sloan y Shirley](https://jcgt.org/published/0002/02/01/paper.pdf),
en una tabla compartida por CPU/GPU. Para fuentes RGB sin espectro se usa la
aproximación bolométrica `g⁴`, que cambia intensidad pero no reconstruye colores
espectrales. El redshift se evalúa en toda la escena en los tres modos; la
integración geométrica sigue la opción elegida.

El Sol emite como cuerpo negro de 5778 K con oscurecimiento hacia el limbo.
La Tierra recibe iluminación directa del Sol y del disco mediante muestreo de
área, con sombras, caída geométrica, reflexión difusa y reflejos GGX en el océano.
Una atmósfera exponencial añade extinción y dispersión Rayleigh simple de la luz
solar. El mapa de color existente aproxima la distinción océano/tierra por color;
no contiene máscaras físicas, relieve, nubes volumétricas ni luces nocturnas.
El fondo estelar es emisivo. Las intensidades, tamaños y separaciones de esta
escena ilustrativa no representan el Sistema Solar a escala física.

`SceneData::disk` configura radios, temperatura, intensidad y normal;
`atmosphereHeight` configura el espesor atmosférico. En materiales DiffuseLight,
`parameters.y/z/w` representan temperatura, escala de radiancia y coeficiente
de limbo. Una temperatura cero conserva la emisión RGB del material original.

Las conexiones de iluminación y sombra hacia las fuentes son rectas; no se
resuelven geodésicas entre cada superficie y cada luz. La Tierra usa iluminación
directa, y la atmósfera dispersión simple solar, sin iluminación volumétrica del
disco ni múltiples rebotes atmosféricos. El disco carece de espesor, dinámica,
autoabsorción volumétrica y evolución temporal. No se implementa rotación Kerr.

## Pruebas y rendimiento

```sh
ctest --test-dir build -C Release --output-on-failure
# Solo pruebas CPU en máquinas sin contexto gráfico:
ctest --test-dir build -C Release -R cpu_reference --output-on-failure
```

También se pueden ejecutar `--test-cpu` y `--test-gpu` directamente. Las pruebas
GPU requieren una sesión gráfica y crean un contexto oculto. Comprueban captura,
escape, rayos rasantes y casi críticos contra double, invariancia por traslación,
convergencia CPU, todos los materiales/texturas, cancelación, cambio de tamaño,
repetibilidad, presentación sRGB, orientación PNG y errores de recursos.
También verifican el perfil térmico del disco, los factores gravitatorios y Doppler,
el limbo solar, iluminación diurna/nocturna y oclusión en la Tierra, y rayos del
disco comparados entre GPU y double. La imagen completa compara ambas rutas con
la nueva atmósfera, iluminación y transformación de pantalla.
La comparación de imágenes admite diferencias pequeñas por redondeo y caminos
divergentes cerca de discontinuidades; no exige igualdad binaria CPU/GPU.

```powershell
.\build\Release\SchwarzschildRayTracer.exe --benchmark
```

El benchmark usa 800×600 y 30 muestras por defecto, calienta los shaders y mide
por separado cómputo GPU, finalización incluyendo lectura y referencia CPU con
la misma escena, semilla y parámetros físicos. Guarda `benchmark-gpu.png` y
`benchmark-cpu.png` en `Output/`; informa RMSE y rayos inválidos. La CPU usa double
y la GPU float: la aceleración medida incluye esa diferencia de precisión.
Las mediciones excluyen compilación inicial, carga de recursos y codificación PNG.

Comparación local del 9 de septiembre de 2026, Release, RTX 4070 Laptop GPU,
con presentación de pasadas completas en ambas versiones:

| 800×600, 30 muestras | Corte a radio 5.5 | Integración en toda la escena |
| --- | ---: | ---: |
| Cómputo GPU | 0.970 s | 2.420 s |
| Finalización GPU incluyendo lectura | 1.047 s | 2.508 s |
| Mayor lote GPU | 40.79 ms | 130.54 ms |
| Rayos inválidos GPU | 0 | 0 |

La nueva vista previa de 200×150 y una muestra tardó 54 ms incluyendo lectura,
con un lote máximo de 16.52 ms. Son tiempos de render sin presentación/VSync,
no una garantía de latencia interactiva. La comparación CPU/GPU a 30 muestras
dio RMSE RGB lineal 0.005835 y cero rayos inválidos en ambas rutas. Se verifican
además independencia respecto al radio antiguo, curvatura exterior y continuidad
al cruzar 5.5. Las conexiones directas de luz/sombra conservan la aproximación
rectilínea descrita arriba.

Medición histórica anterior a la presentación de pasadas completas y prioridad
de primera muestra, Release con disco, redshift y nueva iluminación, Windows,
RTX 4070 Laptop GPU (8 de septiembre de 2026):

| Medida, 800×600, 30 muestras | Resultado |
| --- | ---: |
| Cómputo GPU | 0.500 s |
| GPU incluyendo lectura | 0.536 s |
| Referencia CPU, todos los núcleos, double | 18.25 s |
| Aceleración de finalización | 34.1× |
| Mayor lote GPU | 38.39 ms |
| RMSE RGB lineal | 0.000953 |
| Rayos inválidos CPU / GPU | 0 / 0 |

La presentación interactiva también depende de VSync y de la planificación del
sistema, por lo que estos tiempos no equivalen a FPS de la ventana. Se validó
la nueva iluminación en RTX 4070 Laptop en Windows. El backend anterior también
se probó en Intel Arc integrado, pero la nueva iluminación no se ha vuelto a
validar allí. AMD y Linux siguen sin prueba local; la velocidad depende del hardware.
