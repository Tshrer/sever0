#include "jsonhandlequeue.h"
#include <QDateTime>
#include <QDebug>

JsonHandleQueue::JsonHandleQueue(QObject *parent)
    : QObject(parent)
    , m_processing(false)
    , m_shutdown(false)
    , m_currentHandle(nullptr)
{
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    connect(m_processTimer, &QTimer::timeout, this, &JsonHandleQueue::processNextHandle);
}

JsonHandleQueue::~JsonHandleQueue()
{
    stopProcessing();
    clearQueue();
}


void JsonHandleQueue::enqueueHandle(JsonHandle *handle)
{
    if (!handle) {
        qWarning() << "Attempted to enqueue null JsonHandle";
        return;
    }

    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(handle);
    qDebug() << "JsonHandle enqueued. Queue size:" << m_queue.size();

    // 如果没有正在处理且定时器未激活，立即开始处理
    if (!m_processing && !m_processTimer->isActive() && m_currentHandle == nullptr) {
        qDebug() << "Starting processing...";
        startProcessing();  // 显式调用 startProcessing() 启动定时器
    }

    emit queueStatusChanged(m_queue.size(), m_processing);
}

int JsonHandleQueue::pendingHandles() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

bool JsonHandleQueue::isProcessing() const
{
    return m_processing;
}

void JsonHandleQueue::startProcessing(int processingIntervalMs)
{
    if (m_shutdown) {
        qWarning() << "Cannot start processing after shutdown";
        return;
    }

    m_processTimer->setInterval(processingIntervalMs);
    m_processing = true;

    // 确保定时器启动时，队列处理被执行
    QMetaObject::invokeMethod(this, "processNextHandle", Qt::QueuedConnection);
}


void JsonHandleQueue::stopProcessing()
{
    m_processing = false;
    m_processTimer->stop();
}

void JsonHandleQueue::clearQueue()
{
    QMutexLocker locker(&m_mutex);
    while (!m_queue.isEmpty()) {
        JsonHandle *handle = m_queue.dequeue();
        handle->deleteLater();
    }
    m_condition.wakeAll();
}

void JsonHandleQueue::processNextHandle()
{
    // 如果在处理过程中不允许再调用，直接返回
    if (m_shutdown || !m_processing) {
        return;
    }

    JsonHandle *nextHandle = nullptr;
    bool hasHandle = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_queue.isEmpty()) {
            nextHandle = m_queue.dequeue();
            hasHandle = true;
            m_currentHandle = nextHandle;
            m_processing = true;  // 正在处理
        } else {
            m_processing = false;  // 队列空，停止处理
            m_currentHandle = nullptr;
            emit queueEmpty();
            emit queueStatusChanged(0, false);
            m_processTimer->stop();  // 停止定时器
            return;
        }
    }//锁作用域

    if (hasHandle && nextHandle) {
        int cntOfHandle = pendingHandles();
        qDebug() << "Processing JsonHandle. Remaining in queue:" << cntOfHandle;
        //emit log("")

        emit handleStarted(nextHandle);
        emit queueStatusChanged(pendingHandles(), true);

        // 使用 QtConcurrent 进行异步处理
        QtConcurrent::run([this, nextHandle]() {
            qDebug() << "Running query for handle" << nextHandle;
            emit log("running query at : " +QString::asprintf("%p", static_cast<void*>(nextHandle)));
            nextHandle->query();
            emit handleCompleted(nextHandle);  // 完成后发射信号
            // 处理完毕后，进行下一次处理
            QMetaObject::invokeMethod(this, "processNextHandle", Qt::QueuedConnection);
        });
    }
}


void JsonHandleQueue::cleanupHandle(JsonHandle *handle)
{
    if (handle == m_currentHandle) {
        m_currentHandle = nullptr;
    }

    // 删除handle
    handle->deleteLater();
    qDebug() << "JsonHandle processing completed";
    emit log("JsonHandle processing completed");
    emit handleCompleted(handle); // 确保这个信号在完成后发射
}
