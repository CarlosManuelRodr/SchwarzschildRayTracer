# SchwarzschildRayTracer
Ray tracer con desviación relativista de rayos de luz basado en la métrica de Schwarzschild.

![ExampleImage](https://raw.githubusercontent.com/CarlosManuelRodr/SchwarzschildRayTracer/master/Images/Animation.gif)

## Descargar
Se puede descargar una versión compilada para la plataforma Windows del programa a través del siguiente enlace:

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
manifiesto `vcpkg.json` instala automáticamente SFML 2.6.2 durante la
configuración.

Requisitos:

- CMake 3.21 o posterior
- Un compilador compatible con C++11
- vcpkg, con la variable de entorno `VCPKG_ROOT` apuntando a su directorio

```sh
cmake --preset default
cmake --build --preset default
```

El ejecutable se genera en `build/` (o en `build/Release/` con generadores
multiconfiguración). CMake copia las texturas necesarias junto al ejecutable.

Sin presets, se puede configurar explícitamente el toolchain:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
