cmake_minimum_required(VERSION 3.21)

project(
    SchwarzschildRayTracer
    VERSION 1.0.0
    DESCRIPTION "A Schwarzschild black-hole ray tracer"
    LANGUAGES CXX
)

find_package(SFML 2.6 CONFIG REQUIRED COMPONENTS graphics window system)
find_package(Threads REQUIRED)

add_executable(SchwarzschildRayTracer
    src/Main.cpp
    src/RandomGen.cpp
)

target_compile_features(SchwarzschildRayTracer PRIVATE cxx_std_11)
target_link_libraries(SchwarzschildRayTracer
    PRIVATE
        sfml-graphics
        sfml-window
        sfml-system
        Threads::Threads
)

if(MSVC)
    target_compile_definitions(SchwarzschildRayTracer PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(SchwarzschildRayTracer PRIVATE /W4 /permissive-)
else()
    target_compile_options(SchwarzschildRayTracer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(SchwarzschildRayTracer PRIVATE Shlwapi)
endif()

add_custom_command(
    TARGET SchwarzschildRayTracer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:SchwarzschildRayTracer>/assets"
    COMMENT "Copying runtime assets"
)

install(TARGETS SchwarzschildRayTracer RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
