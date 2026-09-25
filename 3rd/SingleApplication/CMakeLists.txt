cmake_minimum_required(VERSION 3.16)

project(SingleApplication LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Network REQUIRED)

add_library(${PROJECT_NAME} STATIC
    singleapplication.cpp
    singleapplication_p.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Network)

if(QAPPLICATION_CLASS STREQUAL QApplication)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)
elseif(QAPPLICATION_CLASS STREQUAL QGuiApplication)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Qt${QT_VERSION_MAJOR}::Gui)
else()
    set(QAPPLICATION_CLASS QCoreApplication)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Qt${QT_VERSION_MAJOR}::Core)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE advapi32)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC QAPPLICATION_CLASS=${QAPPLICATION_CLASS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
