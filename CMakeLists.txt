cmake_minimum_required(VERSION 3.21)

project(
    SchwarzschildRayTracer
    VERSION 1.0.0
    DESCRIPTION "A Schwarzschild black-hole ray tracer"
    LANGUAGES CXX
)

find_package(SDL3 3.4.16 EXACT CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_executable(SchwarzschildRayTracer
    src/Main.cpp
    src/Animation.cpp
    src/Timeline.cpp
    src/FrameExport.cpp
    src/DesktopFrameSink.cpp
    src/AnimationTests.cpp
    src/SettingsPanel.cpp
    src/BodyEditor.cpp
    src/SceneData.cpp
    src/StbImage.cpp
    src/Blackbody.cpp
    src/CpuRenderer.cpp
    src/GpuRenderer.cpp
    src/Validation.cpp
)

target_compile_features(SchwarzschildRayTracer PRIVATE cxx_std_17)
target_link_libraries(SchwarzschildRayTracer
    PRIVATE
        SDL3::SDL3
        imgui::imgui
        Threads::Threads
        OpenGL::GL
        GLEW::GLEW
)

if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET SchwarzschildRayTracer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:SchwarzschildRayTracer>"
        COMMENT "Copying SDL runtime"
    )
    install(FILES "$<TARGET_FILE:SDL3::SDL3-shared>" DESTINATION .)
endif()

if(WIN32)
    target_link_libraries(SchwarzschildRayTracer PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()

if(MSVC)
    target_compile_definitions(SchwarzschildRayTracer PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(SchwarzschildRayTracer PRIVATE /W4 /permissive-)
else()
    set_source_files_properties(src/StbImage.cpp PROPERTIES COMPILE_OPTIONS -w)
    target_compile_options(SchwarzschildRayTracer PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME animation COMMAND SchwarzschildRayTracer --test-animation)
    add_test(NAME export_pipeline COMMAND SchwarzschildRayTracer --test-export)
    add_test(NAME export_gpu COMMAND SchwarzschildRayTracer --test-export-gpu)
    set_tests_properties(export_gpu PROPERTIES LABELS "gpu" TIMEOUT 180)
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
