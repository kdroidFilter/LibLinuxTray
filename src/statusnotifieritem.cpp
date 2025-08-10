/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org/
 *
 * Copyright: 2015 LXQt team
 * Authors:
 *   Paulo Lieuthier <paulolieuthier@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "statusnotifieritem.h"
#include "statusnotifieritemadaptor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QtEndian>
#include <QMenu>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QSysInfo>
#include <QSize>
#include <QPoint>
#include <QVariantMap>
#include <QList>
#include <utility>
#include <dbusmenuexporter.h>

int StatusNotifierItem::mServiceCounter = 0;

// ------------------------------------------------------------------
// Utilitaire : chemin DBus à utiliser quand il n'y a PAS de menu.
// - KDE/Plasma : "/NO_DBUSMENU"
// - GNOME/Autres : "/"
// Détection simple via variables d'environnement.
// ------------------------------------------------------------------
static inline QString noMenuPathForEnvironment()
{
    const QString xdg  = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const QString sess = qEnvironmentVariable("DESKTOP_SESSION").toLower();
    const bool kdeFull = qEnvironmentVariableIsSet("KDE_FULL_SESSION");

    if (xdg.contains("kde") || xdg.contains("plasma") ||
        sess.contains("kde") || sess.contains("plasma") || kdeFull) {
        return QLatin1String("/NO_DBUSMENU");
    }
    return QLatin1String("/");
}

StatusNotifierItem::StatusNotifierItem(QString id, QObject *parent)
    : QObject(parent),
      mAdaptor(new StatusNotifierItemAdaptor(this)),
      mService(QString::fromLatin1("org.freedesktop.StatusNotifierItem-%1-%2")
                   .arg(QCoreApplication::applicationPid())
                   .arg(++mServiceCounter)),
      mId(std::move(id)),
      mTitle(QLatin1String("Test")),
      mStatus(QLatin1String("Active")),
      mCategory(QLatin1String("ApplicationStatus")),
      mMenu(nullptr),
      mMenuPath(QLatin1String("/")),              // valeur initiale ; corrigée juste après
      mMenuExporter(nullptr),
      mSessionBus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, mService))
{
    // Enregistrer nos types D-Bus (une seule fois)
    static bool s_registered = false;
    if (!s_registered) {
        qDBusRegisterMetaType<IconPixmap>();
        qDBusRegisterMetaType<IconPixmapList>();
        qDBusRegisterMetaType<ToolTip>();
        s_registered = true;
    }

    // Publier l’objet
    mSessionBus.registerObject(QLatin1String("/StatusNotifierItem"), this);

    // Chemin « pas de menu » adapté à l’environnement courant
    setMenuPath(noMenuPathForEnvironment());

    registerToHost();

    // Re-registration si le watcher/host change de propriétaire
    auto *watcher = new QDBusServiceWatcher(
        QLatin1String("org.kde.StatusNotifierWatcher"), mSessionBus,
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &StatusNotifierItem::onServiceOwnerChanged);
}

StatusNotifierItem::~StatusNotifierItem()
{
    mSessionBus.unregisterObject(QLatin1String("/StatusNotifierItem"));
    QDBusConnection::disconnectFromBus(mService);
}

void StatusNotifierItem::registerToHost()
{
    QDBusInterface interface(QLatin1String("org.kde.StatusNotifierWatcher"),
                             QLatin1String("/StatusNotifierWatcher"),
                             QLatin1String("org.kde.StatusNotifierWatcher"),
                             mSessionBus);
    interface.asyncCall(QLatin1String("RegisterStatusNotifierItem"), mSessionBus.baseService());
}

void StatusNotifierItem::onServiceOwnerChanged(const QString& service,
                                               const QString& oldOwner,
                                               const QString& newOwner)
{
    Q_UNUSED(service);
    Q_UNUSED(oldOwner);

    if (!newOwner.isEmpty())
        registerToHost();
}

void StatusNotifierItem::onMenuDestroyed()
{
    mMenu = nullptr;

    if (mMenuExporter) {
        delete mMenuExporter;
        mMenuExporter = nullptr;
    }

    // Signaler qu’il n’y a plus de menu (chemin dépend de l’ENV)
    setMenuPath(noMenuPathForEnvironment());
}

/* ---------------------- Propriétés simples ---------------------- */

void StatusNotifierItem::setTitle(const QString &title)
{
    if (mTitle == title)
        return;
    mTitle = title;
    Q_EMIT mAdaptor->NewTitle();
}

