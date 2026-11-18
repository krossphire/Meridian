# Cross-compile Meridian for 64-bit Windows with mingw-w64.
# Host Qt (moc, rcc, uic) is passed separately as QT_HOST_PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Qt's Windows builds use the posix thread model. Prefer that compiler when
# both Ubuntu variants are installed; the win32 default will not link them.
if(EXISTS "/usr/bin/x86_64-w64-mingw32-g++-posix")
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
else()
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
endif()
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

if(NOT MINGW_QT_ROOT)
    set(MINGW_QT_ROOT "$ENV{HOME}/qt-win/6.8.3/mingw_64")
endif()

set(CMAKE_FIND_ROOT_PATH "${MINGW_QT_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
