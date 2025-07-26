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

static std::atomic<bool> sni_running{true};
static bool debug = false;  // Set to false to reduce debug output
static int trayCount = 0;

// -----------------------------------------------------------------------------
// Custom message handler to filter warnings
// -----------------------------------------------------------------------------
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // Filter out known harmless warnings
    if (msg.contains("QObject::killTimer: Timers cannot be stopped from another thread") ||
        msg.contains("QObject::~QObject: Timers cannot be stopped from another thread") ||
        msg.contains("g_main_context_pop_thread_default") ||
        msg.contains("QtDBus: cannot relay signals") ||
        msg.contains("QApplication was not created in the main() thread") ||
        msg.contains("QWidget: Cannot create a QWidget without QApplication") ||
        msg.contains("QSocketNotifier: Can only be used with threads started with QThread") ||
        msg.contains("QObject::startTimer: Timers can only be used with threads started with QThread") ||
        msg.contains("QMetaObject::invokeMethod: Dead lock detected") ||
        msg.contains("QObject::connect: Cannot queue arguments")) {
        return;
    }

    const QByteArray localMsg = msg.toLocal8Bit();
    const char *file = context.file ? context.file : "unknown";
    const char *function = context.function ? context.function : "unknown";

    switch (type) {
    case QtDebugMsg:
        if (debug) fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        if (!msg.contains("QWidget: Cannot create a QWidget without QApplication")) {
            abort();
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// Helper: get safe connection type to avoid deadlocks
// -----------------------------------------------------------------------------
static inline Qt::ConnectionType safeConn(QObject* receiver) {
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
SNIWrapperManager* SNIWrapperManager::s_instance = nullptr;

SNIWrapperManager* SNIWrapperManager::instance() {
    static QMutex mutex;

    if (!s_instance) {
        QMutexLocker locker(&mutex);
        if (!s_instance) {
            // Use QtThreadManager to create SNIWrapperManager in the Qt thread
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
    qInstallMessageHandler(customMessageHandler);

    // Suppress some debug messages
    setenv("G_MESSAGES_DEBUG", "", 1);
    setenv("G_DEBUG", "", 1);

    // Ensure DBus session bus is initialized in this thread
    QDBusConnection::sessionBus();
}

void SNIWrapperManager::startEventLoop() {
    // No-op; event loop is managed by QtThreadManager
}

StatusNotifierItem* SNIWrapperManager::createSNI(const char* id) {
    return new StatusNotifierItem(QString::fromUtf8(id), this);
}

void SNIWrapperManager::destroySNI(StatusNotifierItem* sni) {
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
    try {
        SNIWrapperManager::instance();  // Ensures creation in Qt thread
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to initialize tray system: %s\n", e.what());
        return -1;
    }
}

void shutdown_tray_system(void) {
    SNIWrapperManager::shutdown();
    QtThreadManager::shutdown();
}

void* create_tray(const char* id) {
    if (!id) return nullptr;

    trayCount++;
    StatusNotifierItem* result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        result = mgr->createSNI(id);
    }, safeConn(mgr));

    return result;
}

void destroy_handle(void* handle) {
    if (!handle) return;

    auto mgr = SNIWrapperManager::instance();
    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);

    QMetaObject::invokeMethod(mgr, [mgr, sni]() {
        mgr->destroySNI(sni);
    }, safeConn(mgr));

    trayCount--;
    if (trayCount <= 0) {
        // Delay shutdown to ensure all cleanup is done
        QTimer::singleShot(100, []() {
            shutdown_tray_system();
        });
    }
}

// ------------------- Tray property setters -------------------

void set_title(void* handle, const char* title) {
    if (!handle || !title) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qtitle = QString::fromUtf8(title);

    QMetaObject::invokeMethod(sni, [sni, qtitle]() {
        sni->setTitle(qtitle);
    }, safeConn(sni));
}

void set_status(void* handle, const char* status) {
    if (!handle || !status) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qstatus = QString::fromUtf8(status);

    QMetaObject::invokeMethod(sni, [sni, qstatus]() {
        sni->setStatus(qstatus);
    }, safeConn(sni));
}

void set_icon_by_name(void* handle, const char* name) {
    if (!handle || !name) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qname = QString::fromUtf8(name);

    QMetaObject::invokeMethod(sni, [sni, qname]() {
        sni->setIconByName(qname);
    }, safeConn(sni));
}

void set_icon_by_path(void* handle, const char* path) {
    if (!handle || !path) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qpath = QString::fromUtf8(path);

    QMetaObject::invokeMethod(sni, [sni, qpath]() {
        // Force icon refresh by clearing cache first
        sni->setIconByName(QString());
        sni->setIconByPixmap(QIcon(qpath));
    }, safeConn(sni));
}

void update_icon_by_path(void* handle, const char* path) {
    // Force complete icon update
    set_icon_by_path(handle, path);
}

void set_tooltip_title(void* handle, const char* title) {
    if (!handle || !title) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qtitle = QString::fromUtf8(title);

    QMetaObject::invokeMethod(sni, [sni, qtitle]() {
        sni->setToolTipTitle(qtitle);
    }, safeConn(sni));
}

void set_tooltip_subtitle(void* handle, const char* subTitle) {
    if (!handle || !subTitle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qsubtitle = QString::fromUtf8(subTitle);

    QMetaObject::invokeMethod(sni, [sni, qsubtitle]() {
        sni->setToolTipSubTitle(qsubtitle);
    }, safeConn(sni));
}

// ------------------- Menu creation & management -------------------

void* create_menu(void) {
    QMenu* result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        result = new QMenu();
        result->setObjectName("SNIContextMenu");
    }, safeConn(mgr));

    return result;
}

void destroy_menu(void* menu_handle) {
    if (!menu_handle) return;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        // Disconnect all signals first
        menu->disconnect();

        // Clear all actions
        menu->clear();

        // Mark for deletion (will be deleted when event loop processes it)
        menu->deleteLater();

        // Process events to ensure deleteLater is handled
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));
}

