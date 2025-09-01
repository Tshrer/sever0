// jsontcpserver.cpp
#include "jsontcpserver.h"


JsonTcpServer::JsonTcpServer(QObject *parent)
    : QObject(parent)
    , tcpServer(nullptr)
{
}

JsonTcpServer::~JsonTcpServer()
{
    close();
}

bool JsonTcpServer::start(QHostAddress hostAddr, quint16 port)
{
    if (tcpServer) {
        close();
    }

    tcpServer = new QTcpServer(this);
    connect(tcpServer, &QTcpServer::newConnection, this, &JsonTcpServer::onNewConnection);

    if (!tcpServer->listen(hostAddr, port)) {
        QString errorMsg = QString("Server could not start on port %1: %2")
        .arg(port)
        .arg(tcpServer->errorString());
        //emit errorOccurred(errorMsg);
        qWarning() << errorMsg;
        delete tcpServer;
        tcpServer = nullptr;
        return false;
    }
    // emit wrnLog("test1");
    // emit log("test2");
    emit log(QString("JSON TCP Server started on %1 : %2")
                 .arg(hostAddr.toString())
                 .arg(QString::number(port)));
    qDebug() << "JSON TCP Server started on port" << port;

    return true;
}

void JsonTcpServer::close()
{
    if (tcpServer) {
        tcpServer->close();

        // 断开所有客户端连接
        for (QTcpSocket *socket : clientBuffers.keys()) {
            socket->disconnectFromHost();
            if (socket->state() != QTcpSocket::UnconnectedState) {
                socket->waitForDisconnected(1000);
            }
            socket->deleteLater();
        }

        clientBuffers.clear();
        clientExpectedSizes.clear();

        delete tcpServer;
        tcpServer = nullptr;
    }
    emit log("JSON TCP Server stopped");
    qDebug() << "JSON TCP Server stopped";
}

bool JsonTcpServer::sendToClient(QTcpSocket *clientSocket, const QJsonDocument &document)
{
    if (!clientSocket || clientSocket->state() != QTcpSocket::ConnectedState) {
        qWarning() << "Client socket is not connected";
        emit wrnLog("Client socket is not connected");
        return false;
    }

    if (!clientBuffers.contains(clientSocket)) {
        qWarning() << "Client socket not found in connected clients";
        emit wrnLog("Client socket not found in connected clients");
        return false;
    }

    return sendJsonToSocket(clientSocket, document);
}

bool JsonTcpServer::broadcast(const QJsonDocument &document)
{
    if (clientBuffers.isEmpty()) {
        qDebug() << "No clients connected to broadcast";
        return true; // 没有客户端也算成功
    }

    bool allSuccess = true;
    int successCount = 0;

    for (auto it = clientBuffers.begin(); it != clientBuffers.end(); ++it) {
        QTcpSocket *socket = it.key();
        if (socket->state() == QTcpSocket::ConnectedState) {
            if (sendJsonToSocket(socket, document)) {
                successCount++;
            } else {
                allSuccess = false;
            }
        }
    }

    qDebug() << "Broadcast to" << successCount << "clients, success:" << allSuccess;
    return allSuccess;
}

int JsonTcpServer::clientCount() const
{
    return clientBuffers.size();
}

QList<QTcpSocket*> JsonTcpServer::connectedClients() const
{
    return clientBuffers.keys();
}

bool JsonTcpServer::sendJsonToSocket(QTcpSocket *socket, const QJsonDocument &document)
{
    if (!socket || socket->state() != QTcpSocket::ConnectedState) {
        return false;
    }

    QByteArray jsonData = document.toJson(QJsonDocument::Compact);

    // 使用长度前缀法：先发送4字节的数据长度，再发送数据
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_10);
    stream << static_cast<quint32>(jsonData.size());
    packet.append(jsonData);

    qint64 bytesWritten = socket->write(packet);
    if (bytesWritten == -1) {
        qWarning() << "Write error to client" << socket->peerAddress().toString()
        << ":" << socket->errorString();
        emit wrnLog(QString("Write error to client %1 : %2")
                        .arg(socket->peerAddress().toString()
                        .arg(socket->errorString())));
        return false;
    }

    if (bytesWritten != packet.size()) {
        qWarning() << "Partial write to client" << socket->peerAddress().toString()
        << ":" << bytesWritten << "of" << packet.size() << "bytes";
        return false;
    }

    socket->flush();
    qDebug() << "Sent JSON to client" << socket->peerAddress().toString()
             << ", size:" << jsonData.size() << "bytes\n" << document.toJson();
    emit log(QString("sent json to client %1 ,size: %2 bytes")
             .arg(socket->peerAddress().toString(),QString::number(jsonData.size())));
    return true;
}

