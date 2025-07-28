# 🐧 ComposeNativeTray - Linux Tray Backend

This repository contains a Linux-specific C/C++ backend for displaying a system tray icon using the StatusNotifierItem protocol. It is designed for use with the [ComposeNativeTray](https://github.com/kdroidFilter/ComposeNativeTray) library, providing tray integration for Linux applications built with Kotlin and JetBrains Compose.

## 🎯 Purpose

* ✅ Focused only on **Linux** desktop environments using the StatusNotifierItem protocol
* 🔄 Implements the DBus-based StatusNotifierItem specification for modern Linux desktops
* 🧩 Provides a C wrapper around the Qt/C++ implementation for easy JNI integration
* 🔗 JNI-friendly API for seamless integration with Kotlin/Compose

## ✅ Features

* Add a system tray icon with tooltip
* Support for primary and secondary click callbacks
* Support for scroll events
* Customizable context menu:
  * ✔️ Checkable items
  * 🚫 Disabled (grayed-out) items
  * ➕ Submenus
* Dynamic updates of the menu, icon, and tooltip at runtime
* Show notifications from the tray icon
* Compatible with various Linux desktop environments (GNOME, KDE, etc.)

## 🔧 C API

```c
/* System tray initialization and cleanup */
int  init_tray_system(void);
void shutdown_tray_system(void);

/* Tray creation and destruction */
void* create_tray(const char* id);
void  destroy_handle(void* handle);

/* Tray property setters */
void set_title(void* handle, const char* title);
void set_status(void* handle, const char* status);
void set_icon_by_name(void* handle, const char* name);
void set_icon_by_path(void* handle, const char* path);
void update_icon_by_path(void* handle, const char* path);
void set_tooltip_title(void* handle, const char* title);
void set_tooltip_subtitle(void* handle, const char* subTitle);

/* Menu creation and management */
void* create_menu(void);
void  destroy_menu(void* menu_handle);
void  set_context_menu(void* handle, void* menu);
void* add_menu_action(void* menu_handle, const char* text, ActionCallback cb, void* data);
void* add_disabled_menu_action(void* menu_handle, const char* text, ActionCallback cb, void* data);
void  add_checkable_menu_action(void* menu_handle, const char* text, int checked, ActionCallback cb, void* data);
void  add_menu_separator(void* menu_handle);
void* create_submenu(void* menu_handle, const char* text);
void  set_menu_item_text(void* menu_item_handle, const char* text);
void  set_menu_item_enabled(void* menu_item_handle, int enabled);
void  remove_menu_item(void* menu_handle, void* menu_item_handle);
void  clear_menu(void* menu_handle);

/* Tray event callbacks */
void set_activate_callback(void* handle, ActivateCallback cb, void* data);
void set_secondary_activate_callback(void* handle, SecondaryActivateCallback cb, void* data);
void set_scroll_callback(void* handle, ScrollCallback cb, void* data);

/* Notifications */
void show_notification(void* handle, const char* title, const char* msg, const char* iconName, int secs);

/* Event loop management */
int  sni_exec(void);
void sni_process_events(void);
void sni_stop_exec(void);

/* Force update of the tray (icon, tooltip, menu) */
void tray_update(void* handle);
```

## 🔨 Build Instructions

### Requirements

* CMake 3.10 or later
* Qt5 development packages (Core, Gui, Widgets, DBus)
* dbusmenu-qt5 development package
* C++17 compatible compiler

### Dependencies Installation

On Debian/Ubuntu:
```sh
sudo apt install cmake qtbase5-dev libdbusmenu-qt5-dev
```

On Fedora:
```sh
sudo dnf install cmake qt5-qtbase-devel libdbusmenu-qt5-devel
```

### Build

```sh
mkdir build
cd build
cmake ..
make
```

### Demo

Build and run the `tray-demo` or `tray-c-demo` binaries for working demonstrations.

## 📦 JNI Integration

This backend is compiled and linked with `ComposeNativeTray` and accessed from Kotlin using JNI. The library handles the complexities of the StatusNotifierItem protocol and Qt integration, providing a simple C API that can be called from Java/Kotlin.

## 🙏 Credits

This implementation is based on:

* The StatusNotifierItem specification from freedesktop.org
* Qt5's DBus integration
* dbusmenu-qt5 for menu handling

> This repository is a focused backend for Linux tray icons using the StatusNotifierItem protocol. Contributions related to Linux enhancements are welcome. Support for other platforms is out of scope.

## 📄 License

This library is licensed under the GNU Lesser General Public License (LGPL) version 2.1.