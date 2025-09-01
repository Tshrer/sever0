#ifndef SQLDATABASE_H
#define SQLDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>     // 新增
#include <QJsonObject>      // 新增
#include <QJsonArray>       // 新增
#include <QJsonDocument>    // 提交健康评估时要把 answers 序列化为 json
class SqlDataBase : public QObject
{
    Q_OBJECT
public:
    explicit SqlDataBase(QString dataPath,QObject *parent = nullptr);
    ~SqlDataBase();

    // —— 登录（患者端通用登录，返回 user_id/role；token 请在上层生成）
    QJsonObject loginPatient(const QString& username, const QString& password,const QString& role);

    // —— 注册（患者端）
    QJsonObject registerPatient(const QString& username, const QString& password,
                                const QString& realName, const QString& genderCN,
                                const QString& phone, const QString& idCard,
                                const QString& address,const QString& role);

    // —— 预约：创建 / 取消 / 查询列表
    QJsonObject createAppointment(qint64 user_id, qint64 doctorId, const QString& startIso,
                                  qint64 age, const QString& height, const QString& weight, const QString& sym);
    QJsonObject cancelAppointment(qint64 apptId);
    QJsonObject  listAppointments(qint64 user_id);//patientId

    // —— 病例列表（record.list）
    QJsonObject  listRecords(qint64 user_id,qint64 appt_id);

    QJsonObject listUserRecords(qint64 user_id);

    // —— 健康评估（health.submit）
    QJsonObject submitHealth(qint64 patientId, const QJsonArray& answers,
                             double score, const QString& risk, const QString& aiAdvice);

    // —— 聊天：发送 / 收件箱
    QJsonObject sendMessage(qint64 fromUserId, qint64 toUserId, const QString& content);
    QJsonArray  inbox(qint64 myUserId, qint64 sinceUnix = 0);

    // —— 辅助：从 user_id 找 patient_id
    qint64 patientIdFromUser(qint64 userId);


    // —— 患者端：获取用户个人信息
    QJsonObject getUserInfo(qint64 userId);


    // 患者端：获取科室列表
    // 返回示例：{ ok:true, payload:{ appointments:[ {department_name:"呼吸科"}, ... ] } }
    QJsonObject listDepartments();

    // 患者端：按科室名获取医生列表
    // 参数：departmentName —— 例如 "呼吸科"
    // 返回示例：{ ok:true, payload:{ appointments:[ {doctor_id:..., full_name:"...", bio:"...", duty_start:"...", reg_fee:50, daily_quota:50}, ... ] } }
    QJsonObject listDoctorsByDepartment(const QString& departmentName);

    void test();

    //修改患者个人信息
    QJsonObject changeUserInfo(qint64 user_id, const QString& name,
                                            const QString& phone, const QString& id_number,
                                            const QString& address);


    //修改患者登录密码
    QJsonObject changePassword(qint64 user_id,
                                            const QString& old_passwd,
                                            const QString& new_passwd);


    // =============== 健康评估提交（含 time；响应只返回 ok） ===============
    QJsonObject submitHealth(qint64 user_id,
                                          const QString& time_text,      // "2025-08-25 10:00"
                                          const QString& risk_level,     // "高/中/低"
                                          const QJsonArray& advice_in);   // ["建议1","建议2",...]

    // =============== 获取健康评估（最新一条） ===============
    // 请求:  type=health.get, {seq, user_id}
    // 返回:  { ok, payload: { time, risk_level, advice[] } }
    QJsonObject getHealth(qint64 user_id);














    //返回医生的仪表盘
    QJsonObject getDoctorConsole(qint64 doctor_id);

    //返回一个患者的情况
    QJsonObject doctorAppoinment(qint64 doctor_id);

    // 写医嘱：根据 doctor 的 user_id 与 patient_id，给最近一次预约写入 encounters.notes
    QJsonObject doctorOrder(qint64 doctor_user_id, qint64 patient_id, const QString& orderText);

    //医生发送消息
    QJsonObject doctorSendMessage(qint64 doctor_user_id, qint64 patient_id, const QString& content);

    //医生接受消息
    QJsonObject doctorInBox(qint64 doctor_user_id, const QString& content);

    //医生注册
    QJsonObject registerDoctor(const QString& name, const QString& passwd);

    //医生修改密码
    QJsonObject doctorModify(qint64 user_id,
                                          const QString& gonghao,
                                          const QString& shenfen,
                                          const QString& passwd);

    // goWork: 医生上班打卡（由 user_id 定位 doctor，再写 attendance ）
    // timeStr 可为空；为空则使用当前本地时间
    QJsonObject goWork(qint64 user_id, const QString& timeStr);


    // 下班打卡：根据 user_id 找到 doctor_id，写入当天的 check_out
    QJsonObject offWork(qint64 user_id, const QString& timeStr /*可为空*/);

    //医生请假
    QJsonObject vacation(qint64 doctor_id,
                                      const QString& start_date,
                                      const QString& end_date,
                                      const QString& reason);

    //医生考勤查询
    QJsonObject checkWork(qint64 user_id, int limitDays /*=30*/);


    //医生登录
    QJsonObject doctorSignIn(const QString& name,
                                          const QString& passwd,
                                          const QString& role /*"doctor"*/);

    //建造图
    QJsonObject statisticDraw(qint64 seq,QString bing);
signals:

private:
    QSqlDatabase db;//可添加多个，根据需要选择
    QString dbPath;
    QMutex * dbMutex;
    // 性别中文→存库代码（"男"→"M","女"→"F"，否则 NULL）
    QString mapGender(const QString& genderCN) const;
};

#endif // SQLDATABASE_H