void StatusNotifierItem::setStatus(const QString &status)
{
    if (mStatus == status)
        return;
    mStatus = status;
    Q_EMIT mAdaptor->NewStatus(mStatus);
}

void StatusNotifierItem::setCategory(const QString &category)
{
    if (mCategory == category)
        return;
    mCategory = category;
}

/* ---------------------- Menu (chemin + notification) ---------------------- */

void StatusNotifierItem::setMenuPath(const QString& path)
{
    if (mMenuPath.path() == path)
        return;

    mMenuPath.setPath(path);

    // Informer l’hôte que la propriété « Menu » a changé
    QDBusMessage msg = QDBusMessage::createSignal(
        QLatin1String("/StatusNotifierItem"),
        QLatin1String("org.freedesktop.DBus.Properties"),
        QLatin1String("PropertiesChanged"));

    msg << QLatin1String("org.kde.StatusNotifierItem");

    QVariantMap changed;
    changed.insert(QLatin1String("Menu"), QVariant::fromValue(menu()));
    msg << changed << QStringList{}; // pas de propriétés invalidées

    mSessionBus.send(msg);
}

/* ---------------------- Icônes ---------------------- */

void StatusNotifierItem::setIconByName(const QString &name)
{
    if (mIconName == name)
        return;

    mIconName = name;
    mIcon.clear();
    mIconCacheKey = 0;
    Q_EMIT mAdaptor->NewIcon();
}

void StatusNotifierItem::setIconByPixmap(const QIcon &icon)
{
    if (mIconCacheKey == icon.cacheKey())
        return;

    mIconCacheKey = icon.cacheKey();
    mIcon = iconToPixmapList(icon);
    mIconName.clear();
    Q_EMIT mAdaptor->NewIcon();
}

void StatusNotifierItem::setOverlayIconByName(const QString &name)
{
    if (mOverlayIconName == name)
        return;

    mOverlayIconName = name;
    mOverlayIcon.clear();
    mOverlayIconCacheKey = 0;
    Q_EMIT mAdaptor->NewOverlayIcon();
}

void StatusNotifierItem::setOverlayIconByPixmap(const QIcon &icon)
{
    if (mOverlayIconCacheKey == icon.cacheKey())
        return;

    mOverlayIconCacheKey = icon.cacheKey();
    mOverlayIcon = iconToPixmapList(icon);
    mOverlayIconName.clear();
    Q_EMIT mAdaptor->NewOverlayIcon();
}

void StatusNotifierItem::setAttentionIconByName(const QString &name)
{
    if (mAttentionIconName == name)
        return;

    mAttentionIconName = name;
    mAttentionIcon.clear();
    mAttentionIconCacheKey = 0;
    Q_EMIT mAdaptor->NewAttentionIcon();
}

void StatusNotifierItem::setAttentionIconByPixmap(const QIcon &icon)
{
    if (mAttentionIconCacheKey == icon.cacheKey())
        return;

    mAttentionIconCacheKey = icon.cacheKey();
    mAttentionIcon = iconToPixmapList(icon);
    mAttentionIconName.clear();
    Q_EMIT mAdaptor->NewAttentionIcon();
}

