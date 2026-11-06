cmake_minimum_required(VERSION 3.16)
project(rg808 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(rg808
    src/main.c
    src/engine.c
    src/canvas.c
    src/samples.c
    src/wav.c
    src/pattern.c
    src/song.c
    src/fx.c
)

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

# ship the curated kit next to the executable for local dev
add_custom_command(TARGET rg808 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:rg808>/assets)
