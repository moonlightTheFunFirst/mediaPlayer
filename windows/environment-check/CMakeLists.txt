cmake_minimum_required(VERSION 3.21)
project(MediaPlayerEnvironmentCheck LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)
add_executable(mediaPlayerEnvironmentCheck main.cpp)
target_compile_features(mediaPlayerEnvironmentCheck PRIVATE cxx_std_17)
target_link_libraries(mediaPlayerEnvironmentCheck PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
