#include "sqldatabase.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSqlRecord>
#include <QVariant>

// =============== 构造/析构 ===============
SqlDataBase::SqlDataBase(QString dataPath, QObject *parent)
    : QObject(parent), dbPath(std::move(dataPath))
{
    dbMutex = new QMutex();

    if (QDir(dbPath).isRelative())
        dbPath = QCoreApplication::applicationDirPath() + "/" + dbPath;

    db = QSqlDatabase::addDatabase("QSQLITE");   // 需要 .pro: QT += sql
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qWarning() << "[DB] open failed:" << db.lastError().text()
                   << " path=" << QFileInfo(dbPath).absoluteFilePath();
    } else {
        qDebug() << "[DB] connected. exists?" << QFile::exists(dbPath)
                 << " path=" << QFileInfo(dbPath).absoluteFilePath();
    }
}

SqlDataBase::~SqlDataBase() {
    if (db.isOpen()) db.close();
    delete dbMutex;
}

// =============== 工具：中文性别映射 ===============
QString SqlDataBase::mapGender(const QString& genderCN) const {
    if (genderCN == "男") return "M";
    if (genderCN == "女") return "F";
    return QString(); // NULL
}

// =============== 登录 ===============
QJsonObject SqlDataBase::loginPatient(const QString& username, const QString& password, const QString& role)
{
    ////QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    q.prepare("SELECT user_id FROM users WHERE username=? AND password=? AND role=? AND status=1");
    q.addBindValue(username);
    q.addBindValue(password);
    q.addBindValue(role);
    qDebug() << "DB:path" << db.databaseName();
    qDebug() << "username:" << username;
    if (!q.exec()) {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }
    if (!q.next()) {
        return QJsonObject{{"ok", false}, {"error", "login failed"}};
    }
    const qint64 uid = q.value(0).toLongLong();
    return QJsonObject{{"ok", true}, {"payload", QJsonObject{{"user_id", uid}}}};
}

// =============== 注册（患者） ===============
QJsonObject SqlDataBase::registerPatient(const QString& username, const QString& password,
                                         const QString& realName, const QString& genderCN,
                                         const QString& phone, const QString& idCard,
                                         const QString& address, const QString& role)
{
    ////QMutexLocker lock(dbMutex);
    qDebug() << role << idCard;
    // 创建用户
    QSqlQuery q(db);

    q.prepare("INSERT INTO users(username,password,role,phone,id_card,gender,address,status) "
              "VALUES(?,?,?,?,?,?,?,1)");
    q.addBindValue(username);
    q.addBindValue(password);
    q.addBindValue(role);
    q.addBindValue(phone);
    q.addBindValue(idCard);
    q.addBindValue(mapGender(genderCN)); // M/F/NULL
    q.addBindValue(address);

    if (!q.exec()) {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }

    // 取 user_id
    QSqlQuery qid(db); qid.exec("SELECT last_insert_rowid()");
    qid.next(); const qint64 uid = qid.value(0).toLongLong();

    // 创建患者资料
    QSqlQuery qp(db);
    qp.prepare("INSERT INTO patients(user_id, full_name) VALUES(?,?)");
    qp.addBindValue(uid);
    qp.addBindValue(realName);
    if (!qp.exec()) {
        return QJsonObject{{"ok", false}, {"error", qp.lastError().text()}};
    }

    return QJsonObject{{"ok", true}, {"payload", QJsonObject{{"user_id", uid}}}};
}

// =============== 辅助：user_id -> patient_id ===============
qint64 SqlDataBase::patientIdFromUser(qint64 userId)
{
    //QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    q.prepare("SELECT patient_id FROM patients WHERE user_id=?");
    q.addBindValue(userId);
    if (!q.exec()){
        qWarning() << "patientIdFromUser exec failed:" << q.lastError().text();
        return -1;
    }
    if (!q.next()){
        qDebug() << "userId" << userId;
        qDebug() << "Couldn't find patient.";
        return 0;
    }
    return q.value(0).toLongLong();
}

// =============== 创建预约 ===============
QJsonObject SqlDataBase::createAppointment(qint64 user_id, qint64 doctorId, const QString& startIso,
                                           qint64 age, const QString& height, const QString& weight, const QString& sym)
{
    //QMutexLocker lock(dbMutex);

    // 1) 映射 patient_id
    const qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        return QJsonObject{{"ok", false}, {"error", "patient not found"}};
    }

    // 2) 插入预约（status 固定为 confirmed；顺带写入 symptom）
    QSqlQuery qi(db);
    qi.prepare("INSERT INTO appointments("
               "  patient_id, doctor_id, start_time, status, symptom"
               ") VALUES(?,?,?,?,?)");
    qi.addBindValue(patientId);
    qi.addBindValue(doctorId);
    qi.addBindValue(startIso);
    qi.addBindValue(QStringLiteral("pending"));
    qi.addBindValue(sym);

    if (!qi.exec()) {
        return QJsonObject{{"ok", false}, {"error", qi.lastError().text()}};
    }

    QSqlQuery qco(db);
    qco.prepare("UPDATE DoctorConsole SET appointment_number=COALESCE(appointment_number,0)+1 WHERE de_id=?");
    qco.addBindValue(1); // deId = 需要+1的那一行ID
    qco.exec();

    // 3) 可选更新患者体征（能解析就写，失败不阻断）
    bool okH=false, okW=false;
    const double h = height.trimmed().isEmpty() ? 0.0 : height.toDouble(&okH);
    const double w = weight.trimmed().isEmpty() ? 0.0 : weight.toDouble(&okW);

    if (okH || okW || age > 0) {
        QSqlQuery qu(db);
        qu.prepare("UPDATE patients "
                   "SET height_cm = COALESCE(?, height_cm), "
                   "    weight_kg = COALESCE(?, weight_kg), "
                   "    age       = COALESCE(?, age), "
                   "    updated_at = strftime('%s','now') "
                   "WHERE patient_id = ?");
        qu.addBindValue(okH ? QVariant(h)   : QVariant(QVariant::Double));     // 传 NULL 则不改
        qu.addBindValue(okW ? QVariant(w)   : QVariant(QVariant::Double));
        qu.addBindValue(age > 0 ? QVariant(age) : QVariant(QVariant::LongLong));
        qu.addBindValue(patientId);
        qu.exec(); // 忽略失败
    }


    // 4) 取 appt_id
    QSqlQuery qid(db);
    qid.exec("SELECT last_insert_rowid()");
    qid.next();
    const qint64 apptId = qid.value(0).toLongLong();

    // 5) 插入对应发票（未支付）
    QSqlQuery qfee(db);
    qfee.prepare("SELECT reg_fee FROM doctors WHERE doctor_id=?");
    qfee.addBindValue(doctorId);
    double regFee = 0.0;
    if (qfee.exec() && qfee.next()) {
        regFee = qfee.value(0).toDouble();
    }

    // 发票: 此时没有 encounter_id 和 prescription_id，可以先挂 NULL
    QSqlQuery qinv(db);
    qinv.prepare("INSERT INTO invoices(encounter_id, prescription_id, amount, paid) "
                 "VALUES(NULL, NULL, ?, 0)");
    qinv.addBindValue(regFee);
    qinv.exec(); // 忽略失败可行，但最好检查

    // 返回给前端
    return QJsonObject{
        {"ok", true},
        {"payload", QJsonObject{
                        {"appt_id", apptId},
                        {"fee", regFee},
                        {"symptom",sym}
                    }}
    };

}

