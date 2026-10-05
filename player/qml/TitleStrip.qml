import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

Rectangle {
    id: strip
    required property var shell
    implicitHeight: 32
    color: Theme.background
    HoverHandler { onPointChanged: if(hovered) { const p=strip.mapToGlobal(point.position.x,point.position.y);strip.shell.handlePointer(strip.shell.contentItem.mapFromGlobal(p.x,p.y)) } }
    RowLayout {
        anchors.fill: parent; spacing: 0
        ToolButton { text: "☰"; implicitWidth: 34; implicitHeight: 32; onClicked: strip.shell.showVideoMenu(this); NativeTip { host: parent; active: parent.hovered; text: "播放器菜单" } }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Label { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: Player.title.replace(/[\r\n]+/g," "); color: Theme.text; elide: Text.ElideRight; font.pixelSize: 12; leftPadding: 4 }
            MouseArea {
                anchors.fill: parent
                onPressed: strip.shell.startSystemMove()
                onDoubleClicked: strip.shell.toggleMaximized()
            }
        }
        ToolButton { objectName: "mediaInfoButton"; visible: Object.keys(Player.mediaInformation).length>0; text: "信息"; implicitHeight: 32; onClicked: strip.shell.showMediaInfo() }
        ToolButton { visible: Player.scriptEnabled; text: Player.loggedIn ? "已登录" : "登录"; implicitHeight: 32; onClicked: Player.login() }
        ToolButton { text: "字幕"; implicitHeight: 32; onClicked: strip.shell.showTracks() }
        ToolButton { text: "设置"; implicitHeight: 32; onClicked: strip.shell.showSettings() }
        WindowButton { objectName: "minimizeWindowButton"; text: "最小化"; iconKind: "minimize"; onClicked: strip.shell.minimizeWindow() }
        WindowButton { objectName: "maximizeWindowButton"; text: "最大化 / 还原"; iconKind: strip.shell.visibility === Window.Maximized ? "restore" : "maximize"; onClicked: strip.shell.toggleMaximized() }
        WindowButton { objectName: "closeWindowButton"; text: "关闭"; iconKind: "close"; onClicked: strip.shell.close() }
    }
}