void set_context_menu(void* handle, void* menu_handle) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QMenu* menu = menu_handle ? static_cast<QMenu*>(menu_handle) : nullptr;

    QMetaObject::invokeMethod(sni, [sni, menu]() {
        // Set the new context menu (StatusNotifierItem handles cleanup internally)
        sni->setContextMenu(menu);

        // Force processing of events to ensure DBus changes are applied
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

        // Additional delay when unsetting menu to ensure cleanup completes
        if (!menu) {
            QTimer timer;
            timer.setSingleShot(true);
            QEventLoop loop;
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(50);
            loop.exec();
        }
    }, safeConn(sni));
}

void* add_menu_action(void* menu_handle, const char* text, ActionCallback cb, void* data) {
    if (!menu_handle || !text) return nullptr;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QAction* result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        QAction* action = menu->addAction(qtext);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
        result = action;
    }, safeConn(mgr));

    return result;
}

void* add_disabled_menu_action(void* menu_handle, const char* text, ActionCallback cb, void* data) {
    if (!menu_handle || !text) return nullptr;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QAction* result = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        QAction* action = menu->addAction(qtext);
        action->setEnabled(false);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
        result = action;
    }, safeConn(mgr));

    return result;
}

void add_checkable_menu_action(void* menu_handle, const char* text, int checked, ActionCallback cb, void* data) {
    if (!menu_handle || !text) return;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [=]() {
        QAction* action = menu->addAction(qtext);
        action->setCheckable(true);
        action->setChecked(checked != 0);
        if (cb) {
            QObject::connect(action, &QAction::triggered, [cb, data]() {
                if (cb) cb(data);
            });
        }
    }, safeConn(mgr));
}

void add_menu_separator(void* menu_handle) {
    if (!menu_handle) return;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        menu->addSeparator();
    }, safeConn(mgr));
}

void* create_submenu(void* menu_handle, const char* text) {
    if (!menu_handle || !text) return nullptr;

    QMenu* parentMenu = static_cast<QMenu*>(menu_handle);
    QString qtext = QString::fromUtf8(text);
    QMenu* subMenu = nullptr;
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [&]() {
        subMenu = parentMenu->addMenu(qtext);
        subMenu->setObjectName("SNISubMenu");
    }, safeConn(mgr));

    return subMenu;
}

void set_menu_item_text(void* menu_item_handle, const char* text) {
    if (!menu_item_handle || !text) return;

    QAction* action = static_cast<QAction*>(menu_item_handle);
    QString qtext = QString::fromUtf8(text);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [action, qtext]() {
        action->setText(qtext);
    }, safeConn(mgr));
}

void set_menu_item_enabled(void* menu_item_handle, int enabled) {
    if (!menu_item_handle) return;

    QAction* action = static_cast<QAction*>(menu_item_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [action, enabled]() {
        action->setEnabled(enabled != 0);
    }, safeConn(mgr));
}

