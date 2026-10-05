pragma Singleton
import QtQuick
QtObject {
    readonly property color background: Player.theme.background
    readonly property color panel: Player.theme.panel
    readonly property color raised: Player.theme.raised
    readonly property color text: Player.theme.text
    readonly property color muted: Player.theme.muted
    readonly property color accent: Player.theme.accent
    readonly property string font: Player.theme.font
    readonly property string defaultViewMode: Player.theme.viewMode
    readonly property int controlTextSize: 13
    readonly property int controlSize: 32
    readonly property int iconSize: 16
    readonly property int motionDuration: 150
    readonly property int menuMotionDuration: 60
    readonly property int chromeMotionDuration: 100
}
