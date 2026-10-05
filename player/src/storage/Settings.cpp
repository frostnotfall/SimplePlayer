#include "Settings.h"
#include "domain/Models.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QDateTime>
namespace bp {
Settings::Settings(QString dir):directory(QDir(dir).absolutePath()) {
    if(!QDir().mkpath(directory)) throw Error("Storage","无法创建便携数据目录");
    QFile file(directory+"/settings.json");
    if(file.open(QIODevice::ReadOnly)) {
        QJsonParseError error; auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) {
            file.close(); QFile::copy(file.fileName(), file.fileName()+".broken-"+QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz"));
        } else { values=doc.object(); if(values.value("schemaVersion").toInt(1)>1) throw Error("Storage","配置由更新版本创建"); }
    }
    values["schemaVersion"]=1;
}
void Settings::save() const {
    QSaveFile file(directory+"/settings.json");
    const auto bytes=QJsonDocument(values).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) throw Error("Storage","无法原子保存设置");
}
}
