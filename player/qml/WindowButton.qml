import QtQuick
import QtQuick.Controls
import SimplePlayer

ToolButton {
    id: button
    required property string iconKind
    implicitWidth: 32; implicitHeight: 32
    padding: 0
    background: Rectangle {
        color: button.down ? Theme.accent : button.hovered ? Theme.raised : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
    }
    contentItem: Item {
        Canvas {
            id: glyph
            anchors.centerIn: parent; width: 16; height: 16
            property string kind: button.iconKind
            property color ink: Theme.text
            onKindChanged: requestPaint()
            onInkChanged: requestPaint()
            onPaint: {
                let ctx=getContext("2d")
                ctx.clearRect(0,0,width,height);ctx.strokeStyle=ink;ctx.lineWidth=1.5
                if(kind === "minimize") { ctx.beginPath();ctx.moveTo(2.5,8.5);ctx.lineTo(12.5,8.5);ctx.stroke() }
                else if(kind === "maximize")ctx.strokeRect(2.5,2.5,10,10)
                else if(kind === "restore") {
                    ctx.strokeRect(2.5,4.5,8,8)
                    ctx.beginPath();ctx.moveTo(4.5,4.5);ctx.lineTo(4.5,2.5);ctx.lineTo(12.5,2.5);ctx.lineTo(12.5,10.5);ctx.lineTo(10.5,10.5);ctx.stroke()
                }else if(kind === "close") {
                    ctx.beginPath();ctx.moveTo(2.5,2.5);ctx.lineTo(12.5,12.5);ctx.moveTo(12.5,2.5);ctx.lineTo(2.5,12.5);ctx.stroke()
                }
            }
        }
    }
    NativeTip { host: button; active: button.hovered; text: button.text }
}
