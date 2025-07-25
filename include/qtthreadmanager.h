// qtthreadmanager.h
#pragma once
#include <QThread>
#include <QApplication>
#include <QMutex>
#include <functional>

class QtThreadManager : public QThread
{
    Q_OBJECT
public:
    static QtThreadManager* instance();
    static void shutdown();

    // Exécute fn dans le thread Qt et attend la fin (blockant)
    void runBlocking(const std::function<void()>& fn);

    // Variante asynchrone (queued, ne bloque pas l’appelant)
    void runAsync(const std::function<void()>& fn);

protected:
    void run() override;          // crée QApplication + exec()

private:
    QtThreadManager() = default;
    ~QtThreadManager() override = default;

    QApplication* m_app = nullptr;
};
