cmake_minimum_required(VERSION 3.16)
project(rg808 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Pick the machine definition at configure time. The shared core is identical;
# only the machine file, sample provider and app shell differ.
set(RG_MACHINE "808" CACHE STRING "Machine: 808 or smp")
set_property(CACHE RG_MACHINE PROPERTY STRINGS 808 smp)

set(CORE_SOURCES
    src/core/engine.c
    src/core/canvas.c
    src/core/wav.c
    src/core/pattern.c
    src/core/song.c
    src/core/fx.c
    src/core/perf_fx.c
    src/core/step_ui.c
)

if (RG_MACHINE STREQUAL "808")
    set(MACHINE_DIR "${CMAKE_SOURCE_DIR}/src/machines/808")
    set(MACHINE_SOURCES
        src/machines/808/samples.c
        src/machines/808/machine.c
        src/machines/808/main.c
    )
elseif (RG_MACHINE STREQUAL "smp")
    set(MACHINE_DIR "${CMAKE_SOURCE_DIR}/src/machines/smp")
    set(MACHINE_SOURCES
        src/machines/smp/samplebank.c
        src/machines/smp/machine.c
        src/machines/smp/main.c
    )
else()
    message(FATAL_ERROR "RG_MACHINE must be 808 or smp")
endif()

add_executable(rg808 ${CORE_SOURCES} ${MACHINE_SOURCES})
target_include_directories(rg808 PRIVATE src/core ${MACHINE_DIR})

if (WIN32)
    # MinGW dev libs vendored under vendor/sdl2 (see tools/fetch-deps.ps1)
    set(SDL2_ROOT "${CMAKE_SOURCE_DIR}/vendor/sdl2/SDL2-2.32.10/x86_64-w64-mingw32")
    target_include_directories(rg808 PRIVATE "${SDL2_ROOT}/include/SDL2")
    target_link_libraries(rg808 mingw32 "${SDL2_ROOT}/lib/libSDL2main.a" "${SDL2_ROOT}/lib/libSDL2.dll.a")
    add_custom_command(TARGET rg808 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_ROOT}/bin/SDL2.dll" $<TARGET_FILE_DIR:rg808>)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(rg808 PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(rg808 ${SDL2_LIBRARIES} m)
endif()

# ship the local sample tree next to the executable for dev
add_custom_command(TARGET rg808 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:rg808>/assets)
