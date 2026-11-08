cmake_minimum_required(VERSION 3.16)
project(qdiscord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Network Core Widgets Quick)

add_executable(qdiscord
    main.cpp
    discordclient.cpp discordclient.h
    qdiscord.qrc
)

target_link_libraries(qdiscord PRIVATE Qt6::Network Qt6::Core Qt6::Quick)
