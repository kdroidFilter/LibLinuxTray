// Update of src/exemple.c to use sni_exec() and add an item that changes name

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // Added for sleep function
#include "../include/sni_wrapper.h"  // Include with relative path to the include folder

void* global_tray = NULL;  // Global variable to access tray from callbacks
void* global_menu = NULL;  // New global to access menu from callbacks
void* change_name_item = NULL;  // Global for the item that changes name
void* add_item_button = NULL;   // Global for the item that adds an item
void* disappear_item = NULL;    // Global for the item that disappears
void* toggle_item = NULL;       // Global for the item that can be enabled/disabled

void on_activate(int x, int y, void* data) {
    printf("Tray activated at (%d, %d)\n", x, y);
}

void on_secondary_activate(int x, int y, void* data) {
    printf("Secondary activate at (%d, %d)\n", x, y);
}

void on_scroll(int delta, int orientation, void* data) {
    printf("Scroll: delta=%d, orientation=%d\n", delta, orientation);
}

void on_action1(void* data) {
    printf("Action 1 clicked!\n");
}

void on_action2(void* data) {
    printf("Action 2 clicked!\n");
}

void on_checkable_action(void* data) {
    printf("Checkable action toggled!\n");
}

void on_submenu_action(void* data) {
    printf("Submenu action clicked!\n");
}

void on_change_icon(void* data) {
    printf("Changing icon dynamically!\n");
    // Path to a new icon (replace with a valid path on your system)
    const char* new_icon_path = "/usr/share/icons/hicolor/48x48/apps/firefox.png";  // Example: Firefox icon
    update_icon_by_path(global_tray, new_icon_path);
}

void on_change_name(void* data) {
    printf("Changing item name!\n");
    set_menu_item_text(change_name_item, "New Name");
}

void on_add_item(void* data) {
    printf("Adding new item dynamically!\n");
    add_menu_action(global_menu, "New Added Item", NULL, NULL);
}

void on_disappear(void* data) {
    printf("Making item disappear!\n");
    remove_menu_item(global_menu, disappear_item);
    disappear_item = NULL;  // Reset pointer after deletion
}

void on_enable_item(void* data) {
    printf("Enabling item!\n");
    set_menu_item_enabled(toggle_item, 1);
}

void on_disable_item(void* data) {
    printf("Disabling item!\n");
    set_menu_item_enabled(toggle_item, 0);
}

void on_toggle_item(void* data) {
    printf("Toggle item clicked!\n");
}

int main() {
    init_tray_system();

    void* tray = create_tray("my_tray_example");
    if (!tray) {
        fprintf(stderr, "Failed to create tray\n");
        return 1;
    }
    global_tray = tray;  // Stocke le tray globalement pour l'accès dans les callbacks

    set_title(tray, "My Tray Example");
    set_status(tray, "Active");
    // Using an icon from a file path
    set_icon_by_path(tray, "/usr/share/icons/hicolor/48x48/apps/openjdk-17.png");
    set_tooltip_title(tray, "My App");
    set_tooltip_subtitle(tray, "Example Tooltip");

    set_activate_callback(tray, on_activate, NULL);
    set_secondary_activate_callback(tray, on_secondary_activate, NULL);
    set_scroll_callback(tray, on_scroll, NULL);

    void* menu = create_menu();
    if (!menu) {
        fprintf(stderr, "Failed to create menu\n");
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }
    global_menu = menu;  // Stocke le menu globalement

    // Ajout d'une action standard
    add_menu_action(menu, "Action 1", on_action1, NULL);

    // Ajout d'une action cochable
    add_checkable_menu_action(menu, "Toggle Me", 1, on_checkable_action, NULL);

    // Ajout d'un séparateur
    add_menu_separator(menu);

    // Création d'un sous-menu
    void* submenu = create_submenu(menu, "Submenu");
    if (!submenu) {
        fprintf(stderr, "Failed to create submenu\n");
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    // Ajout d'actions dans le sous-menu
    add_menu_action(submenu, "Submenu Action", on_submenu_action, NULL);
    add_menu_separator(submenu);
    add_menu_action(submenu, "Action 2", on_action2, NULL);

    // Ajout d'un nouvel item pour changer l'icône dynamiquement (dans le menu principal)
    add_menu_separator(menu);
    add_menu_action(menu, "Change Icon", on_change_icon, NULL);

    // New item that changes name when clicked
    add_menu_separator(menu);
    change_name_item = add_menu_action(menu, "Click me to change", on_change_name, NULL);
    if (!change_name_item) {
        fprintf(stderr, "Failed to create change name item\n");
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    // New item that adds an item when clicked
    add_menu_separator(menu);
    add_item_button = add_menu_action(menu, "Add an item", on_add_item, NULL);
    if (!add_item_button) {
        fprintf(stderr, "Failed to create add item button\n");
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    // New item that disappears when clicked
    add_menu_separator(menu);
    disappear_item = add_menu_action(menu, "Click me to disappear", on_disappear, NULL);
    if (!disappear_item) {
        fprintf(stderr, "Failed to create disappear item\n");
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    // New item that can be enabled/disabled (initially enabled)
    add_menu_separator(menu);
    toggle_item = add_menu_action(menu, "Toggle Item", on_toggle_item, NULL);
    if (!toggle_item) {
        fprintf(stderr, "Failed to create toggle item\n");
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    // Submenu to enable/disable the item
    add_menu_separator(menu);
    void* toggle_submenu = create_submenu(menu, "Toggle Item");
    if (!toggle_submenu) {
        fprintf(stderr, "Failed to create toggle submenu\n");
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    add_menu_action(toggle_submenu, "Enable", on_enable_item, NULL);
    add_menu_action(toggle_submenu, "Disable", on_disable_item, NULL);

    // Add a disabled item
    add_menu_separator(menu);
    void* disabled_item = add_disabled_menu_action(menu, "Item Disabled", NULL, NULL);
    if (!disabled_item) {
        fprintf(stderr, "Failed to create disabled item\n");
        destroy_handle(toggle_submenu);
        destroy_handle(submenu);
        destroy_handle(menu);
        destroy_handle(tray);
        shutdown_tray_system();
        return 1;
    }

    set_context_menu(tray, menu);

    // Display a notification
    show_notification(tray, "Hello", "This is a test notification", "dialog-information", 5000);

    // Main loop to handle events: use sni_exec() for a real Qt event loop
    printf("Tray is running. Press Ctrl+C to exit.\n");
    sni_exec();  // Blocking, handles events correctly

    destroy_handle(toggle_submenu); // Destruction du sous-menu toggle
    destroy_handle(submenu); // Destruction du sous-menu
    destroy_handle(menu);
    destroy_handle(tray);
    shutdown_tray_system();

    return 0;
}