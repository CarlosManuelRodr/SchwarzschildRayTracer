# SchwarzschildRayTracer
Ray tracer con desviación relativista de rayos de luz basado en la métrica de Schwarzschild.

El renderizador actual usa **OpenGL 4.3 compute shaders** para trazar los rayos,
integrar sus trayectorias y acumular muestras en la GPU. La ventana muestra un
render progresivo; C++ y SFML se encargan de los controles y la exportación PNG.

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
manifiesto `vcpkg.json` instala automáticamente SFML 2.6.2 y GLEW durante la
configuración.

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

## Uso

```powershell
.\build\Release\SchwarzschildRayTracer.exe
.\build\Release\SchwarzschildRayTracer.exe --width 800 --height 600 --samples 30 --seed 1
```

En Linux, usa `./build/SchwarzschildRayTracer` con las mismas opciones.

| Control | Acción |
| --- | --- |
| Flechas | Mover la cámara y su objetivo en X/Z |
| Q / E | Subir / bajar la cámara y su objetivo |
| W A S D | Mover el objetivo de la cámara en X/Z |
| P | Guardar las muestras disponibles como PNG en `Output/`, junto al ejecutable |
| Escape | Salir |

Cada cambio de cámara o tamaño reinicia la acumulación. La imagen se refina hasta
el objetivo de muestras (30 por defecto); no se guardan imágenes automáticamente.
La barra de título muestra GPU, promedio de muestras por píxel, tiempo del último
lote GPU y número de rayos inválidos. Los píxeles avanzan independientemente;
algunos pueden terminar antes que otros. Mientras se mueve la cámara se ve una
imagen incompleta que se refina al detenerse.
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
  Dielectric, DiffuseLight, Schwarzschild y texturas Constant, Checker e Image.
- `GpuRenderer` recibe una escena y `RenderSettings`, reinicia la cámara, despacha
  trabajo, consulta su finalización y presenta la acumulación con un triángulo.
  El propietario mantiene un contexto OpenGL activo hasta destruir el renderer.
  Se llama `poll()` entre despachos; solo hay un lote en vuelo. El framebuffer se
  descarga únicamente para exportación, validación o benchmark.
- Cada invocación realiza hasta 64 transiciones de un rayo antes de guardar su
  estado en un SSBO. Los rayos largos continúan en despachos posteriores. Las
  barreras de memoria y un fence coordinan cómputo, presentación y lectura.
- `assets/shaders/TraceCore.inl` contiene las ecuaciones compartidas entre GLSL
  (float) y la referencia CPU (double). El RNG es independiente por píxel/muestra
  y reproducible con una semilla fija. La CPU de referencia usa todos los núcleos,
  sin el generador aleatorio global compartido del código anterior.
- Se conserva **una región gravitatoria esférica finita** de radio 5.5 alrededor
  del agujero negro y un horizonte de radio 1. Fuera de la región, los rayos son
  rectos. Es una aproximación, no una integración de geodésicas en toda la escena.
- Dentro de la región, `r = posición - centro` y `h² = |r × velocidad|²` dan
  `aceleración = -1.5 h² r / |r|⁵`. La integración RK4 adaptativa compara un paso
  completo con dos medios pasos: tolerancia relativa `1e-4`, absoluta `1e-6`, paso
  máximo `0.05`. La velocidad no se normaliza entre pasos. Los cruces de horizonte,
  superficies y límite exterior se detectan sobre el segmento de cada paso
  aceptado; su localización converge al reducir el paso.
- Se admiten cámaras dentro de la región y objetos interceptados durante la
  integración. Se limitan las trayectorias a 50 interacciones y cada recorrido
  gravitatorio a 16,384 intentos. Agotar el presupuesto o producir valores no
  finitos termina el rayo negro y aumenta el diagnóstico; capturarlo por el
  horizonte es un resultado normal. Las órbitas casi críticas pueden necesitar
  más trabajo y ser sensibles a precisión/tolerancia.
- La radiancia emitida ya no se normaliza. Las imágenes sRGB se decodifican a luz
  lineal antes del muestreo, y se convierten de vuelta a sRGB para mostrar/guardar.
  Los valores fuera del rango de pantalla se recortan. Estas correcciones y las
  coordenadas relativas al agujero negro cambian la apariencia respecto a v1.0.

Los headers antiguos del trazador v1.0 permanecen como referencia histórica;
no forman parte del pipeline compilado. No se añadieron redshift, disco de
acreción, rotación del agujero negro ni nuevas características de escena.

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

Medición local Release, Windows, RTX 4070 Laptop GPU (8 de septiembre de 2026):

| Medida, 800×600, 30 muestras | Resultado |
| --- | ---: |
| Cómputo GPU | 0.351 s |
| GPU incluyendo lectura | 0.395 s |
| Referencia CPU, todos los núcleos, double | 35.33 s |
| Aceleración de finalización | 89.5× |
| Mayor lote GPU | 6.75 ms |
| RMSE RGB lineal | 0.000489 |
| Rayos inválidos CPU / GPU | 0 / 0 |

La presentación interactiva también depende de VSync y de la planificación del
sistema, por lo que estos tiempos no equivalen a FPS de la ventana. Se validó
el trazado en RTX 4070 Laptop e Intel Arc integrado en Windows. AMD y Linux
siguen sin prueba local; los resultados de velocidad dependen del hardware.
