import QtQuick
import QtQuick.Controls
import SimplePlayer

ToolTip {
    id: tip
    required property Item host
    property bool active: false
    x: Math.max(0,(host.width-width)/2)
    popupType: Popup.Window
    visible: active
    delay: 500
    font.family: Theme.font; font.pixelSize: 12
    FontMetrics { id: tipFont; font: tip.font }
    width: Math.min(640, Math.ceil(tipFont.advanceWidth(text)) + leftPadding + rightPadding + 2)
    contentItem: Text { text: tip.text; textFormat: Text.PlainText; color: Theme.text; font: tip.font; wrapMode: Text.Wrap }
    background: Rectangle { color: Theme.panel; border.color: Theme.raised; radius: 3 }
}
