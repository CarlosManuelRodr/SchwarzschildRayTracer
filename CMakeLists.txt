cmake_minimum_required(VERSION 3.21)

project(
    SchwarzschildRayTracer
    VERSION 1.0.0
    DESCRIPTION "A Schwarzschild black-hole ray tracer"
    LANGUAGES CXX
)

find_package(SFML 2.6 CONFIG REQUIRED COMPONENTS graphics window system)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_executable(SchwarzschildRayTracer
    src/Main.cpp
    src/SceneData.cpp
    src/CpuRenderer.cpp
    src/GpuRenderer.cpp
    src/Validation.cpp
)

target_compile_features(SchwarzschildRayTracer PRIVATE cxx_std_17)
target_link_libraries(SchwarzschildRayTracer
    PRIVATE
        sfml-graphics
        sfml-window
        sfml-system
        Threads::Threads
        OpenGL::GL
        GLEW::GLEW
)

if(MSVC)
    target_compile_definitions(SchwarzschildRayTracer PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(SchwarzschildRayTracer PRIVATE /W4 /permissive-)
else()
    target_compile_options(SchwarzschildRayTracer PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME cpu_reference COMMAND SchwarzschildRayTracer --test-cpu)
    add_test(NAME gpu_reference COMMAND SchwarzschildRayTracer --test-gpu)
    set_tests_properties(gpu_reference PROPERTIES LABELS "gpu" TIMEOUT 180)
endif()

add_custom_target(runtime_assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:SchwarzschildRayTracer>/assets"
    COMMENT "Copying runtime assets"
)
add_dependencies(SchwarzschildRayTracer runtime_assets)

install(TARGETS SchwarzschildRayTracer RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
