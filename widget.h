#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QList>
#include <QMessageBox>
#include <QString>

#include "jsontcpserver.h"
#include "jsonhandlequeue.h"
#include "sqldatabase.h"
#include "logout.h"
QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
//public slots:

private slots:
    void while_btnListengingState_clicked();

    void addNewRequestInQueue(QTcpSocket *clientSocket, const QJsonDocument &document);
    //void handleJsonDocument(QTcpSocket *clientSocket, const QJsonDocument &document);
private:
    Ui::Widget *ui;
    JsonTcpServer *server;
    JsonHandleQueue *jsonHandlerQueue;
    SqlDataBase *database;
    LogOut * log;
    bool listeningState;
    bool serverListen(QHostAddress, qint16);
    bool serverClose();
    void writeLog(QString);


};
#endif // WIDGET_H
