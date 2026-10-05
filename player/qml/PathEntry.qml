import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SimplePlayer

ColumnLayout {
    id: entry
    required property var spec
    property alias textInput: field
    Layout.fillWidth: true; spacing: 4
    Label { text: entry.spec.label; color: Theme.muted; font.pixelSize: 12 }
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        TextField {
            id: field; objectName: "path_"+entry.spec.key
            Layout.fillWidth: true; text: Player.settings[entry.spec.key] || ""
            selectByMouse: true; persistentSelection: true; activeFocusOnPress: true
            onEditingFinished: if(text !== (Player.settings[entry.spec.key] || ""))Player.setSetting(entry.spec.key,text)
            Keys.onShortcutOverride: function(event) { if(event.modifiers & Qt.ControlModifier)event.accepted=true }
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: {field.forceActiveFocus();editMenu.popup(point.position.x,point.position.y)} }
            TextEditMenu { id: editMenu; field: entry.textInput }
        }
        Button { text: "选择"; onClicked: entry.spec.folder ? folderPicker.open() : filePicker.open() }
        Button { text: "打开目录"; enabled: field.text.length>0; onClicked: Player.openPath(field.text) }
    }
    FileDialog { id: filePicker; title: "选择"+entry.spec.label; nameFilters: entry.spec.filters || ["所有文件 (*)"]; onAccepted: Player.setSetting(entry.spec.key,Player.nativePath(selectedFile.toString())) }
    FolderDialog { id: folderPicker; title: "选择"+entry.spec.label; onAccepted: Player.setSetting(entry.spec.key,Player.nativePath(selectedFolder.toString())) }
}
