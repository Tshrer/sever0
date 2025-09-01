#ifndef JSONHANDLEQUEUE_H
#define JSONHANDLEQUEUE_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QtConcurrent/QtConcurrent> // 添加QtConcurrent支持
#include "jsonhandle.h"

class JsonHandleQueue : public QObject
{
    Q_OBJECT

public:
    explicit JsonHandleQueue(QObject *parent = nullptr);
    ~JsonHandleQueue();

    // 添加JsonHandle到队列
    void enqueueHandle(JsonHandle *handle);

    // 队列管理
    int pendingHandles() const;
    bool isProcessing() const;
    void startProcessing(int processingIntervalMs = 50);
    void stopProcessing();
    void clearQueue();

signals:
    void handleStarted(JsonHandle *handle);
    void handleCompleted(JsonHandle *handle);
    void queueEmpty();
    void queueStatusChanged(int pendingCount, bool isProcessing);
    void log(const QString& logStr);
    void wrnLog(const QString& wrnStr);
public slots:
    void processNextHandle();

private:
    QQueue<JsonHandle*> m_queue;
    mutable QMutex m_mutex;
    QWaitCondition m_condition;

    std::atomic<bool> m_processing;
    std::atomic<bool> m_shutdown;

    QTimer *m_processTimer;
    JsonHandle *m_currentHandle;

    void cleanupHandle(JsonHandle *handle);
};

#endif // JSONHANDLEQUEUE_H