// =============== 取消预约 ===============
QJsonObject SqlDataBase::cancelAppointment(qint64 apptId)
{
    //QMutexLocker lock(dbMutex);

    // 1) 更新预约状态
    QSqlQuery q(db);
    q.prepare("UPDATE appointments "
              "SET status='cancelled', updated_at=strftime('%s','now') "
              "WHERE appt_id=? AND status<>'cancelled'");
    q.addBindValue(apptId);

    if (!q.exec()) {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }
    if (q.numRowsAffected()==0){
        return QJsonObject{{"ok", false}, {"error", "not found or already cancelled"}};
    }

    // 2) 删除/作废对应发票（未支付的才处理）
    QSqlQuery qinv(db);
    qinv.prepare("DELETE FROM invoices "
                 "WHERE paid=0 "
                 "  AND encounter_id IS NULL "
                 "  AND invoice_id IN ( "
                 "    SELECT i.invoice_id "
                 "    FROM invoices i "
                 "    JOIN appointments a ON a.appt_id=? "
                 "  )");
    qinv.addBindValue(apptId);
    qinv.exec();

    return QJsonObject{{"ok", true}};
}

// =============== 查看预约（按 user_id 列出患者全部） ===============
QJsonObject SqlDataBase::listAppointments(qint64 user_id)
{
    QMutexLocker lock(dbMutex);

    QJsonObject out;
    out["ok"] = false;           // 先给默认值，成功后再置 true
    QJsonObject payload;
    QJsonArray appointments;

    // 1) user -> patient
    const qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        // patient 不存在：按约定返回空集与 0 计数
        payload["num_pending"]   = 0;
        payload["num_confirmed"] = 0;
        payload["num_cancelled"] = 0;
        payload["appointments"]  = QJsonArray{};
        out["payload"] = payload;
        return out;
    }

    // 2) 查询预约（注意 LEFT JOIN 科室，医生可能未分配科室）
    QSqlQuery q(db);
    q.prepare(
        "SELECT a.appt_id, d.full_name, dp.name, a.start_time, a.status "
        "FROM appointments a "
        "JOIN doctors d ON a.doctor_id = d.doctor_id "
        "LEFT JOIN departments dp ON d.department_id = dp.department_id "
        "WHERE a.patient_id = ? "
        "ORDER BY datetime(a.start_time) DESC"
    );
    q.addBindValue(patientId);

    if (!q.exec()) {
        // SQL 执行失败：返回空集与 0 计数（也可 out["error"]=q.lastError().text()）
        payload["num_pending"]   = 0;
        payload["num_confirmed"] = 0;
        payload["num_cancelled"] = 0;
        payload["appointments"]  = QJsonArray{};
        out["payload"] = payload;
        return out;
    }

    // 3) 组装结果 + 计数
    int num_pending = 0, num_confirmed = 0, num_cancelled = 0;

    while (q.next()) {
        const qint64 apptId     = q.value(0).toLongLong();
        const QString doctor    = q.value(1).toString();                 // d.full_name
        const QString deptName  = q.value(2).isNull() ? "" : q.value(2).toString(); // dp.name 可能为 NULL
        const QString startTime = q.value(3).toString();                 // a.start_time（建议存 ISO8601）
        const QString status    = q.value(4).toString();

        // 计数
        if (status == "pending")    ++num_pending;
        else if (status == "confirmed") ++num_confirmed;
        else if (status == "cancelled") ++num_cancelled;

        // 单条预约对象
        QJsonObject it;
        it["appt_id"]         = apptId;
        it["doctor_name"]     = doctor;
        it["department_name"] = deptName;   // 可能为空字符串
        it["time"]            = startTime;  // 例："2025-08-25 10:00" 或 "2025-08-25T10:00:00"
        it["status"]          = status;

        appointments.push_back(it);
    }

    // 4) 写入 payload
    payload["num_pending"]   = num_pending;
    payload["num_confirmed"] = num_confirmed;
    payload["num_cancelled"] = num_cancelled;
    payload["appointments"]  = appointments;

    out["ok"] = true;
    out["payload"] = payload;
    return out;
}


// =============== 病例查看（根据user_id, appt_id 返回单个病例） ===============
QJsonObject SqlDataBase::listRecords(qint64 user_id,qint64 appt_id)
{
    //QMutexLocker lock(dbMutex);
    QJsonObject o;

    const qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        return o; // 空对象表示未找到/无权限
    }

    QSqlQuery q(db);
    q.prepare(
        "SELECT e.appt_id, e.doctor_id, d.name, dep.name, "
        "       e.notes, mr.treatment, pr.notes, pr.prescription_id "
        "FROM encounters e "
        "LEFT JOIN medical_records  mr  ON mr.encounter_id = e.encounter_id "
        "LEFT JOIN prescriptions    pr  ON pr.encounter_id = e.encounter_id "
        "LEFT JOIN doctors          d   ON d.doctor_id = e.doctor_id "
        "LEFT JOIN departments      dep ON dep.department_id = d.department_id "
        "WHERE e.appt_id=? AND e.patient_id=?");
    q.addBindValue(appt_id);
    q.addBindValue(patientId);

    if (q.exec() && q.next()) {
        qint64 apptId      = q.value(0).toLongLong();
        qint64 doctorId    = q.value(1).toLongLong();
        QString doctorName = q.value(2).toString();
        QString deptName   = q.value(3).toString();
        QString encNotes   = q.value(4).toString();
        QString treat      = q.value(5).toString();
        QString rxNotes    = q.value(6).toString();
        qint64 rxId        = q.value(7).toLongLong();

        // 合并 prescription 文本
        QStringList prescParts;
        if (!encNotes.isEmpty()) prescParts << encNotes;
        if (!treat.isEmpty())    prescParts << treat;
        if (!rxNotes.isEmpty())  prescParts << rxNotes;

        // 追加药品清单
        if (rxId > 0) {
            QSqlQuery q2(db);
            q2.prepare(
                "SELECT m.name, m.spec, pi.instruction, pi.quantity "
                "FROM prescription_items pi "
                "JOIN medications m ON m.med_id = pi.med_id "
                "WHERE pi.prescription_id=?");
            q2.addBindValue(rxId);
            if (q2.exec()) {
                while (q2.next()) {
                    QString line = QString("%1 %2, 用法:%3, 数量:%4")
                                       .arg(q2.value(0).toString())  // 药名
                                       .arg(q2.value(1).toString())  // 规格
                                       .arg(q2.value(2).toString())  // 用法
                                       .arg(q2.value(3).toString()); // 数量
                    prescParts << line;
                }
            }
        }

        // 构造 JSON 对象
        o["appt_id"]         = apptId;
        o["doctor_id"]       = doctorId;
        o["doctor_name"]     = doctorName;
        o["department_name"] = deptName;
        o["prescription"]    = prescParts.join("；");
    }

    return o;
}