void StatusNotifierItem::setToolTipTitle(const QString &title)
{
    if (mTooltipTitle == title)
        return;

    mTooltipTitle = title;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipSubTitle(const QString &subTitle)
{
    if (mTooltipSubtitle == subTitle)
        return;

    mTooltipSubtitle = subTitle;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipIconByName(const QString &name)
{
    if (mTooltipIconName == name)
        return;

    mTooltipIconName = name;
    mTooltipIcon.clear();
    mTooltipIconCacheKey = 0;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipIconByPixmap(const QIcon &icon)
{
    if (mTooltipIconCacheKey == icon.cacheKey())
        return;

    mTooltipIconCacheKey = icon.cacheKey();
    mTooltipIcon = iconToPixmapList(icon);
    mTooltipIconName.clear();
    Q_EMIT mAdaptor->NewToolTip();
}

/* ---------------------- Attachement/détachement du menu ---------------------- */

void StatusNotifierItem::setContextMenu(QMenu* menu)
{
    if (mMenu == menu)
        return;

    if (mMenu)
        disconnect(mMenu, &QObject::destroyed, this, &StatusNotifierItem::onMenuDestroyed);

    mMenu = menu;

    // Toujours libérer l’exporter avant de (re)créer
    if (mMenuExporter) {
        delete mMenuExporter;
        mMenuExporter = nullptr;
    }

    if (mMenu) {
        // Menu présent
        setMenuPath(QLatin1String("/MenuBar"));
        connect(mMenu, &QObject::destroyed, this, &StatusNotifierItem::onMenuDestroyed);
        mMenuExporter = new DBusMenuExporter{ QLatin1String("/MenuBar"), mMenu, mSessionBus };
    } else {
        // Plus de menu (chemin adapté à l’ENV)
        setMenuPath(noMenuPathForEnvironment());
    }
}

/* ---------------------- Appels DBus (actions) ---------------------- */

void StatusNotifierItem::Activate(int x, int y)
{
    if (mStatus == QLatin1String("NeedsAttention"))
        mStatus = QLatin1String("Active");

    Q_EMIT mAdaptor->NewStatus(mStatus);
    Q_EMIT activateRequested(QPoint(x, y));
}

void StatusNotifierItem::SecondaryActivate(int x, int y)
{
    if (mStatus == QLatin1String("NeedsAttention"))
        mStatus = QLatin1String("Active");

    Q_EMIT mAdaptor->NewStatus(mStatus);
    Q_EMIT secondaryActivateRequested(QPoint(x, y));
}

void StatusNotifierItem::ContextMenu(int x, int y)
{
    if (!mMenu)
        return;

    if (mMenu->isVisible())
        mMenu->hide();
    else
        mMenu->popup(QPoint(x, y));
}

void StatusNotifierItem::Scroll(int delta, const QString &orientation)
{
    Qt::Orientation orient = Qt::Vertical;
    if (orientation.toLower() == QLatin1String("horizontal"))
        orient = Qt::Horizontal;

    Q_EMIT scrollRequested(delta, orient);
}

/* ---------------------- Divers utilitaires ---------------------- */

void StatusNotifierItem::showMessage(const QString& title, const QString& msg,
                                     const QString& iconName, int secs)
{
    QDBusInterface interface(QLatin1String("org.freedesktop.Notifications"),
                             QLatin1String("/org/freedesktop/Notifications"),
                             QLatin1String("org.freedesktop.Notifications"),
                             mSessionBus);
    interface.call(QLatin1String("Notify"), mTitle, (uint)0, iconName, title,
                   msg, QStringList(), QVariantMap(), secs);
}

IconPixmapList StatusNotifierItem::iconToPixmapList(const QIcon &icon)
{
    IconPixmapList pixmapList;

    QList<QSize> sizes = icon.availableSizes();
    if (sizes.isEmpty())
        sizes = { {16,16}, {22,22}, {24,24}, {32,32}, {48,48} };

    for (const QSize &sz : std::as_const(sizes)) {
        QPixmap pm = icon.pixmap(sz);
        if (pm.isNull())
            continue;

        QImage img = pm.toImage();
        if (img.format() != QImage::Format_ARGB32)
            img = img.convertToFormat(QImage::Format_ARGB32);

        IconPixmap p;
        p.width  = img.width();
        p.height = img.height();
        p.bytes  = QByteArray(reinterpret_cast<char*>(img.bits()),
                              img.sizeInBytes());

        // D-Bus spécifie big-endian pour ARGB, convertir si nécessaire
        if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
            auto *u = reinterpret_cast<quint32*>(p.bytes.data());
            for (uint i = 0; i < static_cast<uint>(p.bytes.size()) / sizeof(quint32); ++i, ++u)
                *u = qToBigEndian(*u);
        }
        pixmapList.append(p);
    }

    // Fallback — garantir au moins un 32px
    if (pixmapList.isEmpty()) {
        QImage img = icon.pixmap(32, 32).toImage();
        if (!img.isNull()) {
            if (img.format() != QImage::Format_ARGB32)
                img = img.convertToFormat(QImage::Format_ARGB32);

            IconPixmap p;
            p.width  = img.width();
            p.height = img.height();
            p.bytes  = QByteArray(reinterpret_cast<char*>(img.bits()),
                                  img.sizeInBytes());
            if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
                auto *u = reinterpret_cast<quint32*>(p.bytes.data());
                for (uint i = 0; i < static_cast<uint>(p.bytes.size()) / sizeof(quint32); ++i, ++u)
                    *u = qToBigEndian(*u);
            }
            pixmapList.append(p);
        }
    }

    return pixmapList;
}

void StatusNotifierItem::unregister()
{
    mSessionBus.unregisterObject(QLatin1String("/StatusNotifierItem"));
    QDBusConnection::disconnectFromBus(mService);
}

void StatusNotifierItem::forceUpdate()
{
    Q_EMIT mAdaptor->NewIcon();
    Q_EMIT mAdaptor->NewToolTip();
    Q_EMIT mAdaptor->NewStatus(mStatus);
}
