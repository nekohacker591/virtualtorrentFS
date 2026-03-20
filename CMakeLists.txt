cmake_minimum_required(VERSION 3.24)
project(VirtualTorrentFS VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(virtualtorrentfs
  src/main.cpp
  src/App.cpp
  src/Config.cpp
  src/CacheManager.cpp
  src/TorrentSession.cpp
  src/TorrentMetadata.cpp
  src/Bencode.cpp
  src/Sha1.cpp
  src/PeerProtocol.cpp
  src/TrackerClient.cpp
  src/PeerConnection.cpp
  src/PortMapper.cpp
  src/DhtClient.cpp
  src/TorrentEngine.cpp
  src/VirtualDrive.cpp
  src/WebDavServer.cpp
  src/WebSeedClient.cpp
)

target_include_directories(virtualtorrentfs PRIVATE include)

if (MSVC)
  target_compile_options(virtualtorrentfs PRIVATE /W4 /permissive-)
else()
  target_compile_options(virtualtorrentfs PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS virtualtorrentfs RUNTIME DESTINATION bin)

if (WIN32)
  target_link_libraries(virtualtorrentfs PRIVATE projectedfslib ws2_32 mpr)
endif()
