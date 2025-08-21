// File: sni_wrapper.cpp

#include "sni_wrapper.h"
#include "statusnotifieritem.h"
#include "dbustypes.h"
#include "qtthreadmanager.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QDBusConnection>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QPoint>
#include <QMutex>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstdarg>

#include <glib.h>

// --- GLib warning silencer ----------------------------------------------------
// We silence GLib/GObject/GDK warnings to avoid noisy console output when
// Qt (Widgets / GTK platformtheme) is used from a non-main thread.
// We keep this off in debug mode so developers still see logs.
static bool g_glibSilencerInstalled = false;

static void glib_log_handler(const gchar *log_domain,
                             GLogLevelFlags log_level,
                             const gchar *message,
                             gpointer user_data) {
    // Drop all messages.
    (void)log_domain; (void)log_level; (void)message; (void)user_data;
}

static void install_glib_warning_silencer(bool enable) {
    if (!enable || g_glibSilencerInstalled) return;
    g_glibSilencerInstalled = true;
    const GLogLevelFlags mask = (GLogLevelFlags)(G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION);
    g_log_set_handler("GLib", mask, glib_log_handler, nullptr);
    g_log_set_handler("GLib-GObject", mask, glib_log_handler, nullptr);
    g_log_set_handler("Gdk", mask, glib_log_handler, nullptr);
    g_log_set_handler("Gtk", mask, glib_log_handler, nullptr);
}


static std::atomic<bool> sni_running{true};

// Global shutdown guard to avoid double teardown ordering issues between Qt/GLib.
static std::atomic<bool> g_shuttingDown{false};

static bool debug_mode = false;
static int trayCount = 0;

// -----------------------------------------------------------------------------
// Function to enable/disable debug mode
// -----------------------------------------------------------------------------
extern "C" void sni_set_debug_mode(int enabled) {
    debug_mode = enabled != 0;
}

// -----------------------------------------------------------------------------
// Centralized logger
// -----------------------------------------------------------------------------
static void sni_log(const char* format, ...) {
    if (!debug_mode) return;

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

// -----------------------------------------------------------------------------
// Minimal message handler for Qt
// -----------------------------------------------------------------------------
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (!debug_mode) return;

    const QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", localMsg.constData());
}

// -----------------------------------------------------------------------------
// Helper: get safe connection type to avoid deadlocks
// -----------------------------------------------------------------------------
static inline Qt::ConnectionType safeConn(QObject *receiver) {
    if (!receiver) return Qt::QueuedConnection;

    if (QThread::currentThread() == receiver->thread()) {
        return Qt::DirectConnection;
    } else {
        return Qt::BlockingQueuedConnection;
    }
}

// -----------------------------------------------------------------------------
// SNIWrapperManager implementation
// -----------------------------------------------------------------------------
SNIWrapperManager *SNIWrapperManager::s_instance = nullptr;

SNIWrapperManager *SNIWrapperManager::instance() {
    static QMutex mutex;

    if (!s_instance) {
        QMutexLocker locker(&mutex);
        if (!s_instance) {
            QtThreadManager::instance()->runBlocking([] {
                s_instance = new SNIWrapperManager();
            });
        }
    }
    return s_instance;
}

void SNIWrapperManager::shutdown() {
    if (s_instance) {
        QtThreadManager::instance()->runBlocking([] {
            delete s_instance;
            s_instance = nullptr;
        });
    }
}

SNIWrapperManager::~SNIWrapperManager() {
    // Cleanup handled by QtThreadManager
}

SNIWrapperManager::SNIWrapperManager() : QObject(), app(qApp) {
    // If not in debug mode, suppress only Qt outputs
    if (!debug_mode) {
        // Redirect all Qt outputs to nowhere
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});

        // DO NOT redirect stderr
    } else {
        // In debug mode, install a simple handler
        qInstallMessageHandler(customMessageHandler);
    }

    // Ensure DBus session bus is initialized in this thread
    QDBusConnection::sessionBus();
}

void SNIWrapperManager::startEventLoop() {
    // No-op; event loop is managed by QtThreadManager
}

StatusNotifierItem *SNIWrapperManager::createSNI(const char *id) {
    return new StatusNotifierItem(QString::fromUtf8(id), this);
}

void SNIWrapperManager::destroySNI(StatusNotifierItem *sni) {
    if (!sni) return;
    sni->unregister();
    sni->deleteLater();
}

void SNIWrapperManager::processEvents() {
    if (app) {
        app->processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 100);
    }
}

// -----------------------------------------------------------------------------
// C API Implementation
// -----------------------------------------------------------------------------

