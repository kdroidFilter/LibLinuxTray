#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QDebug>
#include "statusnotifieritem.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    StatusNotifierItem trayIcon("example");
    trayIcon.setTitle("Tray Example");

    /* ---------- Icons ---------- */
    const QString iconPath1 = "/home/elie-gambache/Images/avatar.png";            // Default icon
    const QString iconPath2 = "/usr/share/icons/hicolor/48x48/apps/firefox.png";  // Alternative icon (example)
    static bool useAltIcon = false;  // Allows switching between the two

    QIcon icon(iconPath1);

    // Force a render to fill availableSizes()
    QPixmap dummy = icon.pixmap(QSize(24, 24));
    if (icon.isNull() || dummy.isNull())
        qWarning() << "Failed to load icon" << iconPath1;

    trayIcon.setIconByPixmap(icon);

    /* ---------- ToolTip ---------- */
    trayIcon.setToolTipTitle("My App");
    trayIcon.setToolTipSubTitle("StatusNotifierItem Example");

    /* ---------- Toggleable Context Menu (starts empty) ---------- */
    QMenu *menu = nullptr;
    auto toggleMenu = [&]() {
        if (menu == nullptr) {
            menu = new QMenu();
            QAction *item = menu->addAction("Item sample");
            QObject::connect(item, &QAction::triggered, []() {
                qDebug() << "Menu item clicked";
            });
            trayIcon.setContextMenu(menu);
            qDebug() << "Menu attached";
        } else {
            trayIcon.setContextMenu(nullptr);
            menu->deleteLater();
            menu = nullptr;
            qDebug() << "Menu detached";
        }
    };

    // Primary click toggles the menu on/off
    QObject::connect(&trayIcon, &StatusNotifierItem::activateRequested, [&](const QPoint &) {
        toggleMenu();
    });

    return app.exec();
}