// =============== 健康评估提交（含 time；响应只返回 ok） ===============
QJsonObject SqlDataBase::submitHealth(qint64 user_id,
                                      const QString& time_text,      // "2025-08-25 10:00"
                                      const QString& risk_level,     // "高/中/低"
                                      const QJsonArray& advice_in)   // ["建议1","建议2",...]
{
    QMutexLocker lock(dbMutex);

    // 1) user -> patient
    const qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        return QJsonObject{{"ok", false}, {"error", "patient not found"}};
    }

    // 2) 规范化：advice → 字符串数组
    QJsonArray advice;
    for (const QJsonValue &v : advice_in) {
        advice.append(v.isString() ? v.toString() : v.toVariant().toString());
    }
    const QString adviceJson =
        QString::fromUtf8(QJsonDocument(advice).toJson(QJsonDocument::Compact));

    // 3) 写库
    // 优先尝试写入 created_at（若表里有该列，单位：unix 秒）
    // time_text 形如 "YYYY-MM-DD HH:MM" 或 "YYYY-MM-DD HH:MM:SS"
    QSqlQuery q(db);
    q.prepare("INSERT INTO health_assessments("
              "  patient_id, answers_json, score, risk, ai_advice, created_at"
              ") VALUES(?,?,?,?,?, strftime('%s', ?))");
    q.addBindValue(patientId);
    q.addBindValue(QStringLiteral("[]"));              // 没有问卷细项，空数组占位
    q.addBindValue(QVariant(QVariant::Double));        // NULL score
    q.addBindValue(risk_level);
    q.addBindValue(adviceJson);
    q.addBindValue(time_text);

    if (!q.exec()) {
        // 若失败，可能是没有 created_at 列；回退为不写 created_at 的版本
        QSqlQuery q2(db);
        q2.prepare("INSERT INTO health_assessments("
                   "  patient_id, answers_json, score, risk, ai_advice"
                   ") VALUES(?,?,?,?,?)");
        q2.addBindValue(patientId);
        q2.addBindValue(QStringLiteral("[]"));
        q2.addBindValue(QVariant(QVariant::Double));
        q2.addBindValue(risk_level);
        q2.addBindValue(adviceJson);

        if (!q2.exec()) {
            return QJsonObject{{"ok", false}, {"error", q2.lastError().text()}};
        }
    }

    // 4) 按新协议：只返回 ok（seq 由路由层补）
    return QJsonObject{{"ok", true}};
}




// =============== 获取健康评估（最新一条） ===============
// 请求:  type=health.get, {seq, user_id}
// 返回:  { ok, payload: { time, risk_level, advice[] } }
QJsonObject SqlDataBase::getHealth(qint64 user_id)
{
    QMutexLocker lock(dbMutex);

    // user -> patient
    const qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        return QJsonObject{{"ok", false}, {"error", "patient not found"}};
    }

    // 选最新一条：NULL 的时间排最后，再按 created_at 降序、rowid 降序兜底
    QSqlQuery q(db);
    q.prepare(
        "SELECT "
        "  risk, "
        "  ai_advice, "
        // 统一成 'YYYY-MM-DD HH:MM'，兼容 created_at 为整数(Unix秒)或文本
        "  CASE "
        "    WHEN typeof(created_at)='integer' THEN strftime('%Y-%m-%d %H:%M', created_at, 'unixepoch', 'localtime') "
        "    WHEN created_at IS NOT NULL      THEN strftime('%Y-%m-%d %H:%M', created_at, 'localtime') "
        "    ELSE strftime('%Y-%m-%d %H:%M', 'now', 'localtime') "
        "  END AS time_text "
        "FROM health_assessments "
        "WHERE patient_id=? "
        "ORDER BY (created_at IS NULL), created_at DESC, rowid DESC "
        "LIMIT 1"
    );
    q.addBindValue(patientId);

    if (!q.exec()) {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }

    // 没记录：返回空 payload
    if (!q.next()) {
        QJsonObject payload;
        payload["time"]       = QString();
        payload["risk_level"] = QString();
        payload["advice"]     = QJsonArray{};
        return QJsonObject{{"ok", true}, {"payload", payload}};
    }

    const QString risk       = q.value(0).toString();  // risk_level
    const QString adviceJson = q.value(1).toString();  // JSON 串
    const QString timeText   = q.value(2).toString();  // 格式化时间

    // 解析 ai_advice → 数组（健壮：不是数组时也能回传为数组）
    QJsonArray adviceArr;
    if (!adviceJson.trimmed().isEmpty()) {
        QJsonParseError perr;
        perr.error = QJsonParseError::NoError;
        QJsonDocument doc = QJsonDocument::fromJson(adviceJson.toUtf8(), &perr);

        if (perr.error == QJsonParseError::NoError && doc.isArray()) {
            adviceArr = doc.array();
        } else if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            // 误存成对象：转成一条字符串
            adviceArr.append(QString::fromUtf8(
                QJsonDocument(doc.object()).toJson(QJsonDocument::Compact)));
        } else {
            // 非法/非JSON：按原文本塞入数组
            adviceArr.append(adviceJson);
        }
    }

    // 只取前三条
    QJsonArray adviceTop3;
    for (int i = 0; i < adviceArr.size() && i < 3; ++i) {
        const QJsonValue v = adviceArr.at(i);
        adviceTop3.append(v.isString() ? v.toString() : v.toVariant().toString());
    }

    QJsonObject payload;
    payload["time"]       = timeText;
    payload["risk_level"] = risk;
    payload["advice"]     = adviceTop3;

    return QJsonObject{{"ok", true}, {"payload", payload}};
}




// =============== 发送消息 ===============
QJsonObject SqlDataBase::sendMessage(qint64 fromUserId, qint64 toUserId, const QString& content)
{
    //QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    q.prepare("INSERT INTO messages(from_user,to_user,content) VALUES(?,?,?)");
    q.addBindValue(fromUserId);
    q.addBindValue(toUserId);
    q.addBindValue(content);

    if (!q.exec()){
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }
    QSqlQuery qid(db); qid.exec("SELECT last_insert_rowid()"); qid.next();
    const qint64 msgId = qid.value(0).toLongLong();
    return QJsonObject{{"ok", true}, {"payload", QJsonObject{{"msg_id", msgId}}}};
}

// =============== 收件箱（可选 sinceUnix 起点） ===============
QJsonArray SqlDataBase::inbox(qint64 myUserId, qint64 sinceUnix)
{
    //QMutexLocker lock(dbMutex);
    QJsonArray arr;

    QSqlQuery q(db);
    if (sinceUnix > 0){
        q.prepare("SELECT msg_id, from_user, to_user, content, created_at "
                  "FROM messages WHERE to_user=? AND created_at>=? ORDER BY created_at DESC");
        q.addBindValue(myUserId);
        q.addBindValue(sinceUnix);
    } else {
        q.prepare("SELECT msg_id, from_user, to_user, content, created_at "
                  "FROM messages WHERE to_user=? ORDER BY created_at DESC");
        q.addBindValue(myUserId);
    }

    if (!q.exec()) return arr;

    while (q.next()){
        QJsonObject m;
        m["msg_id"]    = q.value(0).toLongLong();
        m["from_user"] = q.value(1).toLongLong();
        m["to_user"]   = q.value(2).toLongLong();
        m["content"]   = q.value(3).toString();
        m["created_at"]= q.value(4).toLongLong();
        arr.push_back(m);
    }
    return arr;
}

// =============== 科室列表 ===============
QJsonObject SqlDataBase::listDepartments()
{
    ////QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    QJsonArray items;

    if (q.exec("SELECT name FROM departments ORDER BY name ASC")) {
        while (q.next()){
            items.append(QJsonObject{{"department_name", q.value(0).toString()}});
        }
    } else {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }

    QJsonObject payload; payload["appointments"] = items; // 按你的 JSON 字段名
    return QJsonObject{{"ok", true}, {"payload", payload}};
}

