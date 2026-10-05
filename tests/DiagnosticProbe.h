#pragma once
#include <QJsonArray>
class QQuickWindow;
namespace bp { class PlayerController; }
void startDiagnosticProbe(bp::PlayerController&,QQuickWindow&,QJsonArray&,QString);
