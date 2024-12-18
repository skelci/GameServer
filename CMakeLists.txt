cmake_minimum_required(VERSION 3.20)

project(GameServer)

include_directories("src")
include_directories("src/client")
include_directories("src/server")

if(WIN32)
    set(BOOST_LIB_SUFFIX "-mgw13-mt-x64-1_85.a")

    include_directories("C:\\lib\\include")

    link_directories(C:\\lib\\bin)
    link_directories(C:\\lib\\github_repos\\PDCurses-3.9\\wincon)
    link_directories(C:\\lib\\github_repos\\libpqxx-master\\build\\libpqxx\\lib)

    find_library(BOOST_CONTAINER
        NAMES libboost_container${BOOST_LIB_SUFFIX}
        PATHS C:\\lib\\github_repos\\boost_1_85_0\\stage\\lib
    )
    find_library(BOOST_SYSTEM
        NAMES libboost_system${BOOST_LIB_SUFFIX}
        PATHS C:\\lib\\github_repos\\boost_1_85_0\\stage\\lib
    )
    find_library(BOOST_THREAD
        NAMES libboost_thread${BOOST_LIB_SUFFIX}
        PATHS C:\\lib\\github_repos\\boost_1_85_0\\stage\\lib
    )
endif()


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY bin)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY lib)

option(BUILD_SERVER "Build the server executable" OFF)
option(BUILD_SERVER_AUTH "Build the server authentication executable" OFF)
option(BUILD_SERVER_WORLD "Build the server world executable" OFF)
option(BUILD_SERVER_CHAT "Build the server chat executable" OFF)
option(BUILD_SERVER_VOICE "Build the server voice executable" OFF)
option(BUILD_SERVER_CONTROL "Build the server control executable" OFF)
option(BUILD_SERVER_SHARED "Build the server shared library" OFF)

option(BUILD_COMMON "Build the common library" OFF)
option(BUILD_CLIENT "Build the client executable" OFF)
option(DEBUG "Build with debug flags" OFF)
option(RELEASE "Build with release flags" OFF)


if(BUILD_SERVER)
    set(BUILD_SERVER_AUTH ON)
    set(BUILD_SERVER_WORLD ON)
    set(BUILD_SERVER_CHAT ON)
    set(BUILD_SERVER_VOICE ON)
    set(BUILD_SERVER_CONTROL ON)
    set(BUILD_SERVER_SHARED ON)
endif()

if(DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG -g -O0") 
endif()

if(RELEASE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

if(BUILD_COMMON)
    file(GLOB commonsrc src/common/*.cpp)
    add_library(common SHARED ${commonsrc})

    if(WIN32)
        target_link_libraries(common PUBLIC
            crypto-1_1-x64__
            ssl-1_1-x64__
            ws2_32
        )
    else()
        target_link_libraries(common PUBLIC
            crypto
            ssl
        )
    endif()
endif()

if(BUILD_CLIENT)    
    file(GLOB clientsrc src/client/network/*.cpp src/common/*.cpp)
    add_executable(client ${clientsrc} src/client/main.cpp)
    link_directories(${CMAKE_SOURCE_DIR}/lib)
    include_directories(${CMAKE_SOURCE_DIR}/src)
    
    if(WIN32)
        target_link_libraries(client PUBLIC
            ${BOOST_SYSTEM}
            ${BOOST_THREAD}
            crypto-1_1-x64__
            ssl-1_1-x64__
            ws2_32
            common
        )
    else()
        target_link_libraries(client PUBLIC
            boost_system
            boost_thread
            jsoncpp
            crypto
            common
            ssl
        )
    endif()
endif()

if(BUILD_SERVER_SHARED)
    file(GLOB serversharedsrc src/server/shared/*.cpp src/common/*.cpp)
    add_library(servershared SHARED ${serversharedsrc})

    if(WIN32)
        target_link_libraries(servershared PUBLIC
            jsoncpp
            ws2_32
            common
        )
    else()
        target_link_libraries(servershared PUBLIC
            jsoncpp
            common
        )
    endif()
endif()

if(BUILD_SERVER_CONTROL)
    file(GLOB servercontrolsrc src/server/control/*.cpp)
    add_executable(servercontrol ${servercontrolsrc} src/server/control/main.cpp)

    if(WIN32)
        target_link_libraries(servercontrol PUBLIC
            -l:pdcurses.a
            servershared
            libpq__
            common
            pqxx
        )
    else()
        target_link_libraries(servercontrol PUBLIC
            servershared
            ncurses
            common
            pqxx
            SDL2
        )
    endif()
endif()

if(BUILD_SERVER_AUTH)
    file(GLOB serverauthsrc src/server/auth/*.cpp)
    add_executable(serverauth ${serverauthsrc} src/server/auth/main.cpp)

    if(WIN32)
        target_link_libraries(serverauth PUBLIC
            ${BOOST_SYSTEM}
            ${BOOST_THREAD}
            crypto-1_1-x64__
            ssl-1_1-x64__
            servershared
            mswsock
            libpq__
            common
            pqxx
        )
    else()
        target_link_libraries(serverauth PUBLIC
            servershared
            boost_system
            boost_thread
            crypto
            common
            pqxx
            ssl
        )
    endif()
endif()

if(BUILD_SERVER_WORLD)
    file(GLOB serverworldsrc src/server/world/*.cpp)
    add_executable(serverworld ${serverworldsrc} src/server/world/main.cpp)

    if(WIN32)
        target_link_libraries(serverworld PUBLIC
            ${BOOST_SYSTEM}
            ${BOOST_THREAD}
            servershared
            mswsock
            common
        )
    else()
        target_link_libraries(serverworld PUBLIC
            servershared
            boost_system
            boost_thread
            common
        )
    endif()
endif()

if(BUILD_SERVER_CHAT)
    file(GLOB serverchatsrc src/server/chat/*.cpp)
    add_executable(serverchat ${serverchatsrc} src/server/chat/main.cpp)

    if(WIN32)
        target_link_libraries(serverchat PUBLIC
            servershared
        )
    else()
        target_link_libraries(serverchat PUBLIC
            servershared
        )
    endif()
endif()

if(BUILD_SERVER_VOICE)
    file(GLOB servervoicesrc src/server/voice/*.cpp)
    add_executable(servervoice ${servervoicesrc} src/server/voice/main.cpp)

    if(WIN32)
        target_link_libraries(servervoice PUBLIC
            servershared
        )
    else()
        target_link_libraries(servervoice PUBLIC
            servershared
        )
    endif()
endif()
