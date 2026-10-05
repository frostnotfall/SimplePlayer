import QtQuick
import QtQuick.Controls

MenuItem {
    id: control
    // Keep the check at the text's scale, independent of platform icon sizes.
    indicator: CheckMark {
        implicitWidth: control.font.pixelSize
        implicitHeight: implicitWidth
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.checked
        ink: control.palette.windowText
    }
}
