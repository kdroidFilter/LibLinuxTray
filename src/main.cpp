#include <QApplication>
#include <QIcon>
#include <QMenu>
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

    /* ---------- Context Menu ---------- */
    QMenu *menu = new QMenu();

    // Existing Action 1
    QAction *action1 = menu->addAction("Action 1");
    QObject::connect(action1, &QAction::triggered, [](){
        qDebug() << "Action 1 was clicked!";
    });

    // ----- New item: dynamically change the icon -----
    QAction *changeIconAction = menu->addAction("Change icon");
    QObject::connect(changeIconAction, &QAction::triggered,
                     [&trayIcon, &useAltIcon, iconPath1, iconPath2](){
        const QString &nextPath = useAltIcon ? iconPath1 : iconPath2;
        QIcon newIcon(nextPath);
        if (newIcon.isNull()) {
            qWarning() << "Failed to load new icon" << nextPath;
            return;
        }
        trayIcon.setIconByPixmap(newIcon);
        useAltIcon = !useAltIcon;  // Reverse for next time
        qDebug() << "Icon changed to" << nextPath;
    });

    // Exit the application
    menu->addAction("Exit", &app, &QApplication::quit);
    trayIcon.setContextMenu(menu);

    return app.exec();
}