void remove_menu_item(void* menu_handle, void* menu_item_handle) {
    if (!menu_handle || !menu_item_handle) return;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    QAction* action = static_cast<QAction*>(menu_item_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu, action]() {
        menu->removeAction(action);
        action->deleteLater();
    }, safeConn(mgr));
}

// ------------------- Tray update function -------------------

void tray_update(void* handle) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [sni]() {
        // Store current values
        QString currentIcon = sni->iconName();
        QString currentTitle = sni->title();
        QString currentTooltipTitle = sni->toolTipTitle();
        QString currentStatus = sni->status();

        // Force re-emission by toggling values
        if (!currentIcon.isEmpty()) {
            sni->setIconByName("");
            sni->setIconByName(currentIcon);
        }

        // Force title update
        sni->setTitle(currentTitle + " ");
        sni->setTitle(currentTitle);

        // Force tooltip update
        sni->setToolTipTitle(currentTooltipTitle + " ");
        sni->setToolTipTitle(currentTooltipTitle);

        // Force status update
        sni->setStatus("NeedsAttention");
        sni->setStatus(currentStatus);

        // Process events to ensure all changes are applied
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));
}

// ------------------- Tray event callbacks -------------------

void set_activate_callback(void* handle, ActivateCallback cb, void* data) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        // Disconnect previous connections
        QObject::disconnect(sni, &StatusNotifierItem::activateRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::activateRequested, sni,
                           [cb, data](const QPoint& pos) {
                               if (cb) cb(pos.x(), pos.y(), data);
                           },
                           Qt::DirectConnection);
        }
    }, safeConn(sni));
}

void set_secondary_activate_callback(void* handle, SecondaryActivateCallback cb, void* data) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        // Disconnect previous connections
        QObject::disconnect(sni, &StatusNotifierItem::secondaryActivateRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::secondaryActivateRequested, sni,
                           [cb, data](const QPoint& pos) {
                               if (cb) cb(pos.x(), pos.y(), data);
                           },
                           Qt::DirectConnection);
        }
    }, safeConn(sni));
}

void set_scroll_callback(void* handle, ScrollCallback cb, void* data) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);

    QMetaObject::invokeMethod(sni, [sni, cb, data]() {
        // Disconnect previous connections
        QObject::disconnect(sni, &StatusNotifierItem::scrollRequested, nullptr, nullptr);

        if (cb) {
            QObject::connect(sni, &StatusNotifierItem::scrollRequested, sni,
                           [cb, data](int delta, Qt::Orientation orientation) {
                               if (cb) cb(delta, orientation == Qt::Horizontal ? 1 : 0, data);
                           },
                           Qt::DirectConnection);
        }
    }, safeConn(sni));
}

// ------------------- Notifications -------------------

void show_notification(void* handle, const char* title, const char* msg, const char* iconName, int secs) {
    if (!handle) return;

    StatusNotifierItem* sni = static_cast<StatusNotifierItem*>(handle);
    QString qtitle = title ? QString::fromUtf8(title) : QString();
    QString qmsg = msg ? QString::fromUtf8(msg) : QString();
    QString qiconName = iconName ? QString::fromUtf8(iconName) : QString();

    QMetaObject::invokeMethod(sni, [sni, qtitle, qmsg, qiconName, secs]() {
        sni->showMessage(qtitle, qmsg, qiconName, secs * 1000);
    }, safeConn(sni));
}

// ------------------- Event loop management -------------------

int sni_exec(void) {
    while (sni_running.load()) {
        try {
            sni_process_events();
            usleep(100000); // 100ms
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception in sni_exec: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "Unknown exception in sni_exec\n");
        }
    }
    sni_running.store(true); // Reset for potential reuse
    return 0;
}

void sni_stop_exec(void) {
    sni_running.store(false);
}

void sni_process_events(void) {
    QtThreadManager::instance()->runBlocking([] {
        auto mgr = SNIWrapperManager::instance();
        if (mgr) {
            mgr->processEvents();
        }
    });
}

void clear_menu(void* menu_handle) {
    if (!menu_handle) return;

    QMenu* menu = static_cast<QMenu*>(menu_handle);
    auto mgr = SNIWrapperManager::instance();

    QMetaObject::invokeMethod(mgr, [menu]() {
        // Disconnect all signals from actions
        for (QAction* action : menu->actions()) {
            action->disconnect();
        }
        // Clear all actions
        menu->clear();
        // Process events to ensure cleanup
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }, safeConn(mgr));
}