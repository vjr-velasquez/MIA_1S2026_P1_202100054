cmake_minimum_required(VERSION 3.16)
project(mia_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mia_backend
    src/main.cpp
    src/api/Server.cpp
    src/parser/CommandRunner.cpp

    src/disk/BinaryIO.cpp
    src/disk/MountManager.cpp

    src/commands/MkDisk.cpp
    src/commands/RmDisk.cpp
    src/commands/FdDisk.cpp
    src/commands/MountCmd.cpp
    src/commands/RepCmd.cpp
    src/commands/MkFsCmd.cpp
    src/commands/MkDirCmd.cpp
    src/commands/MkFileCmd.cpp
    src/commands/CatCmd.cpp
    src/commands/ChmodCmd.cpp
    src/commands/ChownCmd.cpp
    src/commands/UserSessionCmds.cpp
    src/commands/Project2Cmds.cpp
    src/commands/Ext3Cmds.cpp

    src/fs/Ext2IO.cpp
    src/fs/Ext2Formatter.cpp
    src/fs/Ext2Paths.cpp
    src/fs/FsFileOps.cpp
    src/fs/JournalManager.cpp

    src/session/SessionManager.cpp


)

target_include_directories(mia_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

find_package(Threads REQUIRED)
target_link_libraries(mia_backend PRIVATE Threads::Threads)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mia_backend PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
