cmake_minimum_required(VERSION 3.10)
project(StatusNotifierItemExample LANGUAGES CXX C)  # Added C for C language support

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)  # Enable automatic moc generation
set(CMAKE_AUTORCC ON)  # Enable automatic rcc generation
set(CMAKE_AUTOUIC ON)  # Enable automatic uic generation
set(CMAKE_INCLUDE_CURRENT_DIR ON)  # Include current directory for generated files

# Prefer the older libstdc++ string ABI for wider compatibility (e.g., Debian 12 Bookworm)
# This helps avoid requiring newer CXXABI/GLIBCXX symbols on target systems.
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

# Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)

# DBusMenu (for QMenu via DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUSMENU REQUIRED dbusmenu-qt5)

# Generates the D-Bus adapter from the XML interface
qt5_add_dbus_adaptor(
        statusnotifier_adaptor_src
        ${CMAKE_SOURCE_DIR}/org.kde.StatusNotifierItem.xml
        ${CMAKE_SOURCE_DIR}/include/statusnotifieritem.h
        StatusNotifierItem
        statusnotifieritemadaptor
)

# Include paths
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${DBUSMENU_INCLUDE_DIRS}
)

# Source files
set(SOURCES
        src/statusnotifieritem.cpp
        src/dbustypes.cpp
        src/sni_wrapper.cpp  # Addition of the wrapper
        src/qtthreadmanager.cpp  # Addition of the thread manager
        ${statusnotifier_adaptor_src}
)

set(HEADERS
        include/statusnotifieritem.h
        include/dbustypes.h
        include/sni_wrapper.h  # Addition of the header for the wrapper
        include/qtthreadmanager.h  # Addition of the header for the thread manager
)

# Ensure that the generated adapter file is recognized
set_source_files_properties(${statusnotifier_adaptor_src} PROPERTIES SKIP_AUTOMOC TRUE)

# Shared library for JNA
add_library(tray SHARED ${SOURCES} ${HEADERS})
target_link_libraries(tray
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
        ${DBUSMENU_LIBRARIES}
)

# On GCC/Clang, statically link libstdc++ and libgcc into the shared library to reduce
# runtime dependency on newer system C++ runtimes (improves Debian 12 compatibility).
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(tray PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Static library for the demo
add_library(statusnotifier STATIC ${SOURCES} ${HEADERS})
target_link_libraries(statusnotifier
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus
        ${DBUSMENU_LIBRARIES}
)

# Simple demo in C++
add_executable(tray-demo src/main.cpp)
target_link_libraries(tray-demo statusnotifier)

# Demo in C using the wrapper
add_executable(tray-c-demo src/exemple.c)
target_link_libraries(tray-c-demo tray)