// =============== 指定科室的医生列表 ===============
QJsonObject SqlDataBase::listDoctorsByDepartment(const QString& departmentName)
{
    ////QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    q.prepare(
        "SELECT d.doctor_id, d.full_name, d.bio, d.duty_start, d.reg_fee, d.daily_quota "
        "FROM doctors d "
        "JOIN departments dp ON dp.department_id = d.department_id "
        "WHERE dp.name=? "
        "ORDER BY d.full_name ASC");
    q.addBindValue(departmentName);

    if (!q.exec()){
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }

    QJsonArray items;
    while (q.next()){
        QJsonObject it;
        it["doctor_id"]   = q.value(0).toLongLong();
        it["full_name"]   = q.value(1).toString();
        it["bio"]         = q.value(2).toString();
        it["duty_start"]  = q.value(3).toString();
        it["reg_fee"]     = q.value(4).toDouble();
        it["daily_quota"] = q.value(5).toInt();
        items.push_back(it);
    }

    QJsonObject payload; payload["appointments"] = items; // 按你的 JSON 字段名
    return QJsonObject{{"ok", true}, {"payload", payload}};
}

void SqlDataBase::test()
{
    QSqlQuery q(db);

    // 1) 插入 user
    if (!q.prepare("INSERT INTO users(username, password, role, phone, id_card, gender, address) "
                   "VALUES(?,?,?,?,?,?,?)")) {
        qDebug() << "prepare failed:" << q.lastError().text();
        return;
    }
    q.addBindValue("testuser1");
    q.addBindValue("123456");
    q.addBindValue("patient");
    q.addBindValue("13800000001");
    q.addBindValue("110101199001010011");
    q.addBindValue("M");   // 男
    q.addBindValue("北京市");

    if (!q.exec()) {
        qDebug() << "insert user failed:" << q.lastError().text();
        return;
    }

    // 获取刚插入的 user_id
    q.exec("SELECT last_insert_rowid()");
    q.next();
    qint64 userId = q.value(0).toLongLong();
    qDebug() << "Inserted user_id =" << userId;

    // 2) 插入 patient
    QSqlQuery qp(db);
    qp.prepare("INSERT INTO patients(user_id, full_name, age, height_cm, weight_kg) "
               "VALUES(?,?,?,?,?)");
    qp.addBindValue(userId);
    qp.addBindValue("徐四");
    qp.addBindValue(30);
    qp.addBindValue(175);
    qp.addBindValue(70);

    if (!qp.exec()) {
        qDebug() << "insert patient failed:" << qp.lastError().text();
        return;
    }

    qDebug() << "Inserted patient for user_id =" << userId;

    // 3) 查询 users + patients 确认插入
    QSqlQuery qc(db);
    qc.exec("SELECT u.user_id, u.username, p.full_name, p.age, p.height_cm, p.weight_kg "
            "FROM users u "
            "LEFT JOIN patients p ON u.user_id=p.user_id "
            "ORDER BY u.user_id DESC LIMIT 5");

    qDebug() << "=== 最近的用户/患者 ===";
    while (qc.next()) {
        qDebug() << "user_id:" << qc.value(0).toLongLong()
        << "username:" << qc.value(1).toString()
        << "name:" << qc.value(2).toString()
        << "age:" << qc.value(3).toInt()
        << "height:" << qc.value(4).toInt()
        << "weight:" << qc.value(5).toInt();
    }
}

//获取患者个人信息
QJsonObject SqlDataBase::getUserInfo(qint64 userId)
{
    ////QMutexLocker lock(dbMutex);
    QSqlQuery q(db);
    q.prepare("SELECT username, role, "
              "       COALESCE(p.full_name,''), "
              "       COALESCE(u.gender,''), "
              "       COALESCE(u.phone,''), "
              "       COALESCE(u.id_card,''), "
              "       COALESCE(u.address,'') "
              "FROM users u "
              "LEFT JOIN patients p ON p.user_id = u.user_id "
              "WHERE u.user_id=?");
    q.addBindValue(userId);

    if (!q.exec()) {
        return QJsonObject{{"ok", false}, {"error", q.lastError().text()}};
    }
    if (!q.next()) {
        return QJsonObject{{"ok", false}, {"error", "user not found"}};
    }

    QString genderCode = q.value(3).toString();
    QString genderCN;
    if (genderCode == "M") genderCN = "男";
    else if (genderCode == "F") genderCN = "女";

    QJsonObject payload;
    payload["user"]      = q.value(0).toString();
    payload["role"]      = q.value(1).toString();
    payload["name"]      = q.value(2).toString();
    payload["gender"]    = genderCN;
    payload["phone"]     = q.value(4).toString();
    payload["id_number"] = q.value(5).toString();
    payload["adress"]    = q.value(6).toString();
    //lock.unlock();
    return QJsonObject{{"ok", true}, {"payload", payload}};
}

//根据user_id返回全部病例
QJsonObject SqlDataBase::listUserRecords(qint64 user_id)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject result;
    QJsonArray arr;

    // 根据 user_id 找 patient_id
    qint64 patientId = patientIdFromUser(user_id);
    if (patientId <= 0) {
        result["appointments"] = arr;
        return result;
    }

    QSqlQuery q(db);
    q.prepare(
        "SELECT a.appt_id, d.full_name, dp.name, a.start_time "
        "FROM appointments a "
        "JOIN doctors d    ON d.doctor_id = a.doctor_id "
        "JOIN departments dp ON dp.department_id = d.department_id "
        "WHERE a.patient_id = ? "
        "ORDER BY a.start_time DESC"
    );
    q.addBindValue(patientId);

    if (q.exec()) {
        while (q.next()) {
            QJsonObject o;
            o["appt_id"]        = q.value(0).toLongLong();
            o["doctor_name"]    = q.value(1).toString();
            o["department_name"]= q.value(2).toString();
            o["time"]           = q.value(3).toString();
            arr.append(o);
        }
    }
    QJsonObject payload;
    payload["appointment"] = arr;
    result["payload"] = payload;
    return result;
}



//修改患者个人信息
QJsonObject SqlDataBase::changeUserInfo(qint64 user_id, const QString& name,
                                        const QString& phone, const QString& id_number,
                                        const QString& address)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;
    out["ok"] = false;

    QSqlQuery q(db);
    q.prepare("UPDATE users SET "
              " phone = ?, id_card = ?, address = ? "
              "WHERE user_id = ?");
    q.addBindValue(phone);
    q.addBindValue(id_number);
    q.addBindValue(address);
    q.addBindValue(user_id);
    qint64 patientId = patientIdFromUser(user_id);
    q.prepare("UPDATE patients SET"
           "full_name = ? WHERE patient_id = ?");
    q.addBindValue(name);
    q.addBindValue(patientId);

    if (!q.exec()) {
        out["error"] = q.lastError().text();
        return out;
    }

    if (q.numRowsAffected() == 0) {
        out["error"] = "user not found";
        return out;
    }

    out["ok"] = true;
    return out;
}

