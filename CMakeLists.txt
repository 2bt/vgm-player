cmake_minimum_required(VERSION 3.13)
project(vgm-player)

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(vgm-player
    src/main.cpp
)
target_include_directories(vgm-player PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)
target_link_libraries(vgm-player
    ${SDL2_LIBRARIES}
    ${ZLIB_LIBRARIES}
)
