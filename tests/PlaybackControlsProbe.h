#pragma once
#include <QJsonArray>
class QQuickWindow;
namespace bp { class PlayerController; }
void startPlaybackControlsProbe(bp::PlayerController&,QQuickWindow&,QJsonArray&);
