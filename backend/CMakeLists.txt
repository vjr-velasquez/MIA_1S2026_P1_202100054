cmake_minimum_required(VERSION 3.16)
project(mia_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mia_backend
    src/main.cpp
    src/api/Server.cpp
    src/parser/CommandRunner.cpp

    src/disk/BinaryIO.cpp
    src/commands/MkDisk.cpp
    src/commands/RmDisk.cpp
    src/commands/FdDisk.cpp
    src/disk/MountManager.cpp
    src/commands/MountCmd.cpp

)

target_include_directories(mia_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

find_package(Threads REQUIRED)
target_link_libraries(mia_backend PRIVATE Threads::Threads)