bool JsonTcpServer::sendRawJsonToSocket(QTcpSocket *socket, const QJsonDocument &document)
{
    if (!socket || socket->state() != QTcpSocket::ConnectedState) {
        return false;
    }

    QByteArray jsonData = document.toJson(QJsonDocument::Compact);
    qint64 bytesWritten = socket->write(jsonData);
    if (bytesWritten == -1) {
        qWarning() << "Write error to client" << socket->peerAddress().toString()
        << ":" << socket->errorString();
        return false;
    }

    if (bytesWritten != jsonData.size()) {
        qWarning() << "Partial write to client" << socket->peerAddress().toString()
        << ":" << bytesWritten << "of" << jsonData.size() << "bytes";
        return false;
    }

    socket->flush();
    qDebug() << "Sent JSON to client" << socket->peerAddress().toString()
             << ", size:" << jsonData.size() << "bytes";
    return true;
}

void JsonTcpServer::onNewConnection()
{
    QTcpSocket *clientSocket = tcpServer->nextPendingConnection();
    if (!clientSocket) {
        return;
    }

    QString clientInfo = QString("%1:%2")
                             .arg(clientSocket->peerAddress().toString())
                             .arg(clientSocket->peerPort());

    connect(clientSocket, &QTcpSocket::readyRead, this, &JsonTcpServer::onClientReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &JsonTcpServer::onClientDisconnected);

    clientBuffers[clientSocket] = QByteArray();
    clientExpectedSizes[clientSocket] = 0;

    emit log("client connected: "+ clientInfo);

    qDebug() << "Client connected:" << clientInfo;

    emit clientConnected(clientSocket);
}

void JsonTcpServer::onClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !clientBuffers.contains(socket)) {
        return;
    }

    QByteArray &buffer = clientBuffers[socket];
    buffer.append(socket->readAll());
    processReceiveBuffer(socket);
}

void JsonTcpServer::processReceiveBuffer(QTcpSocket *socket)
{
    if (!clientBuffers.contains(socket) || !clientExpectedSizes.contains(socket)) {
        return;
    }

    QByteArray &buffer = clientBuffers[socket];
    quint32 &expectedSize = clientExpectedSizes[socket];

    while (true) {
        if (expectedSize == 0) {

            // 需要读取数据长度
            if (buffer.size() < static_cast<int>(sizeof(quint32))) {
                break; // 等待更多数据
            }

            QDataStream stream(buffer);
            stream.setVersion(QDataStream::Qt_5_10);
            stream >> expectedSize;
            buffer.remove(0, sizeof(quint32));

            qDebug() << "Expecting data size from client" << socket->peerAddress().toString()
                     << ":" << expectedSize << "bytes";
            emit log(QString("expecting data size from client %1 : %2 bytes")
                    .arg(socket->peerAddress().toString(),QString::number(expectedSize)));
        }

        // 检查是否收到完整数据
        if (buffer.size() < expectedSize) {
            break; // 等待更多数据
        }

        // 提取完整JSON数据
        QByteArray jsonData = buffer.left(expectedSize);
        buffer.remove(0, expectedSize);

        // 解析JSON
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonData, &error);

        if (error.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error from client" << socket->peerAddress().toString()
            << ":" << error.errorString();

            // 发送错误响应
            QJsonObject errorResponse;
            errorResponse["status"] = "error";
            errorResponse["message"] = "Invalid JSON format";
            sendToClient(socket, QJsonDocument(errorResponse));
        } else {
            qDebug() << "Received JSON from client" << socket->peerAddress().toString()
                     << ", size:" << jsonData.size() << "bytes\n" << document.toJson();

            // 发起信号通知接收到JSON文档
            emit jsonDocumentReceived(socket, document);
        }

        expectedSize = 0;
    }
}

void JsonTcpServer::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (socket) {
        QString clientInfo = QString("%1:%2")
        .arg(socket->peerAddress().toString())
            .arg(socket->peerPort());

        clientBuffers.remove(socket);
        clientExpectedSizes.remove(socket);

        qDebug() << "Client disconnected:" << clientInfo;
        emit clientDisconnected(socket);

        socket->deleteLater();
    }
}

void JsonTcpServer::whileJsonNeedSend(QTcpSocket *clientSocket, const QJsonDocument &document)
{
    if(clientSocket){
        sendToClient(clientSocket,document);
    }
    else{
        //未指定则广播
        broadcast(document);
    }
}