int init_tray_system(void) {
    // Set environment variables BEFORE any Qt object creation
    static std::once_flag env_flag;
    std::call_once(env_flag, []() {
            // Force Qt to avoid GLib event dispatcher to prevent GLib context assertion issues
            // when running QApplication in a non-main thread.
            setenv("QT_NO_GLIB", "1", 1);
            // Keep Qt away from GTK/GDK to avoid GLib/GDK type re-registration
            setenv("QT_STYLE_OVERRIDE", "Fusion", 1);
            setenv("QT_QPA_PLATFORMTHEME", "qt5ct", 1);
        if (!debug_mode) {
            // Silence GLib/GObject warnings in release mode
            install_glib_warning_silencer(true);
            setenv("QT_LOGGING_RULES", "*=false", 1);
            setenv("QT_FATAL_WARNINGS", "0", 1);
        }
    });

    try {
        SNIWrapperManager::instance();
        sni_log("Tray system initialized successfully");
        return 0;
    } catch (const std::exception &e) {
        sni_log("Failed to initialize tray system: %s", e.what());
        return -1;
    }
}
void shutdown_tray_system(void) {
    // Prevent double shutdown
    bool expected=false; if (!g_shuttingDown.compare_exchange_strong(expected,true)) return;
    sni_log("Shutting down tray system");
    SNIWrapperManager::shutdown();
    QtThreadManager::shutdown();
}

void *create_tray(const char *id) {
    if (!id) return nullptr;

    trayCount++;
    StatusNotifierItem *result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        result = mgr->createSNI(id);
    }, safeConn(mgr));

    sni_log("Created tray with id: %s", id);
    return result;
}

void destroy_handle(void *handle) {
    if (!handle) return;

    auto mgr = SNIWrapperManager::instance();
    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);

    QMetaObject::invokeMethod(mgr, [mgr, sni]() {
        mgr->destroySNI(sni);
    }, safeConn(mgr));

    trayCount--;
    sni_log("Destroyed tray handle, remaining: %d", trayCount);

    if (trayCount <= 0) {
        QTimer::singleShot(100, []() {
            shutdown_tray_system();
        });
    }
}

// ------------------- Tray property setters -------------------

void set_title(void *handle, const char *title) {
    if (!handle || !title) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qtitle = QString::fromUtf8(title);

    QMetaObject::invokeMethod(sni, [sni, qtitle]() {
        sni->setTitle(qtitle);
    }, safeConn(sni));

    sni_log("Set title: %s", title);
}

void set_status(void *handle, const char *status) {
    if (!handle || !status) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qstatus = QString::fromUtf8(status);

    QMetaObject::invokeMethod(sni, [sni, qstatus]() {
        sni->setStatus(qstatus);
    }, safeConn(sni));

    sni_log("Set status: %s", status);
}

void set_icon_by_name(void *handle, const char *name) {
    if (!handle || !name) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qname = QString::fromUtf8(name);

    QMetaObject::invokeMethod(sni, [sni, qname]() {
        sni->setIconByName(qname);
    }, safeConn(sni));

    sni_log("Set icon by name: %s", name);
}

void set_icon_by_path(void *handle, const char *path) {
    if (!handle || !path) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qpath = QString::fromUtf8(path);

    QMetaObject::invokeMethod(sni, [sni, qpath]() {
        sni->setIconByName(QString());
        sni->setIconByPixmap(QIcon(qpath));
    }, safeConn(sni));

    sni_log("Set icon by path: %s", path);
}

void update_icon_by_path(void *handle, const char *path) {
    set_icon_by_path(handle, path);
}

void set_tooltip_title(void *handle, const char *title) {
    if (!handle || !title) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qtitle = QString::fromUtf8(title);

    QMetaObject::invokeMethod(sni, [sni, qtitle]() {
        sni->setToolTipTitle(qtitle);
    }, safeConn(sni));

    sni_log("Set tooltip title: %s", title);
}

void set_tooltip_subtitle(void *handle, const char *subTitle) {
    if (!handle || !subTitle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qsubtitle = QString::fromUtf8(subTitle);

    QMetaObject::invokeMethod(sni, [sni, qsubtitle]() {
        sni->setToolTipSubTitle(qsubtitle);
    }, safeConn(sni));

    sni_log("Set tooltip subtitle: %s", subTitle);
}

// ------------------- Menu creation & management -------------------

void *create_menu(void) {
    QMenu *result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        result = new QMenu();
        result->setObjectName("SNIContextMenu");
    }, safeConn(mgr));

    sni_log("Created menu");
    return result;
}

void destroy_menu(void *menu_handle) {
    if (!menu_handle) return;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        menu->disconnect();
        menu->clear();
        menu->deleteLater();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));

    sni_log("Destroyed menu");
}

