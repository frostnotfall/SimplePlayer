#pragma once
#include <QJsonArray>
class QQuickWindow;
namespace bp { class PlayerController; }
void startFinalFeaturesProbe(bp::PlayerController&,QQuickWindow&,QJsonArray&,QString dataDirectory);
