import QtQuick

Canvas {
    id: mark
    property color ink: "white"
    implicitWidth: 13
    implicitHeight: implicitWidth
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        ctx.strokeStyle = ink
        ctx.lineWidth = Math.max(1, width * 0.13)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        ctx.moveTo(width * 0.16, height * 0.52)
        ctx.lineTo(width * 0.40, height * 0.76)
        ctx.lineTo(width * 0.84, height * 0.22)
        ctx.stroke()
    }
}
