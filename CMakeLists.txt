cmake_minimum_required(VERSION 3.16)
project(Meridian VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(MERIDIAN_ALPHA_READONLY "Disable EEPROM and calibration writes" ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network SerialPort Svg)
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

add_library(meridian-core STATIC
    src/core/Gridsquare.cpp
    src/core/Bearing.cpp
    src/core/Compass.cpp
    src/core/Projection.cpp
    src/core/ZipCodes.cpp
    src/core/Coordinates.cpp
    src/core/Solar.cpp
    src/map/GeoMap.cpp
    src/map/DayNight.cpp
    src/rotor/Rt21Protocol.cpp
    src/rotor/AppConfig.cpp
    src/rotor/Rt21Driver.cpp
    src/rotor/TrafficLog.cpp
    src/rotor/Rt21Finder.cpp
    src/rotor/UsbHint.cpp
    src/rotor/Rt21Setup.cpp
    src/rotor/UserFiles.cpp
    src/sources/QrzXml.cpp
    src/sources/CredentialStore.cpp
    src/sources/QrzClient.cpp
    src/sources/WsjtStatus.cpp
    src/sources/WsjtSource.cpp
    src/sources/N1mmXml.cpp
    src/sources/N1mmSource.cpp
    src/sources/DxKeeperSource.cpp
)
target_include_directories(meridian-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# One version string: the CMake project version. Window title, QApplication,
# and the traffic-log header all read this.
target_compile_definitions(meridian-core PUBLIC MERIDIAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(meridian-core PUBLIC Qt6::Widgets Qt6::Network Qt6::SerialPort)
if(WIN32)
    target_link_libraries(meridian-core PUBLIC setupapi cfgmgr32 winmm advapi32)
endif()
if(UNIX AND NOT APPLE)
    target_include_directories(meridian-core PUBLIC ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(meridian-core PUBLIC ${LIBSECRET_LIBRARIES})
    target_compile_definitions(meridian-core PUBLIC MERIDIAN_HAVE_LIBSECRET=1)
endif()
if(APPLE)
    target_link_libraries(meridian-core PUBLIC "-framework Security")
endif()
if(WIN32)
    # Private header so a close does not SetCommState the saved DCB back
    # (that re-raises DTR/RTS). Qt6_DIR is .../lib/cmake/Qt6.
    target_include_directories(meridian-core PRIVATE
        "${Qt6_DIR}/../../../include"
        "${Qt6_DIR}/../../../include/QtCore/${Qt6_VERSION}"
        "${Qt6_DIR}/../../../include/QtCore/${Qt6_VERSION}/QtCore"
        "${Qt6_DIR}/../../../include/QtSerialPort/${Qt6_VERSION}"
        "${Qt6_DIR}/../../../include/QtSerialPort/${Qt6_VERSION}/QtSerialPort")
endif()
if(APPLE)
    target_link_libraries(meridian-core PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()
if(MERIDIAN_ALPHA_READONLY)
    target_compile_definitions(meridian-core PUBLIC MERIDIAN_ALPHA_READONLY=1)
endif()

add_library(meridian-mocklib STATIC
    src/mock/Rt21Simulator.cpp
)
target_include_directories(meridian-mocklib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(meridian-mocklib PUBLIC meridian-core Qt6::Network)
if(UNIX AND NOT APPLE)
    target_link_libraries(meridian-mocklib PUBLIC util)
endif()

add_executable(meridian
    src/main.cpp
    src/AppIcon.cpp
    src/ScreenshotRun.cpp
    src/GlobeWidget.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/ControllerColumn.cpp
    src/SetupPanel.cpp
    src/AboutDialog.cpp
    resources.qrc
)
target_link_libraries(meridian PRIVATE meridian-core meridian-mocklib Qt6::Svg)
if(WIN32)
    # Desktop app: no extra console window on Windows.
    target_link_options(meridian PRIVATE -mwindows)
    # Explorer and the taskbar read the exe's icon resource. The .ico is a
    # raster of paintMeridianMark(); the path is absolute so windres does not
    # look for it in the build directory. Forward slashes: backslash is an
    # escape in an .rc file.
    enable_language(RC)
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/resources/meridian.ico" MERIDIAN_ICO)
    configure_file(resources/meridian.rc.in "${CMAKE_BINARY_DIR}/meridian.rc" @ONLY)
    set_source_files_properties("${CMAKE_BINARY_DIR}/meridian.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/meridian.ico")
    target_sources(meridian PRIVATE "${CMAKE_BINARY_DIR}/meridian.rc")
endif()
if(APPLE)
    set_target_properties(meridian PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME Meridian
        MACOSX_BUNDLE_BUNDLE_NAME "Meridian"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.k5po.meridian"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_STRING "RT-21 rotator controller"
    )
endif()

add_executable(meridian-mock src/mock/main.cpp)
target_link_libraries(meridian-mock PRIVATE meridian-mocklib)
if(WIN32)
    target_sources(meridian-mock PRIVATE "${CMAKE_BINARY_DIR}/meridian.rc")
endif()

if(NOT CMAKE_CROSSCOMPILING)
enable_testing()
function(meridian_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE meridian-core meridian-mocklib Qt6::Test)
    target_compile_definitions(${name} PRIVATE MERIDIAN_DATA_DIR="${CMAKE_SOURCE_DIR}/data" MERIDIAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()
meridian_test(tst_core)
meridian_test(tst_sources)
meridian_test(tst_finder)
meridian_test(tst_usb)
meridian_test(tst_driver)
meridian_test(tst_globe)
target_sources(tst_globe PRIVATE src/GlobeWidget.cpp)
target_link_libraries(tst_globe PRIVATE Qt6::Svg)
set_tests_properties(tst_globe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
meridian_test(tst_settings)
target_sources(tst_settings PRIVATE
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/ControllerColumn.cpp
    src/SetupPanel.cpp
    src/AboutDialog.cpp
    src/GlobeWidget.cpp)
meridian_test(tst_setup)
target_sources(tst_setup PRIVATE src/SetupPanel.cpp)
target_link_libraries(tst_settings PRIVATE Qt6::Svg)
set_tests_properties(tst_usb tst_settings tst_setup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS meridian meridian-mock
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