//修改患者登录密码
QJsonObject SqlDataBase::changePassword(qint64 user_id,
                                        const QString& old_passwd,
                                        const QString& new_passwd)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;
    out["ok"] = false;

    // 1) 验证旧密码是否正确
    QSqlQuery q(db);
    q.prepare("SELECT password FROM users WHERE user_id=?");
    q.addBindValue(user_id);
    if (!q.exec() || !q.next()) {
        out["error"] = "user not found";
        return out;
    }
    QString current = q.value(0).toString();
    if (current != old_passwd) {
        out["error"] = "old password incorrect";
        return out;
    }

    // 2) 更新新密码
    QSqlQuery u(db);
    u.prepare("UPDATE users SET password=? WHERE user_id=?");
    u.addBindValue(new_passwd);
    u.addBindValue(user_id);
    if (!u.exec()) {
        out["error"] = u.lastError().text();
        return out;
    }

    out["ok"] = true;
    return out;
}

//========================================医生端==============================


//返回医生的仪表盘
QJsonObject SqlDataBase::getDoctorConsole(qint64 doctor_id)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject res;

    // ========== 1. 查询 DoctorConsole ==========
    QJsonArray daiban;
    {
        QSqlQuery q(db);
        q.exec("SELECT appointment_number, encounter_number, message_number, prescription_number "
                  "FROM DoctorConsole WHERE de_id=1");
        if (q.next()) {
            daiban.append(q.value(0).toInt());
            daiban.append(q.value(1).toInt());
            daiban.append(q.value(2).toInt());
            daiban.append(q.value(3).toInt());
        }
    }
    res["daiban"] = daiban;

    // ========== 2. 查询今日预约患者（未处理，最多4人） ==========
    QJsonArray patients;
    {
        qDebug() << "doctoc_id:" << doctor_id ;
        QSqlQuery q(db);
        q.prepare(
            "SELECT p.full_name, p.age, p.height_cm, p.weight_kg, a.symptom "
            "FROM appointments a "
            "JOIN patients p ON p.patient_id = a.patient_id "
            "WHERE a.doctor_id=? "
            "LIMIT 4");
        q.addBindValue(doctor_id);

        if (q.exec()) {
            while (q.next()) {
                QJsonObject o;
                o["name"]    = q.value(0).toString();
                o["age"]     = q.value(1).toInt();
                o["height"]  = q.value(2).toDouble();
                o["weight"]  = q.value(3).toDouble();
                o["symptom"] = q.value(4).toString();
                patients.append(o);
            }
        }
    }
    res["patient"] = patients;

    return res;
}

//返回一个患者的情况
QJsonObject SqlDataBase::doctorAppoinment(qint64 doctor_id)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject o;

    QSqlQuery q(db);
    q.exec(
        "SELECT p.full_name, p.age, p.height_cm, p.weight_kg, a.symptom, p.patient_id "
        "FROM appointments a "
        "JOIN patients p ON p.patient_id = a.patient_id "
        "WHERE a.doctor_id=1 "
        "ORDER BY a.start_time ASC LIMIT 1"
    );
    //q.addBindValue(doctor_id);

    if (q.next()) {
        o["name"]       = q.value(0).toString();
        o["age"]        = q.value(1).toInt();
        o["height"]     = q.value(2).toDouble();
        o["weight"]     = q.value(3).toDouble();
        o["symptom"]    = q.value(4).toString();
        o["patient_id"] = q.value(5).toInt();
    }

    return o;
}

// 写医嘱：根据 doctor 的 user_id 与 patient_id，给最近一次预约写入 encounters.notes
QJsonObject SqlDataBase::doctorOrder(qint64 doctor_user_id, qint64 patient_id, const QString& orderText)
{                                                       //6       0
    ////QMutexLocker lock(dbMutex);
    QJsonObject reply; // 只需 "type":"yizhu","ok":true/false 由路由层补 type

    // 1) user_id -> doctor_id
    qint64 doctor_id = 1;
//    {
//        QSqlQuery q(db);
//        q.prepare("SELECT doctor_id FROM doctors WHERE user_id=?");
//        q.addBindValue(doctor_user_id);
//        if (q.exec() && q.next()) doctor_id = q.value(0).toLongLong();
//    }
    if (doctor_id <= 0) {
        reply["ok"] = false;
        reply["error"] = "doctor not found";
        return reply;
    }

    // 2) 最近一条预约
    qint64 appt_id = -1;
    {
        QSqlQuery q(db);
        q.prepare(
            "SELECT appt_id "
            "FROM appointments "
            "WHERE patient_id=? AND doctor_id=? AND status IN ('pending','confirmed') "
            "ORDER BY datetime(start_time) DESC LIMIT 1");
        q.addBindValue(patient_id);
        q.addBindValue(doctor_id);
        if (q.exec() && q.next()) appt_id = q.value(0).toLongLong();
    }
    if (appt_id <= 0) {
        reply["ok"] = false;
        reply["error"] = "no appointment for this doctor/patient";
        return reply;
    }

    // 3) 事务：无则插，有则改
    db.transaction();
    bool ok = true;

    // 插入（若不存在）
    {
        QSqlQuery ins(db);
        ins.prepare(
            "INSERT INTO encounters (appt_id, patient_id, doctor_id, notes) "
            "SELECT ?, ?, ?, ? "
            "WHERE NOT EXISTS (SELECT 1 FROM encounters WHERE appt_id=?)");
        ins.addBindValue(appt_id);
        ins.addBindValue(patient_id);
        ins.addBindValue(doctor_id);
        ins.addBindValue(orderText);
        ins.addBindValue(appt_id);
        ok = ins.exec() && ok;
    }

    // 更新（覆盖 notes；若要“追加”可用 COALESCE 拼接方案）
    {
        QSqlQuery upd(db);
        upd.prepare(
            "UPDATE encounters "
            "SET notes = ?, visit_time = datetime('now') "
            "WHERE appt_id = ?");
        upd.addBindValue(orderText);
        upd.addBindValue(appt_id);
        ok = upd.exec() && ok;
    }

    if (ok) {
        db.commit();
        reply["ok"] = true;
    } else {
        db.rollback();
        reply["ok"] = false;
        reply["error"] = "db error";
    }

    return reply;
}



//医生发送消息
QJsonObject SqlDataBase::doctorSendMessage(qint64 doctor_user_id, qint64 patient_id, const QString& content)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    // 1) patient_id → user_id
    qint64 patient_user_id = -1;
    {
        QSqlQuery q(db);
        q.prepare("SELECT user_id FROM patients WHERE patient_id=?");
        q.addBindValue(patient_id);
        if (q.exec() && q.next()) {
            patient_user_id = q.value(0).toLongLong();
        }
    }
    if (patient_user_id <= 0) {
        out["ok"] = false;
        out["error"] = "patient not found";
        return out;
    }

    // 2) 插入消息
    QSqlQuery ins(db);
    ins.prepare(
        "INSERT INTO messages (from_user, to_user, content, created_at) "
        "VALUES (?, ?, ?, strftime('%s','now'))"
    );
    ins.addBindValue(doctor_user_id);
    ins.addBindValue(patient_user_id);
    ins.addBindValue(content);

    if (ins.exec()) {
        out["ok"] = true;
    } else {
        out["ok"] = false;
        out["error"] = ins.lastError().text();
    }

    return out;
}



//医生接受消息
QJsonObject SqlDataBase::doctorInBox(qint64 doctor_user_id, const QString& content)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO messages (from_user_id, to_user_id, content, is_read, created_at) "
        "VALUES (0, ?, ?, 0, strftime('%s','now'))"
    );
    q.addBindValue(doctor_user_id);
    q.addBindValue(content);

    if (q.exec()) {
        out["ok"] = true;
    } else {
        out["ok"] = false;
        out["error"] = q.lastError().text();
    }
    return out;
}

