import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

ApplicationWindow {
    id: diagnosticWindow
    required property var shell
    objectName: "diagnosticDialog"
    title: "播放诊断"; width: 760; height: 540
    minimumWidth: 520; minimumHeight: 340
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    transientParent: shell; modality: Qt.NonModal; visible: false
    color: Theme.background; palette: shell.palette; font: shell.font
    readonly property bool opened: visible
    property string report: ""
    property string exportMessage: ""
    property bool exportFailed: false
    function refresh() {
        report = JSON.stringify(Object.assign({},Player.diagnostics,{state:Player.state}),null,2)
        if(Player.error.length)report += "\n\n播放错误：" + Player.error
    }
    function open() {
        refresh(); exportMessage = ""
        x = shell.x + (shell.width-width)/2
        y = Math.max(0,shell.y+(shell.height-height)/2)
        show(); requestActivate()
    }
    Shortcut { sequence: "Escape"; onActivated: diagnosticWindow.close() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "解码器、渲染器、音视频连接和字幕状态。点击刷新获取最新数据。"
            color: Theme.muted
        }
        ScrollView {
            objectName: "diagnosticScrollView"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            TextArea {
                id: body; objectName: "diagnosticText"
                text: diagnosticWindow.report; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                readOnly: true; selectByMouse: true; persistentSelection: true
                font.family: "Consolas"; font.pixelSize: Theme.controlTextSize; color: Theme.text
                background: Rectangle { color: Theme.panel }
                TextEditMenu { id: editMenu; field: body }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) { body.forceActiveFocus(); editMenu.popup(point.position.x,point.position.y) }
                }
            }
        }
        TextArea {
            objectName: "diagnosticExportStatus"
            Layout.fillWidth: true; visible: text.length > 0
            text: diagnosticWindow.exportMessage; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
            readOnly: true; selectByMouse: true; persistentSelection: true; padding: 0
            color: diagnosticWindow.exportFailed ? "#ff9aa5" : Theme.muted
            font.pixelSize: Theme.controlTextSize; background: null
        }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "refreshDiagnostics"; text: "刷新"; onClicked: diagnosticWindow.refresh() }
            Button { objectName: "copyDiagnostics"; text: "复制全部"; onClicked: { body.selectAll(); body.copy(); body.deselect() } }
            Button {
                objectName: "exportDiagnostics"; text: "导出脱敏诊断"
                onClicked: {
                    const result = Player.exportDiagnostics()
                    diagnosticWindow.exportFailed = !result.ok
                    diagnosticWindow.exportMessage = result.ok ? "诊断已保存到 " + result.path : result.error
                }
            }
            Item { Layout.fillWidth: true }
            Button { objectName: "closeDiagnostics"; text: "关闭"; onClicked: diagnosticWindow.close() }
        }
    }
}
