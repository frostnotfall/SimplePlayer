import QtQuick
import QtQuick.Controls
import SimplePlayer

Menu {
    id: menu
    popupType: Popup.Window
    padding: 4
    font.family: Theme.font; font.pixelSize: Theme.controlTextSize
    delegate: PlayerMenuItem { height: Theme.controlSize; font: menu.font }
    readonly property real labelPadding: 2*padding + 12 + font.pixelSize + 6
    // Each native popup resolves its own palette. Set it explicitly so nested
    // menus follow the live theme rather than the platform's default palette.
    palette.window: Theme.background
    palette.base: Theme.panel
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.light: Theme.raised
    palette.midlight: Theme.accent
    palette.dark: Theme.raised
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.text
    background: Rectangle { color: Theme.background; border.color: Theme.raised }
    // Closed Popup.Window items are not polished automatically. Use final
    // dimensions before showing the native window, rather than implicitHeight.
    property int rows: count
    property int separators: 0
    height: Math.min(rows*Theme.controlSize+separators*8+8,Screen.height-48)
    onAboutToShow: if(contentItem && contentItem.forceLayout)contentItem.forceLayout()
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.menuMotionDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.menuMotionDuration } }
}
