cmake_minimum_required(VERSION 3.15)
project(vgm-player)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(vgm-player
    ymfm/src/ymfm_ssg.cpp
    ymfm/src/ymfm_opn.h
    ymfm/src/ymfm_opq.cpp
    ymfm/src/ymfm_opm.h
    ymfm/src/ymfm_fm.h
    ymfm/src/ymfm_misc.cpp
    ymfm/src/ymfm_opl.cpp
    ymfm/src/ymfm_opz.h
    ymfm/src/ymfm_pcm.cpp
    ymfm/src/ymfm_misc.h
    ymfm/src/ymfm_adpcm.cpp
    ymfm/src/ymfm_opl.h
    ymfm/src/ymfm_opm.cpp
    ymfm/src/ymfm_pcm.h
    ymfm/src/ymfm_adpcm.h
    ymfm/src/ymfm.h
    ymfm/src/ymfm_ssg.h
    ymfm/src/ymfm_opq.h
    ymfm/src/ymfm_opx.h
    ymfm/src/ymfm_opn.cpp
    ymfm/src/ymfm_opz.cpp
    ymfm/src/ymfm_fm.ipp

    src/main.cpp
    src/rf5c68.hpp
    src/ga20.hpp
)
target_include_directories(vgm-player PRIVATE
    ymfm/src
    ${SDL2_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)
target_link_libraries(vgm-player
    ${SDL2_LIBRARIES}
    ${ZLIB_LIBRARIES}
)

