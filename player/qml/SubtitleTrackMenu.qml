import QtQuick
import QtQuick.Controls
import SimplePlayer

StableMenu {
    id: tracks
    required property int slot
    required property var shell
    objectName: slot === 0 ? "mainSubtitleMenu" : "secondarySubtitleMenu"
    readonly property var sources: Player.subtitles.length ? Player.subtitles : [{id:"",label:"关闭"}]
    FontMetrics { id: metrics; font: tracks.font }
    width: Math.min(shell.width-24,Math.ceil(sources.reduce((w,s)=>Math.max(w,metrics.advanceWidth(s.label)),metrics.advanceWidth("加载外置字幕…")))+56)
    rows: sources.length+1; separators: 1
    Instantiator {
        model: tracks.sources
        delegate: PlayerMenuItem {
            required property var modelData
            text: modelData.label; height: Theme.controlSize
            checkable: true
            checked: modelData.id === (tracks.slot === 0 ? Player.selectedMainSubtitle : Player.selectedSecondarySubtitle)
            onTriggered: Player.selectSubtitle(tracks.slot,modelData.id)
        }
        onObjectAdded: (index,object) => tracks.insertItem(index,object)
        onObjectRemoved: (index,object) => tracks.removeItem(object)
    }
    MenuSeparator { height: 8 }
    PlayerMenuItem { text: "加载外置字幕…"; height: Theme.controlSize; onTriggered: tracks.shell.loadSubtitleFile(tracks.slot) }
}
