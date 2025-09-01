// jsontcpserver.h
#ifndef JSONTCPSERVER_H
#define JSONTCPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QMap>
#include <QDataStream>
#include <QDateTime>
#include <QHostAddress>
#include <QDebug>


class JsonTcpServer : public QObject
{
    Q_OBJECT
public:
    explicit JsonTcpServer(QObject *parent = nullptr);
    ~JsonTcpServer();

    // 启动服务器
    bool start(QHostAddress hostAddr, quint16 port);

    // 停止服务器
    void close();

    // 向指定客户端发送JSON文档
    bool sendToClient(QTcpSocket *clientSocket, const QJsonDocument &document);

    // 向所有连接的客户端广播JSON文档
    bool broadcast(const QJsonDocument &document);

    // 获取当前连接的客户端数量
    int clientCount() const;

    // 获取所有客户端套接字列表
    QList<QTcpSocket*> connectedClients() const;

signals:
    // 接收到JSON文档的信号
    void jsonDocumentReceived(QTcpSocket *clientSocket, const QJsonDocument &document);

    // 客户端连接信号
    void clientConnected(QTcpSocket *clientSocket);

    // 客户端断开信号
    void clientDisconnected(QTcpSocket *clientSocket);

    // 错误信号
    void errorOccurred(const QString &errorMessage);

    void log(const QString& logStr);
    void wrnLog(const QString& wrnStr);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
public slots:
    void whileJsonNeedSend(QTcpSocket *clientSocket, const QJsonDocument &document);

private:
    // 处理接收缓冲区
    void processReceiveBuffer(QTcpSocket *socket);

    // 内部发送函数
    bool sendJsonToSocket(QTcpSocket *socket, const QJsonDocument &document);

    //直接发送原始数据
    bool sendRawJsonToSocket(QTcpSocket *socket, const QJsonDocument &document);
    QTcpServer *tcpServer;
    QMap<QTcpSocket*, QByteArray> clientBuffers;      // 客户端接收缓冲区
    QMap<QTcpSocket*, quint32> clientExpectedSizes;   // 客户端期望的数据大小
};

#endif // JSONTCPSERVER_H
