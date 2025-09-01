#include "logout.h"

LogOut::LogOut(QTextBrowser * out,QObject *parent)
    : QObject{parent}
    , m_Out(out)
{
}

void LogOut::log(const QString &logStr)
{
    m_Out->append(currentTime() + logStr);
}

void LogOut::warning(const QString &wrnStr)
{
    QTextCursor cursor = m_Out->textCursor();
    cursor.movePosition(QTextCursor::EndOfLine);

    if (cursor.position() > 0) {
        cursor.insertText("\n");//换行
    }

    QTextCharFormat wrnFormat;
    wrnFormat.setForeground(QBrush(QColor("orange")));
    cursor.insertText(currentTime() + wrnStr,wrnFormat);

    QTextCharFormat defaultFormat;
    cursor.setCharFormat(defaultFormat);

    m_Out->setTextCursor(cursor);
}

void LogOut::sLog(const QString &logStr)
{
    this->log(logStr);
}

void LogOut::sWarning(const QString &wrnStr)
{
    this->warning(wrnStr);
}

QString LogOut::currentTime()
{
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString time = currentDateTime.toString("yyyy-MM-dd hh:mm:ss");
    return QString("[%1] ").arg(time);
}