void set_context_menu(void *handle, void *menu_handle) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QMenu *menu = menu_handle ? static_cast<QMenu *>(menu_handle) : nullptr;

    QMetaObject::invokeMethod(sni, [sni, menu]() {
        sni->setContextMenu(menu);
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

        if (!menu) {
            QTimer timer;
            timer.setSingleShot(true);
            QEventLoop loop;
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(50);
            loop.exec();
        }
    }, safeConn(sni));

    sni_log("Set context menu");
}

void *add_menu_action(void *menu_handle, const char *text, ActionCallback cb, void *data) {
    if (!menu_handle || !text) return nullptr;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QAction *result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        QAction *action = menu->addAction(qtext);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
        result = action;
    }, safeConn(mgr));

    sni_log("Added menu action: %s", text);
    return result;
}

void *add_disabled_menu_action(void *menu_handle, const char *text, ActionCallback cb, void *data) {
    if (!menu_handle || !text) return nullptr;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QAction *result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        QAction *action = menu->addAction(qtext);
        action->setEnabled(false);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
        result = action;
    }, safeConn(mgr));

    sni_log("Added disabled menu action: %s", text);
    return result;
}

void* add_checkable_menu_action(void *menu_handle, const char *text, int checked, ActionCallback cb, void *data) {
    if (!menu_handle || !text) return nullptr;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QAction *result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        QAction *action = menu->addAction(qtext);
        action->setCheckable(true);
        action->setChecked(checked != 0);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
        result = action;
    }, safeConn(mgr));

    sni_log("Added checkable menu action: %s (checked: %d)", text, checked);
    return result;
}

void add_menu_separator(void *menu_handle) {
    if (!menu_handle) return;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        menu->addSeparator();
    }, safeConn(mgr));

    sni_log("Added menu separator");
}
// Une structure pour stocker la relation submenu -> action
static QMap<QMenu*, QAction*> submenuToAction;

void* create_submenu(void *menu_handle, const char *text) {
    if (!menu_handle || !text) return nullptr;

    QMenu *parentMenu = static_cast<QMenu *>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QMenu *subMenu = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        subMenu = parentMenu->addMenu(qtext);
        subMenu->setObjectName("SNISubMenu");

        // Trouver l'action associée et la stocker
        for (QAction* action : parentMenu->actions()) {
            if (action->menu() == subMenu) {
                submenuToAction[subMenu] = action;
                break;
            }
        }
    }, safeConn(mgr));

    sni_log("Created submenu: %s", text);
    return subMenu;
}

void set_submenu_icon(void* submenu_handle, const char* icon_path_or_name) {
    if (!submenu_handle || !icon_path_or_name) return;

    QMenu *submenu = static_cast<QMenu *>(submenu_handle);
    QString qstr = QString::fromUtf8(icon_path_or_name);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [submenu, qstr]() {
        QAction* action = submenuToAction.value(submenu, nullptr);
        if (action) {
            QIcon ico = QIcon::fromTheme(qstr);
            if (ico.isNull())
                ico = QIcon(qstr);
            action->setIcon(ico);
        }
    }, safeConn(mgr));

    sni_log("Set submenu icon: %s", icon_path_or_name);
}

void set_menu_item_text(void *menu_item_handle, const char *text) {
    if (!menu_item_handle || !text) return;

    QAction *action = static_cast<QAction *>(menu_item_handle);
    QString qtext = QString::fromUtf8(text);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [action, qtext]() {
        action->setText(qtext);
    }, safeConn(mgr));

    sni_log("Set menu item text: %s", text);
}

void set_menu_item_icon(void *menu_item_handle,
                        const char *icon_path_or_name)
{
    if (!menu_item_handle || !icon_path_or_name)
        return;

    QAction *action = static_cast<QAction *>(menu_item_handle);
    QString qstr = QString::fromUtf8(icon_path_or_name);
    auto mgr = SNIWrapperManager::instance();      // même schéma que les autres setters

    QMetaObject::invokeMethod(mgr, [action, qstr]()
    {
        /* 1) Essaye d’abord le thème d’icônes */
        QIcon ico = QIcon::fromTheme(qstr);

        /* 2) Puis, si échec, interprète la chaîne comme chemin absolu */
        if (ico.isNull())
            ico = QIcon(qstr);

        action->setIcon(ico);
    }, safeConn(mgr));

    sni_log("Set menu item icon: %s", icon_path_or_name);
}


void set_menu_item_enabled(void *menu_item_handle, int enabled) {
    if (!menu_item_handle) return;

    QAction *action = static_cast<QAction *>(menu_item_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [action, enabled]() {
        action->setEnabled(enabled != 0);
    }, safeConn(mgr));

    sni_log("Set menu item enabled: %d", enabled);
}