//医生注册
QJsonObject SqlDataBase::registerDoctor(const QString& name, const QString& passwd)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    QSqlQuery q(db);
    q.prepare("INSERT INTO users (username, \"password\", role) VALUES (?, ?, ?)");
    q.addBindValue(name);
    q.addBindValue(passwd);
    q.addBindValue("doctor");

    if (q.exec()) {
        qint64 newId = q.lastInsertId().toLongLong();
        out["ok"] = true;
        out["user_id"] = newId;
    } else {
        out["ok"] = false;
        out["error"] = q.lastError().text();
    }

    return out;
}


//医生修改密码
// 医生修改密码（兼容旧签名：gonghao 无视）
QJsonObject SqlDataBase::doctorModify(qint64 user_id,
                                      const QString& /*gonghao_ignored*/,
                                      const QString& shenfen,   // = id_card，可为空则不改
                                      const QString& passwd)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    if (user_id <= 0) {
        out["ok"] = false;
        out["error"] = "invalid user_id";
        return out;
    }
    if (passwd.trimmed().isEmpty()) {
        out["ok"] = false;
        out["error"] = "empty password";
        return out;
    }

    if (!db.transaction()) {
        out["ok"] = false;
        out["error"] = "begin transaction failed";
        return out;
    }

    bool okAll = true;

    // 按是否需要改 id_card 构造 SQL
    if (shenfen.trimmed().isEmpty()) {
        // 仅更新密码
        QSqlQuery q(db);
        q.prepare(R"SQL(
            UPDATE users
               SET password = ?, updated_at = strftime('%s','now')
             WHERE user_id = ?
        )SQL");
        q.addBindValue(passwd);
        q.addBindValue(user_id);
        okAll = q.exec();
        if (!okAll || q.numRowsAffected() == 0) {
            db.rollback();
            out["ok"] = false;
            out["error"] = okAll ? "user not found" : q.lastError().text();
            return out;
        }
    } else {
        // 同时更新 id_card（注意 UNIQUE 约束可能报错）
        QSqlQuery q(db);
        q.prepare(R"SQL(
            UPDATE users
               SET id_card = ?, password = ?, updated_at = strftime('%s','now')
             WHERE user_id = ?
        )SQL");
        q.addBindValue(shenfen);
        q.addBindValue(passwd);
        q.addBindValue(user_id);
        okAll = q.exec();
        if (!okAll || q.numRowsAffected() == 0) {
            db.rollback();
            out["ok"] = false;
            out["error"] = okAll ? "user not found or id_card conflict" : q.lastError().text();
            return out;
        }
    }

    if (!db.commit()) {
        out["ok"] = false;
        out["error"] = "commit failed";
        return out;
    }

    out["ok"] = true;
    out["user_id"] = QString::number(user_id);
    return out;
}


// goWork: 医生上班打卡（由 user_id 定位 doctor，再写 attendance ）
// timeStr 可为空；为空则使用当前本地时间
QJsonObject SqlDataBase::goWork(qint64 user_id, const QString& timeStr)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    // 1) user_id -> doctor_id
    qint64 doctor_id = -1;
    {
        QSqlQuery q(db);
        q.prepare("SELECT doctor_id FROM doctors WHERE user_id=?");
        q.addBindValue(user_id);
        if (q.exec() && q.next())
            doctor_id = q.value(0).toLongLong();
    }
    if (doctor_id <= 0) {
        out["ok"] = false;
        out["error"] = "doctor not found";
        return out;
    }

    // 2) 计算 day / checkin_ts
    const bool hasTime = !timeStr.trimmed().isEmpty();

    // 3) 写考勤（插入或覆盖同一天）
    QSqlQuery q(db);
    if (hasTime) {
        // 传入时间
        q.prepare(
            "INSERT INTO attendance (doctor_id, day, check_in) "
            "VALUES (?, date(?), ?) "
            "ON CONFLICT(doctor_id, day) DO UPDATE SET "
            "check_in=excluded.check_in, created_at=strftime('%s','now')"
        );
        q.addBindValue(doctor_id);
        q.addBindValue(timeStr);   // day = date(timeStr)
        q.addBindValue(timeStr);   // check_in = timeStr
    } else {
        // 不传时间 -> 用当前本地时间
        q.prepare(
            "INSERT INTO attendance (doctor_id, day, check_in) "
            "VALUES (?, date('now','localtime'), datetime('now','localtime')) "
            "ON CONFLICT(doctor_id, day) DO UPDATE SET "
            "check_in=excluded.check_in, created_at=strftime('%s','now')"
        );
        q.addBindValue(doctor_id);
    }

    if (!q.exec()) {
        out["ok"] = false;
        out["error"] = q.lastError().text();
        return out;
    }

    out["ok"] = true;
    return out;
}

// 下班打卡：根据 user_id 找到 doctor_id，写入当天的 check_out
QJsonObject SqlDataBase::offWork(qint64 user_id, const QString& timeStr /*可为空*/)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    // 1) user_id -> doctor_id
    qint64 doctor_id = -1;
    {
        QSqlQuery q(db);
        q.prepare("SELECT doctor_id FROM doctors WHERE user_id=?");
        q.addBindValue(user_id);
        if (q.exec() && q.next())
            doctor_id = q.value(0).toLongLong();
    }
    if (doctor_id <= 0) {
        out["ok"] = false;
        out["error"] = "doctor not found";
        return out;
    }

    // 2) 写考勤（传 time 用传入时间；否则用本地当前时间）
    QSqlQuery q(db);
    if (timeStr.trimmed().isEmpty()) {
        qDebug() << "hello1" ;
        q.prepare(
            "INSERT INTO attendance (doctor_id, day, check_out) "
            "VALUES (?, date('now','localtime'), datetime('now','localtime')) "
            "ON CONFLICT(doctor_id, day) DO UPDATE SET "
            "check_out=excluded.check_out, created_at=strftime('%s','now')"
        );
        q.addBindValue(doctor_id);
    } else {
        qDebug() << "hello2" ;
        q.prepare(
            "INSERT INTO attendance (doctor_id, day, check_out) "
            "VALUES (?, date(?), ?) "
            "ON CONFLICT(doctor_id, day) "
            " DO UPDATE SET  check_out=excluded.check_out, created_at=strftime('%s','now')"
        );
        q.addBindValue(doctor_id);
        q.addBindValue(timeStr);  // day = date(timeStr)
        q.addBindValue(timeStr);  // check_out = timeStr
    }

    if (!q.exec()) {
        out["ok"] = false;
        out["error"] = q.lastError().text();
        return out;
    }

    out["ok"] = true;
    return out;
}

//医生请假
QJsonObject SqlDataBase::vacation(qint64 doctor_id,
                                  const QString& start_date,
                                  const QString& end_date,
                                  const QString& reason)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    if (doctor_id <= 0 || start_date.isEmpty() || end_date.isEmpty()) {
        out["ok"] = false;
        out["error"] = "invalid params";
        return out;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO leaves (doctor_id, type, start_date, end_date, reason) "
              "VALUES (?, '因私', ?, ?, ?)");
    q.addBindValue(doctor_id);
    q.addBindValue(start_date);
    q.addBindValue(end_date);
    q.addBindValue(reason);

    if (q.exec()) {
        out["ok"] = true;
    } else {
        out["ok"] = false;
        out["error"] = q.lastError().text();
    }
    return out;
}


