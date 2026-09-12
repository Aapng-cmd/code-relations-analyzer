cmake_minimum_required(VERSION 3.16)
project(code-relations-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

add_library(cra-core SHARED
    src/AnalysisUtil.cpp
    src/UiConfig.cpp
)
target_include_directories(cra-core PUBLIC src)
target_link_libraries(cra-core PUBLIC Qt${QT_VERSION_MAJOR}::Core)
set_target_properties(cra-core PROPERTIES
    OUTPUT_NAME cra-core
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
    VISIBILITY_INLINES_HIDDEN ON
)
if(UNIX AND NOT APPLE)
    target_link_options(cra-core PRIVATE -Wl,-Bsymbolic)
endif()

function(add_language_plugin name)
    add_library(cra-${name} MODULE ${ARGN})
    target_link_libraries(cra-${name} PRIVATE cra-core)
    set_target_properties(cra-${name} PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "cra-${name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        BUILD_RPATH "$ORIGIN/.."
        INSTALL_RPATH "$ORIGIN/.."
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endfunction()

add_language_plugin(python src/PythonAnalyzer.cpp src/plugins/python_plugin.cpp)
add_language_plugin(cpp src/CppAnalyzer.cpp src/plugins/cpp_plugin.cpp)
add_language_plugin(java src/JavaAnalyzer.cpp src/plugins/java_plugin.cpp)
add_language_plugin(go src/GoAnalyzer.cpp src/plugins/go_plugin.cpp)
add_language_plugin(php src/PhpAnalyzer.cpp src/plugins/php_plugin.cpp)
add_language_plugin(html src/WebAnalyzer.cpp src/plugins/html_plugin.cpp)
add_language_plugin(css src/WebAnalyzer.cpp src/plugins/css_plugin.cpp)
add_language_plugin(js src/WebAnalyzer.cpp src/plugins/js_plugin.cpp)
add_language_plugin(web src/WebAnalyzer.cpp src/plugins/web_plugin.cpp)

set(SOURCES
    src/main.cpp
    src/Theme.cpp
    src/I18n.cpp
    src/AppConfig.cpp
    src/SettingsDialog.cpp
    src/GraphAnnotations.cpp
    src/PluginHost.cpp
    src/ProjectAnalyzer.cpp
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
    src/AnalysisUtil.h
    src/AnalyzerPlugin.h
    src/PluginHost.h
    src/GraphAnnotations.h
    src/ProjectAnalyzer.h
    src/InfoPanel.h
    src/FileNodeItem.h
    src/RelationEdgeItem.h
    src/GraphView.h
    src/MainWindow.h
)

add_executable(code-relations-analyzer ${SOURCES} ${HEADERS})
target_link_libraries(code-relations-analyzer PRIVATE cra-core Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(code-relations-analyzer PRIVATE
    "TRANSLATIONS_DIR=\"${CMAKE_SOURCE_DIR}/config\"")
set_target_properties(code-relations-analyzer PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

add_dependencies(code-relations-analyzer
    cra-python cra-cpp cra-java cra-go cra-php cra-html cra-css cra-js cra-web)

file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(code-relations-analyzer)
endif()
