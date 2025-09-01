#include "widget.h"
#include "ui_widget.h"
#include "jsontcpserver.h"
#include "jsonhandle.h"
#include "jsonhandlequeue.h"
#include "sqldatabase.h"
#include "logout.h"
#include <QCoreApplication>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    this->setLayout(ui->verticalLayout);
    listeningState = false;
    ui->btnListenState->setText("Listen:");
    ui->lineEditPort->setText("8000");

    //创建日志处理类
    log = new LogOut(ui->logTxt, this);

    //创建json收发服务器和处理队列
    server = new JsonTcpServer(this);
    jsonHandlerQueue = new JsonHandleQueue(this);

    //连接相关槽函数
    QObject::connect(server, &JsonTcpServer::log, log, &LogOut::sLog);
    QObject::connect(server, &JsonTcpServer::wrnLog, log, &LogOut::sWarning);
    QObject::connect(jsonHandlerQueue, &JsonHandleQueue::log , log, &LogOut::sLog);
    QObject::connect(jsonHandlerQueue, &JsonHandleQueue::wrnLog, log, &LogOut::sWarning);

    //数据库
    database = new SqlDataBase("MedicalData.db",this);

    //qDebug() <<QCoreApplication::applicationDirPath();
    //获取可用ip地址
    QList<QHostAddress> addressList = QNetworkInterface::allAddresses();
    for(QHostAddress& temp : addressList){

        if(temp.protocol() == QAbstractSocket::IPv4Protocol){
            ui->cbxListeningIP->addItem(temp.toString());
        }

    }

    QObject::connect(ui->btnListenState, &QPushButton::clicked, this ,&Widget::while_btnListengingState_clicked);
    QObject::connect(server, &JsonTcpServer::jsonDocumentReceived, this, &Widget::addNewRequestInQueue);
}

Widget::~Widget()
{
    delete ui;
    delete jsonHandlerQueue;
}

void Widget::while_btnListengingState_clicked()
{
    if(!listeningState){//listening
        listeningState ^= serverListen(QHostAddress(QString(ui->cbxListeningIP->currentText())),
                                       ui->lineEditPort->text().toInt());
        ui->cbxListeningIP->setEnabled(!listeningState);
        ui->lineEditPort->setEnabled(!listeningState);
    }
    else{//close
        listeningState ^= serverClose();
        ui->cbxListeningIP->setEnabled(!listeningState);
        ui->lineEditPort->setEnabled(!listeningState);
    }
    ui->btnListenState->setText(listeningState?"Listening:":"listen");
}

void Widget::addNewRequestInQueue(QTcpSocket *clientSocket, const QJsonDocument &document)
{
    JsonHandle *requestHandle = new JsonHandle(document, clientSocket, database, this);

    //连接新建请求处理类相关信号
    QObject::connect(requestHandle, &JsonHandle::responseReady, server, &JsonTcpServer::whileJsonNeedSend);
    QObject::connect(requestHandle, &JsonHandle::log, log, &LogOut::log);
    QObject::connect(requestHandle, &JsonHandle::wrnLog, log, &LogOut::warning);

    //放入队列执行
    jsonHandlerQueue->enqueueHandle(requestHandle);
}

// void Widget::handleJsonDocument(QTcpSocket *clientSocket, const QJsonDocument &document)
// {
//     qDebug()<<"receive JSON";
//     qDebug() << document.toJson();
// }


bool Widget::serverListen(QHostAddress hostAddr,qint16 port)
{
    qDebug() << hostAddr << port;

    if(!server->start(hostAddr, port)){
        QMessageBox msgBox;
        msgBox.setWindowTitle("失败");
        msgBox.setText("无法监听指定端口");
        msgBox.exec();
        return false;
    }

    //writeLog("listening"+hostAddr.toString()+":"+QString::number(port));
    return true;
}

bool Widget::serverClose()
{
    QList<QTcpSocket *> tcpSocketClients = server->findChildren<QTcpSocket *>();
    QList<QTcpSocket *>& range = tcpSocketClients;

    for(const auto & tmp : range){
        tmp->close();
    }

    server->close();
    return true;
}


