import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

Rectangle {
    id: bar
    objectName: "playbackBar"
    required property var shell
    implicitHeight: 58 + (summary.visible ? 22 : 0)
    color: Theme.background
    readonly property bool interacting: timeline.pressed || volume.pressed || videoChoices.opened || audioChoices.opened || subtitleChoices.opened || repeatChoices.opened || speedChoices.opened
    readonly property var videoStreams: Player.qualities.filter(c => !c.audio)
    readonly property var audioStreams: Player.qualities.filter(c => c.audio)
    readonly property var videoSelection: videoStreams.find(c => c.id === Player.selectedQuality)
    readonly property var audioSelection: audioStreams.find(c => c.id === Player.selectedAudio)
    FontMetrics { id: menuFont; font: videoButton.font }
    HoverHandler { onPointChanged: if(hovered) { const p=bar.mapToGlobal(point.position.x,point.position.y);bar.shell.handlePointer(bar.shell.contentItem.mapFromGlobal(p.x,p.y)) } }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Label {
            id: summary; objectName: "mediaSummary"
            visible: text.length > 0
            Layout.fillWidth: true; Layout.minimumHeight: 22; Layout.maximumHeight: 22
            leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter
            text: MetadataFormat.summary(Player.mediaInformation); textFormat: Text.PlainText
            color: Theme.muted; font.pixelSize: 12; elide: Text.ElideRight
            HoverHandler { id: summaryHover }
            NativeTip { host: summary; active: summaryHover.hovered; text: summary.text }
            TapHandler { onTapped: bar.shell.showMediaInfo() }
        }
        Slider {
            id: timeline; objectName: "playbackTimeline"
            Layout.fillWidth: true; Layout.preferredHeight: 18
            property real dragStart: 0
            property real dragEnd: 1
            property real dragValue: 0
            from: 0; to: 1
            enabled: Player.canSeek
            // Apply bounds and position together. Slider clamps its value when
            // either bound changes, so separate live bindings produce jumps.
            function syncProgress() {
                if(pressed)return
                from=Player.seekStart
                to=Math.max(from+0.01,Player.seekEnd)
                value=Player.position
            }
            Component.onCompleted: syncProgress()
            Connections {
                target: Player
                function onProgressChanged() { Qt.callLater(timeline.syncProgress) }
                function onChanged() { Qt.callLater(timeline.syncProgress) }
            }
            property real displayFraction: visualPosition
            Behavior on displayFraction { NumberAnimation { duration: Player.isLive && !timeline.pressed && !Player.seeking ? 100 : 0 } }
            leftPadding: 8; rightPadding: 8
            background: Rectangle { x: timeline.leftPadding; y: (timeline.height-height)/2; width: timeline.availableWidth; height: 3; color: Theme.raised
                Rectangle { width: timeline.displayFraction*parent.width; height: parent.height; color: Theme.accent }
            }
            handle: Rectangle { x: timeline.leftPadding + timeline.displayFraction*(timeline.availableWidth-width); y: (timeline.height-height)/2; width: 12; height: 12; radius: 6; color: timeline.pressed ? Theme.accent : Theme.text; Behavior on color { ColorAnimation { duration: Theme.motionDuration } } }
            onPressedChanged: {
                if(pressed){dragStart=Player.seekStart;dragEnd=Math.max(dragStart+0.01,Player.seekEnd);dragValue=value}
                else { Player.seek(dragValue);syncProgress() }
            }
            onMoved: {dragValue=value;if(!pressed){Player.seek(value);syncProgress()}}
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 4; Layout.rightMargin: 4; Layout.bottomMargin: 4; spacing: 4
            PlaybackButton { id: pauseButton; text: Player.state === "播放完毕" ? "重新播放" : Player.state === "已暂停" ? "继续播放" : "暂停"; iconKind: "pause"; paused: Player.state === "已暂停" || Player.state === "播放完毕"; enabled: Player.hasMedia; onClicked: Player.togglePause(); NativeTip { host: pauseButton; active: pauseButton.hovered; text: "播放 / 暂停 · 空格 / 双击视频" } }
            PlaybackButton { text: "停止"; iconKind: "stop"; enabled: Player.state !== "就绪"; onClicked: Player.stop(); NativeTip { host: parent; active: parent.hovered; text: "停止" } }
            Label {
                objectName: "playbackTime"
                text: Player.isLive ? "LIVE · -"+bar.shell.formatTime(Math.max(0,Player.seekEnd-(timeline.pressed?timeline.value:Player.position))) : Player.canSeek ? bar.shell.formatTime(timeline.pressed ? timeline.value : Player.position)+" / "+bar.shell.formatTime(Player.duration) : Player.hasMedia ? "LIVE · 实时" : "00:00 / 00:00"
                color: Theme.text; font.family: "Consolas"; font.pixelSize: Theme.controlTextSize
            }
            Label {
                objectName: "playbackStatus"
                visible: Player.error.length > 0 || bar.width > 1050
                text: Player.error.length ? Player.error : Player.state
                color: Player.error.length ? "#ff9aa5" : Theme.muted; font.pixelSize: Theme.controlTextSize
                Layout.maximumWidth: Math.max(60,bar.width-700); elide: Text.ElideRight
                HoverHandler { id: statusHover }
                NativeTip { host: parent; active: statusHover.hovered; text: Player.error.length ? Player.error : Player.detail }
            }
            Item { Layout.fillWidth: true }
            PlaybackButton {
                id: speedButton; objectName: "speedButton"
                text: Number(Player.playbackRate.toFixed(2))+"×"
                enabled: Player.canChangePlaybackRate
                onClicked: speedChoices.open()
                NativeTip { host: speedButton; active: speedButton.hovered; text: "播放速度 · C 加速 / X 减速 / Z 正常速度（保持音调）" }
                StableMenu {
                    id: speedChoices; objectName: "speedMenu"; rows: 10; y: -height
                    FontMetrics { id: speedFont; font: speedChoices.font }
                    width: Math.ceil(speedFont.advanceWidth("1× · 正常速度") + labelPadding)
                    Repeater {
                        model: [0.25,0.5,0.75,1,1.25,1.5,1.75,2,3,4]
                        PlayerMenuItem { required property real modelData; objectName: "speed_"+modelData; height: Theme.controlSize; text: modelData === 1 ? "1× · 正常速度" : modelData+"×"; checkable: true; checked: Math.abs(Player.playbackRate-modelData)<0.001; onTriggered: Player.setPlaybackRate(modelData) }
                    }
                }
            }
            PlaybackButton {
                objectName: "repeatButton"; text: "循环"
                onClicked: repeatChoices.open()
                RepeatMenu { id: repeatChoices; y: -height }
                NativeTip { host: parent; active: parent.hovered; text: Player.settings.loopMode === "one" ? "单个循环" : "不循环" }
            }
            PlaybackButton { objectName: "returnToLiveButton"; text: "回到直播"; visible: Player.isLive; enabled: Player.canSeek; onClicked: Player.returnToLive() }
            PlaybackButton {
                id: videoButton; objectName: "videoStreamButton"
                text: bar.videoSelection && bar.videoSelection.quality ? bar.videoSelection.quality : "视频流"
                enabled: Player.hasMedia && bar.videoStreams.length > 0
                onClicked: videoChoices.open()
                NativeTip { host: videoButton; active: videoButton.hovered; text: bar.videoSelection ? bar.videoSelection.qualityDetail : "选择视频流" }
                StableMenu {
                    id: videoChoices; popupType: Popup.Window
                    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.menuMotionDuration } }
                    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.menuMotionDuration } }
                    rows: bar.videoStreams.length
                    y: -height; width: Math.min(bar.shell.width-24,Math.max(140,bar.videoStreams.reduce((w,c)=>Math.max(w,menuFont.advanceWidth(c.qualityDetail || c.quality || "视频流")),0)+56))
                    Repeater {
                        model: bar.videoStreams
                        PlayerMenuItem { required property var modelData; font: videoButton.font; implicitHeight: Theme.controlSize; text: modelData.qualityDetail || modelData.quality || "视频流"; checkable: true; checked: modelData.id === Player.selectedQuality; onTriggered: Player.selectQuality(modelData.id) }
                    }
                }
            }
            PlaybackButton {
                id: audioButton; objectName: "audioStreamButton"
                text: bar.audioSelection && bar.audioSelection.quality ? bar.audioSelection.quality : "音频流"
                enabled: Player.hasMedia && bar.audioStreams.length > 0
                onClicked: audioChoices.open()
                NativeTip { host: audioButton; active: audioButton.hovered; text: bar.audioSelection ? bar.audioSelection.qualityDetail : "媒体内音频" }
                StableMenu {
                    id: audioChoices; popupType: Popup.Window
                    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.menuMotionDuration } }
                    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.menuMotionDuration } }
                    rows: bar.audioStreams.length
                    y: -height; width: Math.min(bar.shell.width-24,Math.max(140,bar.audioStreams.reduce((w,c)=>Math.max(w,menuFont.advanceWidth(c.qualityDetail || c.quality || "音频流")),0)+56))
                    Repeater {
                        model: bar.audioStreams
                        PlayerMenuItem { required property var modelData; font: audioButton.font; implicitHeight: Theme.controlSize; text: modelData.qualityDetail || modelData.quality || "音频流"; checkable: true; checked: modelData.id === Player.selectedAudio; onTriggered: Player.selectAudio(modelData.id) }
                    }
                }
            }
            PlaybackButton {
                id: subtitleButton; objectName: "subtitleButton"; text: "字幕"
                enabled: Player.hasMedia
                onClicked: subtitleChoices.open()
                SubtitleMenu { id: subtitleChoices; shell: bar.shell; y: -height }
            }
            RowLayout {
                id: volumeControls; spacing: 4
                WheelHandler {
                    target: null
                    onWheel: function(event) {
                        let delta=event.angleDelta.y || event.pixelDelta.y
                        if(delta)Player.setVolume(Math.max(0,Math.min(100,(Player.settings.volume || 0)+(delta>0?5:-5))))
                        event.accepted=true;bar.shell.revealChrome()
                    }
                }
                PlaybackButton { text: Player.settings.muted ? "取消静音" : "静音"; iconKind: "volume"; muted: Player.settings.muted || Player.settings.volume === 0; onClicked: Player.toggleMute(); NativeTip { host: parent; active: parent.hovered; text: "静音 / 取消静音 · 滚轮调节音量" } }
                Label { objectName: "volumePercent"; text: (Player.settings.volume || 0)+"%"; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight; color: Player.settings.muted ? Theme.muted : Theme.text; font.family: Theme.font; font.pixelSize: Theme.controlTextSize }
                Slider {
                id: volume; Layout.preferredWidth: 84; Layout.minimumHeight: Theme.controlSize; Layout.maximumHeight: Theme.controlSize
                objectName: "volumeSlider"
                from: 0; to: 100; value: Player.settings.volume || 0; leftPadding: 6; rightPadding: 6
                background: Rectangle {
                    x: volume.leftPadding; y: (volume.height-height)/2; width: volume.availableWidth; height: 3; color: Theme.raised
                    Rectangle { width: volume.visualPosition*parent.width; height: parent.height; color: Theme.accent }
                }
                handle: Rectangle {
                    x: volume.leftPadding + volume.visualPosition*(volume.availableWidth-width); y: (volume.height-height)/2
                    width: 12; height: 12; radius: 6; color: volume.pressed ? Theme.accent : Theme.text
                    Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
                }
                onMoved: Player.setVolume(Math.round(value))
                }
            }
            PlaybackButton { text: "显示 / 隐藏侧边栏"; iconKind: "sidebar"; onClicked: bar.shell.toggleSidebar(); NativeTip { host: parent; active: parent.hovered; text: "显示 / 隐藏侧边栏" } }
            PlaybackButton { text: "视频全屏"; iconKind: "fullscreen"; enabled: Player.hasMedia; onClicked: bar.shell.toggleVideoFullscreen(); NativeTip { host: parent; active: parent.hovered; text: "视频全屏 · F11 / 鼠标中键" } }
        }
    }
}
