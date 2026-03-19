cmake_minimum_required(VERSION 3.21)
project(VirtualTorrentFS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VTFS_ENABLE_WERROR "Treat warnings as errors" OFF)
option(VTFS_ENABLE_TESTS "Enable tests" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(vtfs_core
    src/ArgumentParser.cpp
    src/CacheManager.cpp
    src/Config.cpp
    src/LibtorrentSession.cpp
    src/Logger.cpp
    src/TorrentIndex.cpp
    src/VirtualDriveManifest.cpp
)

target_include_directories(vtfs_core PUBLIC include)
target_compile_definitions(vtfs_core PUBLIC UNICODE _UNICODE)

if(MSVC)
    target_compile_options(vtfs_core PRIVATE /W4 /permissive-)
    if(VTFS_ENABLE_WERROR)
        target_compile_options(vtfs_core PRIVATE /WX)
    endif()
else()
    target_compile_options(vtfs_core PRIVATE -Wall -Wextra -Wpedantic)
    if(VTFS_ENABLE_WERROR)
        target_compile_options(vtfs_core PRIVATE -Werror)
    endif()
endif()

add_executable(virtualtorrentfs
    src/main.cpp
    src/ServiceHost.cpp
    src/DokanFilesystem.cpp
)

target_link_libraries(virtualtorrentfs PRIVATE vtfs_core)

target_compile_definitions(virtualtorrentfs PRIVATE UNICODE _UNICODE)

if(WIN32)
    target_compile_definitions(virtualtorrentfs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

install(TARGETS virtualtorrentfs RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION .)
