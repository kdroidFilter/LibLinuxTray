// qtthreadmanager.cpp
#include "qtthreadmanager.h"
#include <QMetaObject>

QtThreadManager* QtThreadManager::instance()
{
    static QtThreadManager* inst = []{
        auto* t = new QtThreadManager();
        t->start();              // Lance le thread et donc run()
        t->wait();               // Attend que QApplication soit prêt
        return t;
    }();
    return inst;
}

void QtThreadManager::shutdown()
{
    auto* t = instance();
    QMetaObject::invokeMethod(t->m_app, "quit", Qt::QueuedConnection);
    t->quit(); t->wait();
}

void QtThreadManager::run()
{
    int argc = 0;
    m_app = new QApplication(argc, nullptr);
    exec();                      // Boucle Qt dédiée
    delete m_app;
    m_app = nullptr;
}

void QtThreadManager::runBlocking(const std::function<void()>& fn)
{
    // Si nous sommes déjà dans le thread Qt, exécuter directement
    if (QThread::currentThread() == this) {
        fn();
        return;
    }

    // Sinon utiliser BlockingQueuedConnection
    QMetaObject::invokeMethod(this, [fn]{ fn(); },
                              Qt::BlockingQueuedConnection);
}

void QtThreadManager::runAsync(const std::function<void()>& fn)
{
    QMetaObject::invokeMethod(this, [fn]{ fn(); },
                              Qt::QueuedConnection);
}
