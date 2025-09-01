#include "jsonhandle.h"
#include <QDebug>
#include <QThread>
#include <QString>
JsonHandle::JsonHandle(const QJsonDocument &request, QTcpSocket *clientSocket,
                       SqlDataBase *database, QObject *parent )
    : QObject(parent)
    , m_request(request)
    , m_clientSocket(clientSocket)
    , m_database(database)
{
    // 确保socket在正确的线程
    if (m_clientSocket) {
        m_clientSocket->setParent(this);
    }
}

QString JsonHandle::currentTime()
{
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString time = currentDateTime.toString("yyyy-MM-dd hh:mm");
    return QString("[%1] ").arg(time);
}

void JsonHandle::query()
{
    // qDebug() << "Starting query processing on thread:" << QThread::currentThreadId();

    // //根据request内容调用不同查询函数，可带参数（参数只能从request中获取）
    // m_database->testFunction1();
    // //TODO
    // qDebug() << "SUCCESS!!!!" ;
    QJsonObject object = m_request.object();
    QString requestType = object.value("type").toString();

    qDebug() << requestType;

    QJsonObject res;

    qDebug() << "****query request*****\n" << m_request.toJson();

    emit log("processing " + requestType + " request");

    if(requestType == "login"){//患者登录
        QString username = object.value("user").toString();
        QString role = object.value("role").toString();
        QString password = object.value("pswd").toString();

        res = m_database->loginPatient(username, password, role);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "register"){//患者注册
        QJsonObject payload = object.value("payload").toObject();
        QString user = payload.value("user").toString();
        QString password = payload.value("passwd").toString();
        QString role = payload.value("role").toString();
        QString name = payload.value("name").toString();
        QString gender = payload.value("gender").toString();
        QString phone = payload.value("phone").toString();
        QString id_number = payload.value("id_number").toString();
        QString adress = payload.value("adress").toString();

        //QString role = object.value("role").toString();
        res = m_database->registerPatient(user, password, name, gender, phone, id_number, adress, role);
        //m_database->test();

        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        qDebug() << "***** register *****";
        qDebug() << QJsonDocument(res).toJson();

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "appt.create"){//创建预约
        qDebug() << "***** appt.create *****";

        QJsonObject payload = object.value("payload").toObject();
        qint64 user_id = object.value("user_id").toInt();
        qint64 doctor_id = payload.value("doctor_id").toInt();
        qint64 age = payload.value("age").toInt();
        QString startIso = payload.value("start_time").toString();
        QString height = payload.value("height").toString();
        QString weight = payload.value("weight").toString();
        QString sympptoms = payload.value("sympptoms").toString();//症状

        res = m_database->createAppointment(user_id, doctor_id, startIso, age, height, weight, sympptoms);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        // QJsonObject tmp = res.value("payload").toObject();
        // tmp.insert("symptom",sympptoms);
        // res["payload"] = tmp;

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "appt.list"){//查看预约
        qDebug() << "***** appt.list *****";

        qint64 patient_id = object.value("user_id").toInt();
        qDebug() << "start appt.list";
        res = m_database->listAppointments(patient_id);
        qDebug() << "appt.list done";
//        res["payload"] = arr;

//        if(arr.size() >= 0){
//            res["ok"] = true;
//        }
//        else{
//            res["ok"] = false;
//        }

        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "appt.cancel"){//取消预约
        qDebug() << "***** app.cancel *****";

        QJsonObject payload = object.value("payload").toObject();
        qint64 appt_id = payload.value("appt_id").toInt();

        res = m_database->cancelAppointment(appt_id);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "record"){//病例查看
        qDebug() << "***** record *****";

        qint64 user_id = object.value("user_id").toInt();
        qint64 appt_id = object.value("appt_id").toInt();

        res = m_database->listRecords(user_id/*patient_id*/, appt_id);//单个
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "record.list"){
        qDebug() << "***** record.list *****";

        qint64 user_id = object.value("user_id").toInt();

        res = m_database->listUserRecords(user_id);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "health.submit"){//健康评估
        qDebug() << "***** health.submit *****";

        qint64 user_id = object.value("user_id").toInt();
        QJsonObject payload = object.value("payload").toObject();
        QString time = payload.value("time").toString();
        QString risk_level = payload.value("risk_level").toString();
        QJsonArray advice = payload.value("advice").toArray();
        m_database->submitHealth(user_id, time, risk_level, advice);

        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "health.get"){
        qDebug() << "***** health.get *****";

        qint64 user_id = object.value("user_id").toInt();

        m_database->getHealth(user_id);

        res["type"] = object.value("type");
        res["seq"] = object.value("seq");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "userinfo"){//用户的个人信息
        qDebug() << "***** userinfo *****";

        qint64 user_id = object.value("user_id").toInt();

        res = m_database->getUserInfo(user_id);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "department_list"){//科室名称列表
        qDebug() << "***** department_list *****";

        res = m_database->listDepartments();
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "doctor_list"){//当前科室所有医生信息
        qDebug() << "***** doctoe_list *****";

        QString department_name = object.value("department_name").toString();

        res = m_database->listDoctorsByDepartment(department_name);
        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "change_user_info"){//修改用户信息
        qDebug() << "***** change user info *****";

        QJsonObject payload = object.value("payload").toObject();

        qint64 user_id = payload.value("user_id").toInt();
        QString name = payload.value("name").toString();
        QString phone = payload.value("phone").toString();
        QString id_number = payload.value("id_number").toString();
        QString adress = payload.value("adress").toString();

        res = m_database->changeUserInfo(user_id, name, phone, id_number, adress);

        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");

    }
    else if(requestType == "change_passwd"){
        qDebug() << "***** change passwd *****";

        QJsonObject payload = object.value("payload").toObject();

        qint64 user_id = payload.value("user_id").toInt();
        QString passwd = payload.value("passwd").toString();
        QString new_passwd = payload.value("new_passwd").toString();
        qDebug() << "0";
        res = m_database->changePassword(user_id, passwd, new_passwd);

        res["seq"] = object.value("seq");
        res["type"] = object.value("type");

        emit responseReady(m_clientSocket, QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "message" || requestType == "xiaoxi1"){//聊天
        qDebug() << "***** message_forward *****";

        if(requestType == "message"){
            QString text = object.value("content").toString();

            object.value("type") = "xiaoxi2";
            object.remove("content");
            object["name"] = "patient";
            object["include"] = text;
        }
        else{
            QString text = object.value("include").toString();
            object["time"] = currentTime();
            object.value("type") = "message.return";
            object.remove("include");
            object["sender"] = "doctor";
            object["content"] = text;
        }

        emit responseReady(nullptr, QJsonDocument(object));
        emit log("one request processed");
    }
    else if(requestType == "qingjia"){//请假
        qDebug() << "***** leave *****";

        qint64 doctor_id = object.value("doctor_id").toInt(6);
        QString typeOfLeave = object.value("leixing").toString();//类型，暂时没用上
        QString beginTime = object.value("begintime").toString();
        QString endTime = object.value("endtime").toString();
        QString reason = object.value("shiyou").toString();

        res["type"] = "qingjia";
        res = m_database->vacation(doctor_id, beginTime, endTime, reason);

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "xiaban"){//下班打卡
        qDebug() << "***** xiaban *****";

        qint64 user_id = object.value("user_id").toInt(6);
        QString time = object.value("time").toString();

        res = m_database->offWork(user_id, time);
        res["type"] = "xiaban";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "shangban"){//上班打卡
        qDebug() << "***** shangban *****";

        qint64 user_id = object.value("user_id").toInt(6);
        QString time = object.value("time").toString();

        res = m_database->goWork(user_id, time);
        res["type"] = "shangban";

        doctorOnline = true;

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "yizhu"){//开医嘱
        qDebug() << "***** yizhu *****";

        qint64 user_id = object.value("user_id").toInt(6);
        qint64 patient_id = object.value("patient_id").toInt();
        QString order = object.value("include").toString();

        res = m_database->doctorOrder(user_id, patient_id, order);
        res["type"] = "yizhu";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "everysecond"){//刷新数据
        qDebug() << "***** everysecond *****";

        qint64 doctor_id = object.value("user_id").toInt(1);

        res = m_database->getDoctorConsole(doctor_id);
        res["type"] = "everysecond";
        res["online"] = doctorOnline;

        if(!getBuffer){
            patientInfoBuffer = res.value("patient").toArray();
        }

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "yuyue"){//查看预约
        qDebug() << "***** yuyue *****";

        qint64 doctor_id = object.value("user_id").toInt(6);

        res = m_database->doctorAppoinment(doctor_id);
        res["type"] = "yuyue";

        res = patientInfoBuffer.at(bufferIndex).toObject();
        if(bufferIndex >= patientInfoBuffer.size()){
            bufferIndex = 0;
        }
        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "shuju"){//数据
        qDebug() << "***** shuju *****";

        qint64 user_id = object.value("user_id").toInt(6);
        QString dis = object.value("bing").toString();
        res = m_database->statisticDraw(user_id, dis);
        QJsonObject payload = res.value("payload").toObject();
        QJsonArray diseases = payload.value("diseases").toArray();
        payload = diseases[0].toObject();
        res["payload"] = payload;
        res["type"] = "shuju";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "kaoqin"){//考勤
        qDebug() << "***** kaoqin *****";

        qint64 user_id = object.value("user_id").toInt(6);
        qint64 limit = object.value("limit").toInt(30);

        res = m_database->checkWork(user_id, limit);
        res["type"] = "kaoqin";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "zhuce"){//医生注册
        qDebug() << "***** zhuce *****";

        QString name = object.value("name").toString();
        QString passwd = object.value("passwd").toString();

        res = m_database->registerDoctor(name, passwd);
        res["type"] = "zhuce";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "xiugai"){//医生修改
        qDebug() << "***** xiugai *****";

        qint64 user_id = object.value("user_id").toInt(6);
        QString doctor_number = object.value("gonghao").toString();
        QString identity = object.value("shenfen").toString();
        QString passwd = object.value("passwd").toString();

        res = m_database->doctorModify(user_id, doctor_number, identity, passwd);
        res["type"] = "xiugai";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else if(requestType == "denglu"){//医生登录
        qDebug() << "***** doctor signin *****";

        QString name = object.value("name").toString();
        QString passwd = object.value("passwd").toString();
        QString role = object.value("role").toString();

        res = m_database->doctorSignIn(name, passwd, role);
        res["type"] = "denglu";

        emit responseReady(m_clientSocket,QJsonDocument(res));
        emit log("one request processed");
    }
    else{
        emit log("receive one log request");
    }
    //emit responseReady(m_clientSocket, m_request);
    emit processingFinished(this);
}