//医生考勤查询
QJsonObject SqlDataBase::checkWork(qint64 user_id, int limitDays /*=30*/)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;
    QJsonArray dates, ins, outs, stats;

    // 1) user_id -> doctor_id
    qint64 doctor_id = -1;
    {
        QSqlQuery q(db);
        q.prepare("SELECT doctor_id FROM doctors WHERE user_id=?");
        q.addBindValue(user_id);
        if (q.exec() && q.next()) doctor_id = q.value(0).toLongLong();
    }
    if (doctor_id <= 0) {
        // 返回空数组（按你们风格：面向结果，不抛错）
        out["date"] = dates;
        out["checkInTime"] = ins;
        out["checkOutTime"] = outs;
        out["status"] = stats;
        return out;
    }

    if (limitDays <= 0) limitDays = 30;

    // 2) 查询 attendance
    QSqlQuery q(db);
    q.prepare(
        "SELECT day, check_in, check_out, status "
        "FROM attendance "
        "WHERE doctor_id=? "
        "ORDER BY day DESC "
        "LIMIT ?"
    );
    q.addBindValue(doctor_id);
    q.addBindValue(limitDays);

    if (q.exec()) {
        while (q.next()) {
            const QString day  = q.value(0).toString();

            auto pickTime = [](const QVariant &v)->QString {
                QString s = v.toString().trimmed();
                if (s.isEmpty()) return "";
                // 如果是 "YYYY-MM-DD HH:MM:SS" 取 "HH:MM"
                int sp = s.indexOf(' ');
                if (sp > 0 && s.size() >= sp + 5) return s.mid(sp+1, 5);
                // 如果原本就是 "HH:MM" 或 "HH:MM:SS"，取前5位
                if (s.size() >= 5) return s.left(5);
                return s;
            };

            const QString cin  = pickTime(q.value(1));
            const QString cout = pickTime(q.value(2));
            const QString st   = q.value(3).toString();

            dates.append(day);
            ins.append(cin);
            outs.append(cout);
            stats.append(st);
        }
    }

    out["date"]         = dates;
    out["checkInTime"]  = ins;
    out["checkOutTime"] = outs;
    out["status"]       = stats;
    return out;
}

//医生登录
QJsonObject SqlDataBase::doctorSignIn(const QString& name,
                                      const QString& passwd,
                                      const QString& role /*"doctor"*/)
{
    ////QMutexLocker lock(dbMutex);
    QJsonObject out;

    QSqlQuery q(db);
    q.prepare(
        "SELECT user_id FROM users "
        "WHERE username=? AND password=? AND role=? AND status=1 LIMIT 1"
    );
    q.addBindValue(name);
    q.addBindValue(passwd);
    q.addBindValue(role);

    if (!q.exec())
    {
        qDebug() << "Exec Fail";
        out["ok"] = false;
        out["error"] = "invalid credentials";
        return out;
    }
    if (!q.next())
    {
        qDebug() << "Couldn't find";
        out["ok"] = false;
        out["error"] = "invalid credentials";
        return out;
    }
    out["ok"] = true;
    out["user_id"] = q.value(0).toLongLong();
    return out;
}


//建造图
// ---- 显式类型版本的排序工具（C++11可用） ----
static QJsonArray sortByBuckets(const QJsonArray& arr,
                                const QString& key,
                                const QStringList& order)
{
    QVector<QPair<int, QJsonObject> > vec;
    vec.reserve(arr.size());

    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr.at(i).toObject();
        int idx = order.indexOf(obj.value(key).toString());
        if (idx < 0) idx = INT_MAX; // 未知桶放最后
        vec.push_back(qMakePair(idx, obj));
    }

    std::sort(vec.begin(), vec.end(),
              [](const QPair<int, QJsonObject>& a,
                 const QPair<int, QJsonObject>& b) {
                  return a.first < b.first;
              });

    QJsonArray out;
    for (int i = 0; i < vec.size(); ++i)
        out.append(vec[i].second);
    return out;
}

// 建造图
//QJsonObject SqlDataBase::statisticDraw(qint64 seq)
//{
//    QMutexLocker _locker(dbMutex); // 如果 dbMutex 不是指针，请改为 &dbMutex
//    QJsonObject out;

//    out["ok"]  = false;
//    out["seq"] = QString::number(seq); // 想要数值可改为: out["seq"] = static_cast<double>(seq);

//    // 1) 取所有病种
//    QSqlQuery qList(db);
//    if (!qList.exec("SELECT DISTINCT disease FROM disease_stats ORDER BY disease ASC")) {
//        out["error"] = QString("list diseases failed: %1").arg(qList.lastError().text());
//        return out;
//    }

//    // 四类分桶的有序列表（用于排序）
//    const QStringList ageBuckets = (QStringList() << "0-18" << "19-40" << "41-65" << "65+");
//    const QStringList wgtBuckets = (QStringList() << "<50kg" << "50-70kg" << "70-90kg" << "90kg+");
//    const QStringList hgtBuckets = (QStringList() << "<160cm" << "160-170cm" << "170-180cm" << "180cm+");

//    // 2) 为每个病种聚合四组
//    QJsonArray diseasesArr;

//    while (qList.next()) {
//        const QString disease = qList.value(0).toString();

//        // 2.1 年龄分布
//        QSqlQuery qAge(db);
//        qAge.prepare(
//            "SELECT age_group, SUM(count) "
//            "FROM disease_stats "
//            "WHERE disease=? AND age_group<>'ALL' "
//            "GROUP BY age_group"
//        );
//        qAge.addBindValue(disease);
//        QJsonArray ageStats;
//        if (!qAge.exec()) {
//            out["error"] = QString("age stats failed(%1): %2").arg(disease, qAge.lastError().text());
//            return out;
//        }
//        while (qAge.next()) {
//            QJsonObject o;
//            o["age"]   = qAge.value(0).toString();
//            o["count"] = qAge.value(1).toInt();
//            ageStats.push_back(o);
//        }
//        ageStats = sortByBuckets(ageStats, "age", ageBuckets);

//        // 2.2 体重分布
//        QSqlQuery qW(db);
//        qW.prepare(
//            "SELECT weight_group, SUM(count) "
//            "FROM disease_stats "
//            "WHERE disease=? AND weight_group<>'ALL' "
//            "GROUP BY weight_group"
//        );
//        qW.addBindValue(disease);
//        QJsonArray weightStats;
//        if (!qW.exec()) {
//            out["error"] = QString("weight stats failed(%1): %2").arg(disease, qW.lastError().text());
//            return out;
//        }
//        while (qW.next()) {
//            QJsonObject o;
//            o["weight"] = qW.value(0).toString();
//            o["count"]  = qW.value(1).toInt();
//            weightStats.push_back(o);
//        }
//        weightStats = sortByBuckets(weightStats, "weight", wgtBuckets);

//        // 2.3 身高分布
//        QSqlQuery qH(db);
//        qH.prepare(
//            "SELECT height_group, SUM(count) "
//            "FROM disease_stats "
//            "WHERE disease=? AND height_group<>'ALL' "
//            "GROUP BY height_group"
//        );
//        qH.addBindValue(disease);
//        QJsonArray heightStats;
//        if (!qH.exec()) {
//            out["error"] = QString("height stats failed(%1): %2").arg(disease, qH.lastError().text());
//            return out;
//        }
//        while (qH.next()) {
//            QJsonObject o;
//            o["height"] = qH.value(0).toString();
//            o["count"]  = qH.value(1).toInt();
//            heightStats.push_back(o);
//        }
//        heightStats = sortByBuckets(heightStats, "height", hgtBuckets);

