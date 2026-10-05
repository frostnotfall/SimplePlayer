#pragma once
#include <QJsonObject>
#include <QString>
namespace bp {
class Settings {
public:
    explicit Settings(QString directory);
    QJsonObject values;
    QString directory;
    void save() const;
};
}