int set_menu_item_checked(void *menu_item_handle, int checked) {
    if (!menu_item_handle) return -1;

    QAction *action = static_cast<QAction *>(menu_item_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [action, checked]() {
        if (action->isCheckable()) {
            action->setChecked(checked != 0);
        }
    }, safeConn(mgr));

    sni_log("Set menu item checked: %d", checked);
    return 0;
}

void remove_menu_item(void *menu_handle, void *menu_item_handle) {
    if (!menu_handle || !menu_item_handle) return;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    QAction *action = static_cast<QAction *>(menu_item_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu, action]() {
        menu->removeAction(action);
        action->deleteLater();
    }, safeConn(mgr));

    sni_log("Removed menu item");
}

// ------------------- Tray update function -------------------

void tray_update(void *handle) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [sni]() {
        QString currentIcon = sni->iconName();
        QString currentTitle = sni->title();
        QString currentTooltipTitle = sni->toolTipTitle();
        QString currentStatus = sni->status();

        if (!currentIcon.isEmpty()) {
            sni->setIconByName("");
            sni->setIconByName(currentIcon);
        }

        sni->setTitle(currentTitle + " ");
        sni->setTitle(currentTitle);

        sni->setToolTipTitle(currentTooltipTitle + " ");
        sni->setToolTipTitle(currentTooltipTitle);

        sni->setStatus("NeedsAttention");
        sni->setStatus(currentStatus);

        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));

    sni_log("Updated tray");
}

// ------------------- Tray event callbacks -------------------

void set_activate_callback(void *handle, ActivateCallback cb, void *data) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        QObject::disconnect(sni, &StatusNotifierItem::activateRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::activateRequested, sni,
                             [cb, data](const QPoint &pos) {
                                 if (cb) cb(pos.x(), pos.y(), data);
                             },
                             Qt::DirectConnection);
        }
    }, safeConn(sni));

    sni_log("Set activate callback");
}

void set_secondary_activate_callback(void *handle, SecondaryActivateCallback cb, void *data) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        QObject::disconnect(sni, &StatusNotifierItem::secondaryActivateRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::secondaryActivateRequested, sni,
                             [cb, data](const QPoint &pos) {
                                 if (cb) cb(pos.x(), pos.y(), data);
                             },
                             Qt::DirectConnection);
        }
    }, safeConn(sni));

    sni_log("Set secondary activate callback");
}

void set_scroll_callback(void *handle, ScrollCallback cb, void *data) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        QObject::disconnect(sni, &StatusNotifierItem::scrollRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::scrollRequested, sni,
                             [cb, data](int delta, Qt::Orientation orientation) {
                                 if (cb) cb(delta, orientation == Qt::Horizontal ? 1 : 0, data);
                             },
                             Qt::DirectConnection);
        }
    }, safeConn(sni));

    sni_log("Set scroll callback");
}

// ------------------- Notifications -------------------

void show_notification(void *handle, const char *title, const char *msg, const char *iconName, int secs) {
    if (!handle) return;

    StatusNotifierItem *sni = static_cast<StatusNotifierItem *>(handle);
    QString qtitle = title ? QString::fromUtf8(title) : QString();
    QString qmsg = msg ? QString::fromUtf8(msg) : QString();
    QString qiconName = iconName ? QString::fromUtf8(iconName) : QString();

    QMetaObject::invokeMethod(sni, [sni, qtitle, qmsg, qiconName, secs]() {
        sni->showMessage(qtitle, qmsg, qiconName, secs * 1000);
    }, safeConn(sni));

    sni_log("Showed notification: %s", title ? title : "");
}

// ------------------- Event loop management -------------------

int sni_exec(void) {
    while (sni_running.load()) {
        try {
            sni_process_events();
            usleep(100000); // 100ms
        } catch (const std::exception &e) {
            sni_log("Exception in sni_exec: %s", e.what());
        } catch (...) {
            sni_log("Unknown exception in sni_exec");
        }
    }
    sni_running.store(true);
    return 0;
}

void sni_stop_exec(void) {
    sni_running.store(false);
    sni_log("Stopped event loop");
}

void sni_process_events(void) {
    QtThreadManager::instance()->runBlocking([] {
        auto mgr = SNIWrapperManager::instance();
        if (mgr) {
            mgr->processEvents();
        }
    });
}

void clear_menu(void *menu_handle) {
    if (!menu_handle) return;

    QMenu *menu = static_cast<QMenu *>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        for (QAction *action: menu->actions()) {
            action->disconnect();
        }
        menu->clear();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));

    sni_log("Cleared menu");
}