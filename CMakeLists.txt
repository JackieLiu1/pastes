cmake_minimum_required(VERSION 3.16)

project(pastes VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Match OrcaTerminal: application files live in /opt, integration in /usr.
    set(CMAKE_INSTALL_PREFIX "/" CACHE INTERNAL "Linux package root" FORCE)
endif()
include(GNUInstallDirs)

set(CMAKE_PREFIX_QT_PATH "" CACHE PATH "Qt installation prefix")
if(CMAKE_PREFIX_QT_PATH)
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_PREFIX_QT_PATH}")
endif()

# Network is used privately by the bundled SingleApplication; declaring it
# here satisfies Qt6's dependency walk at target finalization
find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Sql Network LinguistTools)

# used by the bundled SingleApplication build
set(QAPPLICATION_CLASS QApplication)
add_subdirectory(3rd/SingleApplication)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# translations: Pastes_zh_CN.ts -> Pastes_zh_CN.qm
add_custom_target(update_translations
    COMMAND Qt6::lupdate -I "${CMAKE_CURRENT_SOURCE_DIR}" -extensions cpp,h,mm
        app core application storage sync ui platform -ts Pastes_zh_CN.ts
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
qt_add_translation(QM_FILES Pastes_zh_CN.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})

# Each module owns its sources and dependencies; app assembles the adapters.
add_subdirectory(core)
add_subdirectory(application)
add_subdirectory(storage)
add_subdirectory(sync)
add_subdirectory(platform)
add_subdirectory(ui)
add_subdirectory(app)

if(WIN32)
    add_subdirectory(packaging/windows)
endif()
if(APPLE)
    add_subdirectory(packaging/macos)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS pastes DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/pastes/translations)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(packaging/linux)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(PASTES_BUILD_BENCHMARKS "Build isolated history performance benchmarks" OFF)
if(PASTES_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
