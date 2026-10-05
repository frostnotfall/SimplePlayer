import QtQuick
import QtQuick.Controls
import SimplePlayer

StableMenu {
    id: menu
    title: "循环设置"
    objectName: "repeatMenu"
    rows: 2
    FontMetrics { id: metrics; font: menu.font }
    width: Math.ceil(Math.max(metrics.advanceWidth("不循环"),metrics.advanceWidth("单个循环")))+56
    ActionGroup { id: modes; exclusive: true }
    PlayerMenuItem {
        objectName: "repeatNone"
        height: Theme.controlSize
        action: Action {
            text: "不循环"; checkable: true; checked: Player.settings.loopMode !== "one"
            ActionGroup.group: modes
            onTriggered: Player.setSetting("loopMode","none")
        }
    }
    PlayerMenuItem {
        objectName: "repeatOne"
        height: Theme.controlSize
        action: Action {
            text: "单个循环"; checkable: true; checked: Player.settings.loopMode === "one"
            ActionGroup.group: modes
            onTriggered: Player.setSetting("loopMode","one")
        }
    }
}
