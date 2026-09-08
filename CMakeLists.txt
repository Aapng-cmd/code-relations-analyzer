cmake_minimum_required(VERSION 3.16)
project(code-relations-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(SOURCES
    src/main.cpp
    src/Theme.cpp
    src/UiConfig.cpp
    src/I18n.cpp
    src/AppConfig.cpp
    src/SettingsDialog.cpp
    src/PythonAnalyzer.cpp
    src/InfoPanel.cpp
    src/FileNodeItem.cpp
    src/RelationEdgeItem.cpp
    src/GraphView.cpp
    src/MainWindow.cpp
)

set(HEADERS
    src/Theme.h
    src/UiConfig.h
    src/ConfigPaths.h
    src/I18n.h
    src/AppConfig.h
    src/SettingsDialog.h
    src/Model.h
    src/PythonAnalyzer.h
    src/InfoPanel.h
    src/FileNodeItem.h
    src/RelationEdgeItem.h
    src/GraphView.h
    src/MainWindow.h
)

add_executable(code-relations-analyzer ${SOURCES} ${HEADERS})
target_include_directories(code-relations-analyzer PRIVATE src)
target_link_libraries(code-relations-analyzer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(code-relations-analyzer PRIVATE
    "TRANSLATIONS_DIR=\"${CMAKE_SOURCE_DIR}/config\"")

file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(code-relations-analyzer)
endif()
