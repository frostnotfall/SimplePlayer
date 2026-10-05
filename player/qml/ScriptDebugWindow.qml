import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

ApplicationWindow {
    id: debugWindow
    required property var shell
    objectName: "scriptDebugWindow"
    title: "AngelScript 调试窗口"; width: 860; height: 540
    minimumWidth: 520; minimumHeight: 300
    transientParent: shell; visible: false
    color: Theme.background; palette: shell.palette; font: shell.font
    function open() { x=shell.x+(shell.width-width)/2;y=Math.max(0,shell.y+(shell.height-height)/2);show();requestActivate() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        Label { text: Player.scriptEnabled ? "编译信息、函数调用与 HostPrintUTF8 日志（仅保留最近 500 条，凭据已脱敏）" : "AngelScript 已关闭。本地播放不执行解析脚本。"; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            TextArea { id: log; objectName: "scriptDebugLog"; text: Player.debugLog; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: "Consolas"; font.pixelSize: 12; color: Theme.text; background: Rectangle { color: Theme.panel } }
        }
        RowLayout {
            Button { text: "复制日志"; enabled: log.text.length>0; onClicked: { log.selectAll();log.copy();log.deselect() } }
            Button { text: "清空日志"; onClicked: Player.clearDebugLog() }
            Item { Layout.fillWidth: true }
            Button { text: "关闭"; onClicked: debugWindow.close() }
        }
    }
}