//        // 2.4 年份分布
//        QSqlQuery qY(db);
//        qY.prepare(
//            "SELECT year, SUM(count) "
//            "FROM disease_stats "
//            "WHERE disease=? AND age_group='ALL' AND weight_group='ALL' AND height_group='ALL' "
//            "GROUP BY year "
//            "ORDER BY year"
//        );
//        qY.addBindValue(disease);
//        QJsonArray yearStats;
//        if (!qY.exec()) {
//            out["error"] = QString("year stats failed(%1): %2").arg(disease, qY.lastError().text());
//            return out;
//        }
//        while (qY.next()) {
//            QJsonObject o;
//            o["year"]  = qY.value(0).toInt();
//            o["count"] = qY.value(1).toInt();
//            yearStats.push_back(o);
//        }

//        // 组装
//        QJsonObject dObj;
//        dObj["disease"]      = disease;
//        dObj["age_stats"]    = ageStats;
//        dObj["weight_stats"] = weightStats;
//        dObj["height_stats"] = heightStats;
//        dObj["year_stats"]   = yearStats;

//        diseasesArr.push_back(dObj);
//    }

//    QJsonObject payload;
//    payload["diseases"] = diseasesArr;

//    out["ok"]      = true;
//    out["payload"] = payload;
//    return out;
//}


// =============== 统计绘图（bing 仅允许四种：冠心病/青光眼/高血压/糖尿病） ===============
QJsonObject SqlDataBase::statisticDraw(qint64 seq, QString bing)
{
    QMutexLocker _locker(dbMutex);
    QJsonObject out;
    out["ok"]  = false;
    out["seq"] = QString::number(seq);

    // 允许的病种集合
    const QSet<QString> allowed = QSet<QString>{"冠心病","青光眼","高血压","糖尿病"};

    const QString chosen = bing.trimmed();
    if (chosen.isEmpty() || !allowed.contains(chosen)) {
        // 不传或不在四种里 —— 返回空结果或提示
        QJsonObject payload;
        payload["diseases"] = QJsonArray{};
        out["ok"] = true;
        out["payload"] = payload;
        if (chosen.isEmpty()) {
            out["note"] = "未指定 bing，已返回空结果（仅支持：冠心病/青光眼/高血压/糖尿病）。";
        } else {
            out["note"] = QString("不支持的病种：%1（仅支持：冠心病/青光眼/高血压/糖尿病）").arg(chosen);
        }
        return out;
    }

    // 桶顺序
    const QStringList ageBuckets = {"0-18","19-40","41-65","65+"};
    const QStringList wgtBuckets = {"<50kg","50-70kg","70-90kg","90kg+"};
    const QStringList hgtBuckets = {"<160cm","160-170cm","170-180cm","180cm+"};

    // 本地排序工具：按 buckets 顺序重排
    auto sortByBucketsLocal = [](const QJsonArray& arr, const QString& key, const QStringList& buckets) -> QJsonArray {
        struct Item { int idx; QJsonObject obj; };
        QVector<Item> v; v.reserve(arr.size());
        for (const auto& it : arr) {
            QJsonObject o = it.toObject();
            const int idx = qMax(0, buckets.indexOf(o.value(key).toString()));
            v.push_back({ idx < 0 ? 9999 : idx, o });
        }
        std::sort(v.begin(), v.end(), [](const Item& a, const Item& b){ return a.idx < b.idx; });
        QJsonArray out;
        for (const auto& it : v) out.push_back(it.obj);
        return out;
    };

    // 只查 chosen 这一种
    QJsonArray diseasesArr;

    // 2.1 年龄分布
    QSqlQuery qAge(db);
    qAge.prepare(
        "SELECT age_group, SUM(count) "
        "FROM disease_stats "
        "WHERE disease=? AND age_group<>'ALL' "
        "GROUP BY age_group"
    );
    qAge.addBindValue(chosen);
    if (!qAge.exec()) {
        out["error"] = QString("age stats failed(%1): %2").arg(chosen, qAge.lastError().text());
        return out;
    }
    QJsonArray ageStats;
    while (qAge.next()) {
        ageStats.push_back(QJsonObject{
            {"age",   qAge.value(0).toString()},
            {"count", qAge.value(1).toInt()}
        });
    }
    ageStats = sortByBucketsLocal(ageStats, "age", ageBuckets);

    // 2.2 体重分布
    QSqlQuery qW(db);
    qW.prepare(
        "SELECT weight_group, SUM(count) "
        "FROM disease_stats "
        "WHERE disease=? AND weight_group<>'ALL' "
        "GROUP BY weight_group"
    );
    qW.addBindValue(chosen);
    if (!qW.exec()) {
        out["error"] = QString("weight stats failed(%1): %2").arg(chosen, qW.lastError().text());
        return out;
    }
    QJsonArray weightStats;
    while (qW.next()) {
        weightStats.push_back(QJsonObject{
            {"weight", qW.value(0).toString()},
            {"count",  qW.value(1).toInt()}
        });
    }
    weightStats = sortByBucketsLocal(weightStats, "weight", wgtBuckets);

    // 2.3 身高分布
    QSqlQuery qH(db);
    qH.prepare(
        "SELECT height_group, SUM(count) "
        "FROM disease_stats "
        "WHERE disease=? AND height_group<>'ALL' "
        "GROUP BY height_group"
    );
    qH.addBindValue(chosen);
    if (!qH.exec()) {
        out["error"] = QString("height stats failed(%1): %2").arg(chosen, qH.lastError().text());
        return out;
    }
    QJsonArray heightStats;
    while (qH.next()) {
        heightStats.push_back(QJsonObject{
            {"height", qH.value(0).toString()},
            {"count",  qH.value(1).toInt()}
        });
    }
    heightStats = sortByBucketsLocal(heightStats, "height", hgtBuckets);

    // 2.4 年份分布（ALL 汇总）
    QSqlQuery qY(db);
    qY.prepare(
        "SELECT year, SUM(count) "
        "FROM disease_stats "
        "WHERE disease=? AND age_group='ALL' AND weight_group='ALL' AND height_group='ALL' "
        "GROUP BY year ORDER BY year"
    );
    qY.addBindValue(chosen);
    if (!qY.exec()) {
        out["error"] = QString("year stats failed(%1): %2").arg(chosen, qY.lastError().text());
        return out;
    }
    QJsonArray yearStats;
    while (qY.next()) {
        yearStats.push_back(QJsonObject{
            {"year",  qY.value(0).toInt()},
            {"count", qY.value(1).toInt()}
        });
    }

    // 组装单一疾病
    QJsonObject dObj;
    dObj["disease"]      = chosen;
    dObj["age_stats"]    = ageStats;
    dObj["weight_stats"] = weightStats;
    dObj["height_stats"] = heightStats;
    dObj["year_stats"]   = yearStats;

    diseasesArr.push_back(dObj);

    // 输出
    QJsonObject payload;
    payload["diseases"] = diseasesArr;
    out["ok"] = true;
    out["payload"] = payload;
    return out;
}
