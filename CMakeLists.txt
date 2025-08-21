cmake_minimum_required(VERSION 3.10)
project(StatusNotifierItemExample LANGUAGES CXX C)

# ---- Basics -----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---- Qt5 --------------------------------------------------------------------
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)

# ---- PkgConfig and dependencies ---------------------------------------------
find_package(PkgConfig REQUIRED)

# DBusMenu (Qt5 bindings for exporting QMenu over DBus)
pkg_check_modules(DBUSMENU REQUIRED dbusmenu-qt5)

# GLib / GObject / GThread (for glib.h, log handlers, etc.)
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gthread-2.0)

# ---- D-Bus adaptor generation -----------------------------------------------
qt5_add_dbus_adaptor(
    statusnotifier_adaptor_src
    ${CMAKE_SOURCE_DIR}/org.kde.StatusNotifierItem.xml
    ${CMAKE_SOURCE_DIR}/include/statusnotifieritem.h
    StatusNotifierItem
    statusnotifieritemadaptor
)
# The adaptor is generated; AUTOMOC must not reprocess it.
set_source_files_properties(${statusnotifier_adaptor_src} PROPERTIES SKIP_AUTOMOC TRUE)

# ---- Sources / Headers -------------------------------------------------------
set(SOURCES
    src/statusnotifieritem.cpp
    src/dbustypes.cpp
    src/sni_wrapper.cpp
    src/qtthreadmanager.cpp
    ${statusnotifier_adaptor_src}
)

set(HEADERS
    include/statusnotifieritem.h
    include/dbustypes.h
    include/sni_wrapper.h
    include/qtthreadmanager.h
)

# ---- Shared library for JNA -------------------------------------------------
add_library(tray SHARED ${SOURCES} ${HEADERS})
target_include_directories(tray
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${DBUSMENU_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)
target_compile_options(tray
    PRIVATE
        ${DBUSMENU_CFLAGS_OTHER}
        ${GLIB_CFLAGS_OTHER}
)
target_link_libraries(tray
    PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
        ${DBUSMENU_LIBRARIES}
        ${GLIB_LIBRARIES}
)

# ---- Static library for demos ----------------------------------------------
# Make usage requirements PUBLIC so executables inherit include paths and flags.
add_library(statusnotifier STATIC ${SOURCES} ${HEADERS})
target_include_directories(statusnotifier
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${DBUSMENU_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)
target_compile_options(statusnotifier
    PUBLIC
        ${DBUSMENU_CFLAGS_OTHER}
        ${GLIB_CFLAGS_OTHER}
)
target_link_libraries(statusnotifier
    PUBLIC
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
        ${DBUSMENU_LIBRARIES}
        ${GLIB_LIBRARIES}
)

# ---- Executables -------------------------------------------------------------
# Simple C++ demo
add_executable(tray-demo src/main.cpp)
target_link_libraries(tray-demo
    PRIVATE
        statusnotifier
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
)

# C demo using the wrapper
add_executable(tray-c-demo src/exemple.c)
target_include_directories(tray-c-demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(tray-c-demo
    PRIVATE
        tray
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
)

# ---- Notes ------------------------------------------------------------------
# System packages (Debian/Ubuntu):
#   sudo apt-get install -y libdbusmenu-qt5-dev libglib2.0-dev
# Optionally: libgirepository1.0-dev if you need GObject Introspection.
