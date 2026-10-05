import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

ToolButton {
    id: button
    property string iconKind: ""
    property bool paused: false
    property bool muted: false
    font.family: Theme.font
    font.pixelSize: Theme.controlTextSize
    implicitWidth: iconKind.length ? Theme.controlSize : Math.max(Theme.controlSize, caption.implicitWidth + 16)
    implicitHeight: Theme.controlSize
    Layout.minimumHeight: Theme.controlSize
    Layout.maximumHeight: Theme.controlSize
    padding: 0
    opacity: enabled ? 1 : 0.45
    Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }
    background: Rectangle {
        color: button.down ? Theme.accent : button.hovered ? Theme.raised : Theme.panel
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
    }
    contentItem: Item {
        Text {
            id: caption
            anchors.centerIn: parent
            visible: !button.iconKind.length
            text: button.text; font: button.font; color: Theme.text
        }
        Canvas {
            id: glyph
            anchors.centerIn: parent
            visible: button.iconKind.length > 0
            width: Theme.iconSize; height: Theme.iconSize
            property string kind: button.iconKind
            property bool paused: button.paused
            property bool muted: button.muted
            property color ink: Theme.text
            onKindChanged: requestPaint()
            onPausedChanged: requestPaint()
            onMutedChanged: requestPaint()
            onInkChanged: requestPaint()
            onPaint: {
                let ctx = getContext("2d")
                ctx.clearRect(0,0,width,height); ctx.fillStyle=ink; ctx.strokeStyle=ink; ctx.lineWidth=1.5
                if (kind === "pause") {
                    if (paused) { ctx.beginPath(); ctx.moveTo(3,2); ctx.lineTo(14,8); ctx.lineTo(3,14); ctx.closePath(); ctx.fill() }
                    else { ctx.fillRect(3,2,4,12); ctx.fillRect(10,2,4,12) }
                } else if (kind === "stop") ctx.fillRect(3,3,10,10)
                else if (kind === "volume") {
                    ctx.beginPath();ctx.moveTo(2,6);ctx.lineTo(5,6);ctx.lineTo(9,2);ctx.lineTo(9,14);ctx.lineTo(5,10);ctx.lineTo(2,10);ctx.closePath();ctx.fill()
                    ctx.beginPath()
                    if(muted) {ctx.moveTo(11,5);ctx.lineTo(15,11);ctx.moveTo(15,5);ctx.lineTo(11,11)}
                    else {ctx.arc(9,8,5,-0.8,0.8)}
                    ctx.stroke()
                }
                else if (kind === "sidebar") {
                    ctx.strokeRect(1.5,2.5,13,11); ctx.beginPath(); ctx.moveTo(10,3); ctx.lineTo(10,13); ctx.stroke()
                } else if (kind === "fullscreen") {
                    ctx.beginPath()
                    ctx.moveTo(6,2); ctx.lineTo(2,2); ctx.lineTo(2,6)
                    ctx.moveTo(10,2); ctx.lineTo(14,2); ctx.lineTo(14,6)
                    ctx.moveTo(2,10); ctx.lineTo(2,14); ctx.lineTo(6,14)
                    ctx.moveTo(14,10); ctx.lineTo(14,14); ctx.lineTo(10,14); ctx.stroke()
                }
            }
        }
    }
}
