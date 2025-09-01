#ifndef LOGOUT_H
#define LOGOUT_H

#include <QObject>
#include <QTextBrowser>
#include <QDateTime>
#include <QString>
class LogOut : public QObject
{
    Q_OBJECT
public:
    explicit LogOut(QTextBrowser * out,QObject *parent = nullptr);
    void log(const QString& logStr);
    void warning(const QString& wrnStr);
signals:

public slots:
    void sLog(const QString& logStr);
    void sWarning(const QString& wrnStr);

private:
    QTextBrowser * m_Out;

    QString currentTime();
};

#endif // LOGOUT_H
