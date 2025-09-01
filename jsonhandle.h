#ifndef JSONHANDLE_H
#define JSONHANDLE_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include "sqldatabase.h"
#include <QJsonArray>
#include <QTcpSocket>
#include <QDateTime>

class JsonHandle : public QObject
{
    Q_OBJECT

public:
    explicit JsonHandle(const QJsonDocument &request, QTcpSocket *clientSocket, SqlDataBase *database, QObject *parent = nullptr);

    void query(); // 执行查询处理

signals:
    void responseReady(QTcpSocket *clientSocket,const QJsonDocument &response);
    void processingFinished(JsonHandle *handle); // 处理完成信号，用于队列管理
    void log(const QString& logStr);
    void wrnLog(const QString& wrnStr);
private:
    QJsonDocument m_request;
    QTcpSocket *m_clientSocket;
    SqlDataBase *m_database;

    QString currentTime();
    QJsonArray patientInfoBuffer;
    int bufferIndex = 0;
    bool doctorOnline = false;
    bool getBuffer = false;

};

#endif // JSONHANDLE_H
