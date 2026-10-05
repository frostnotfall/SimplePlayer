import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

ApplicationWindow {
    id: infoWindow
    required property var shell
    objectName: "mediaInfoWindow"
    title: "媒体信息"; width: 680; height: 520
    minimumWidth: 440; minimumHeight: 300
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    transientParent: shell; modality: Qt.NonModal; visible: false
    color: Theme.background; palette: shell.palette; font: shell.font
    readonly property bool opened: visible
    property bool followCurrent: true
    property var selectedInformation: ({})
    readonly property var information: followCurrent ? Player.mediaInformation : selectedInformation
    readonly property string webUrl: MetadataFormat.text(information.webUrl || information.url)
    function open(data) {
        followCurrent = data === undefined
        selectedInformation = data || ({})
        x = shell.x + (shell.width-width)/2
        y = Math.max(0,shell.y+(shell.height-height)/2)
        show(); requestActivate()
    }
    Shortcut { sequence: "Escape"; onActivated: infoWindow.close() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            TextArea {
                id: body; objectName: "mediaInfoText"
                text: MetadataFormat.details(infoWindow.information)
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                readOnly: true; selectByMouse: true; persistentSelection: true
                color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.controlTextSize
                background: Rectangle { color: Theme.panel }
                TextEditMenu { id: editMenu; field: body }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) { body.forceActiveFocus(); editMenu.popup(point.position.x,point.position.y) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "openMediaWebUrl"; text: "打开原网页"; enabled: /^https?:\/\//i.test(infoWindow.webUrl); onClicked: Qt.openUrlExternally(infoWindow.webUrl) }
            Button { objectName: "copyMediaInformation"; text: "复制信息"; onClicked: { body.selectAll(); body.copy(); body.deselect() } }
            Item { Layout.fillWidth: true }
            Button { text: "关闭"; onClicked: infoWindow.close() }
        }
    }
}
