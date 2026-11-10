cmake_minimum_required(VERSION 3.16)
project(qdiscord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Network Core Widgets Quick Multimedia)

set(VOICE_DEP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/voice)

add_executable(qdiscord
    main.cpp
    discordclient.cpp discordclient.h
    voiceclient.cpp voiceclient.h
    voiceaudio.cpp voiceaudio.h
    qdiscord.qrc
)

target_include_directories(qdiscord PRIVATE ${VOICE_DEP_DIR})
target_link_libraries(qdiscord PRIVATE Qt6::Network Qt6::Core Qt6::Quick Qt6::Multimedia)

file(GLOB QT_DLLS C:/Qt/6.12.0/mingw_64/bin/Qt6*.dll)
foreach(dll ${QT_DLLS})
    get_filename_component(dll_name ${dll} NAME)
    add_custom_command(TARGET qdiscord POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll} $<TARGET_FILE_DIR:qdiscord>/${dll_name})
endforeach()
add_custom_command(TARGET qdiscord POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            C:/Qt/6.12.0/mingw_64/plugins/platforms $<TARGET_FILE_DIR:qdiscord>/platforms
    COMMENT "Copying Qt platform plugins")
add_custom_command(TARGET qdiscord POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            C:/Qt/6.12.0/mingw_64/qml $<TARGET_FILE_DIR:qdiscord>/qml
    COMMENT "Copying Qt QML plugins")
add_custom_command(TARGET qdiscord POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            C:/Qt/6.12.0/mingw_64/plugins/tls $<TARGET_FILE_DIR:qdiscord>/tls
    COMMENT "Copying Qt TLS backend plugins")
add_custom_command(TARGET qdiscord POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            C:/Qt/6.12.0/mingw_64/plugins/multimedia $<TARGET_FILE_DIR:qdiscord>/multimedia
    COMMENT "Copying Qt multimedia plugins")
foreach(dll libssl-1_1-x64.dll libcrypto-1_1-x64.dll)
    add_custom_command(TARGET qdiscord POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/Qt/Tools/mingw1310_64/opt/bin/${dll}" $<TARGET_FILE_DIR:qdiscord>/${dll})
endforeach()
foreach(dll libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll)
    add_custom_command(TARGET qdiscord POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                C:/Qt/Tools/mingw1310_64/bin/${dll} $<TARGET_FILE_DIR:qdiscord>/${dll})
endforeach()
foreach(dll libdave.dll libsodium.dll opus.dll)
    add_custom_command(TARGET qdiscord POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VOICE_DEP_DIR}/${dll} $<TARGET_FILE_DIR:qdiscord>/${dll})
endforeach()
