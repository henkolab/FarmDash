cmake_minimum_required(VERSION 3.19)
project(FarmRun LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL OpenGLWidgets Multimedia)

add_executable(FarmRun
    src/main.cpp
    src/MainWindow.cpp        src/MainWindow.h
    src/MenuWidget.cpp        src/MenuWidget.h
    src/GameWidget.cpp        src/GameWidget.h
    src/GameWorld.cpp         src/GameWorld.h
    src/Entities.cpp          src/Entities.h
    src/Pathfinding.cpp       src/Pathfinding.h
    src/NetworkManager.cpp    src/NetworkManager.h
    src/Protocol.cpp          src/Protocol.h
    src/Leaderboard.cpp       src/Leaderboard.h
    src/Audio.cpp             src/Audio.h
    src/Biomes.h
    src/Biomes.cpp
    src/Minimap.h
    src/Minimap.cpp
    src/NaturalEffects.h
    src/NaturalEffects.cpp
)

target_include_directories(FarmRun PRIVATE src)
target_link_libraries(FarmRun PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia)
