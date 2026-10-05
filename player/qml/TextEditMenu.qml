import QtQuick
import QtQuick.Controls
import SimplePlayer

StableMenu {
    id: editMenu
    required property var field
    objectName: "textEditMenu"
    rows: 4
    FontMetrics { id: metrics; font: editMenu.font }
    width: Math.ceil(metrics.advanceWidth("复制 · Ctrl+C"))+56
    PlayerMenuItem { objectName: "editCopy"; text: "复制 · Ctrl+C"; height: Theme.controlSize; enabled: editMenu.field.selectedText.length>0; onTriggered: editMenu.field.copy() }
    PlayerMenuItem { objectName: "editCut"; text: "剪切 · Ctrl+X"; height: Theme.controlSize; enabled: !editMenu.field.readOnly && editMenu.field.selectedText.length>0; onTriggered: editMenu.field.cut() }
    PlayerMenuItem { objectName: "editPaste"; text: "粘贴 · Ctrl+V"; height: Theme.controlSize; enabled: !editMenu.field.readOnly && editMenu.field.canPaste; onTriggered: editMenu.field.paste() }
    PlayerMenuItem { objectName: "editSelectAll"; text: "全选 · Ctrl+A"; height: Theme.controlSize; enabled: editMenu.field.text.length>0; onTriggered: editMenu.field.selectAll() }
}
