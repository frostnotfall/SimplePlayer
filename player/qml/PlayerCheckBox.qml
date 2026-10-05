import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    indicator: Rectangle {
        implicitWidth: control.font.pixelSize + 4
        implicitHeight: implicitWidth
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        color: control.down ? control.palette.light : control.palette.base
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? control.palette.highlight : control.palette.text
        CheckMark {
            anchors.centerIn: parent
            width: control.font.pixelSize; height: width
            visible: control.checkState === Qt.Checked
            ink: control.palette.text
        }
        Rectangle {
            anchors.centerIn: parent
            width: control.font.pixelSize * 0.65; height: 2
            color: control.palette.text
            visible: control.checkState === Qt.PartiallyChecked
        }
    }
}
