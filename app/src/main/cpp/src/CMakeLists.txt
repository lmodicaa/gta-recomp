cmake_minimum_required(VERSION 3.12)
project(reGTA)

set(CMAKE_CXX_STANDARD 20)

if(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    add_definitions(-DVERSION_x32=true)
elseif(${ANDROID_ABI} STREQUAL "arm64-v8a")
    add_definitions(-DVERSION_x32=false)
endif()

# Add source files
file(GLOB_RECURSE SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp

        # Header Files
        #${CMAKE_CURRENT_LIST_DIR}/Code/*.cpp
        #${CMAKE_CURRENT_LIST_DIR}/Code/**/*.cpp
        #${CMAKE_CURRENT_LIST_DIR}/Code/**/**/*.cpp
        #${CMAKE_CURRENT_LIST_DIR}/Code/**/**/**/*.cpp
        #${CMAKE_CURRENT_LIST_DIR}/Code/**/**/**/*/*.cpp

        # vendor - notsa
        ${CMAKE_CURRENT_LIST_DIR}/vendor/javawrapper/JavaWrapper.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vendor/log/log.cpp

        # MTA Android Module
        ${CMAKE_CURRENT_LIST_DIR}/mta/core/MtaClient.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/core/MtaHooks.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/game/MtaGameInterface.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/multiplayer/MtaPlayerManager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/net/MtaNetwork.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/net/NetBitStream.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/ui/MtaJNI.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/ui/MtaUI.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mta/server/MtaLocalServer.cpp
)

# Add include directories
include_directories(
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/vendor/
        ${CMAKE_CURRENT_LIST_DIR}/mta/
)

add_library(reGTA SHARED ${SOURCES})
target_link_libraries(reGTA
        log
        GLESv3
        shadowhook::shadowhook
)

# Set compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -w -fstack-protector -Wall -O3 -fexceptions")

