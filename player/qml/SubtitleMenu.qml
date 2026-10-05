import QtQuick
import QtQuick.Controls
import SimplePlayer

StableMenu {
    id: subtitleMenu
    required property var shell
    title: "字幕"
    objectName: "subtitleMenu"
    FontMetrics { id: metrics; font: subtitleMenu.font }
    readonly property var labels: ["显示主字幕", "显示次字幕", "主字幕", "次字幕"]
    width: Math.min(shell.width-24,Math.ceil(labels.reduce((w,s)=>Math.max(w,metrics.advanceWidth(s)),0))+56)
    rows: 4
    delegate: PlayerMenuItem { height: Theme.controlSize; font: subtitleMenu.font }
    PlayerMenuItem {
        objectName: "mainSubtitleVisibility"
        height: Theme.controlSize; autoExclusive: false
        action: shell.mainSubtitleAction
    }
    PlayerMenuItem {
        objectName: "secondarySubtitleVisibility"
        height: Theme.controlSize; autoExclusive: false
        action: shell.secondarySubtitleAction
    }
    SubtitleTrackMenu { title: "主字幕"; slot: 0; shell: subtitleMenu.shell }
    SubtitleTrackMenu { title: "次字幕"; slot: 1; shell: subtitleMenu.shell }
}
