import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SimplePlayer

ApplicationWindow {
    id: root
    width: 1320; height: 850
    minimumWidth: rightSidebarResize ? resizeStartWindowWidth-resizeStartSidebarWidth+240 : chromeVisible && sideVisible ? 866 : 620; minimumHeight: 480
    maximumWidth: rightSidebarResize ? resizeStartWindowWidth-resizeStartSidebarWidth+960 : 16777215
    flags: Qt.Window | Qt.FramelessWindowHint
    visible: true
    title: Player.title + " · SimplePlayer"
    color: Theme.background
    font.family: Theme.font
    font.pixelSize: 14
    palette.window: Theme.background
    palette.base: Theme.panel
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.placeholderText: Theme.muted
    palette.highlight: Theme.accent
    palette.light: Theme.raised
    palette.midlight: Theme.accent
    readonly property bool sideVisible: Player.settings.chatVisible !== false
    readonly property string viewMode: ["videoOnly", "videoAndPanel"].includes(Player.settings.viewMode) ? Player.settings.viewMode : Theme.defaultViewMode
    property bool videoFullscreen: false
    readonly property int resizeBorderWidth: 8
    readonly property bool windowResizable: root.visibility === Window.Windowed && !root.videoFullscreen
    property bool fullscreenSidebarVisible: false
    property point lastPointerPosition: Qt.point(-100000,-100000)
    function handlePointer(position) {
        // Showing/hiding a tool window also changes hover state underneath a
        // stationary cursor. Only physical movement should restart visibility.
        if(Math.abs(position.x-lastPointerPosition.x)<1 && Math.abs(position.y-lastPointerPosition.y)<1)return
        lastPointerPosition=position
        revealChrome()
    }
    function closeSidebar() {
        if(videoFullscreen)fullscreenSidebarVisible=false
        else setSidebarVisible(false)
    }
    property bool wasMaximized: false
    readonly property bool chromeVisible: !videoFullscreen && viewMode !== "videoOnly"
    readonly property bool floatingChrome: !chromeVisible
    property bool overlayVisible: true
    readonly property alias mainSubtitleAction: primarySubtitleAction
    readonly property alias secondarySubtitleAction: secondaryAction
    function syncSubtitleActions() {
        primarySubtitleAction.checked=Player.mainSubtitleVisible
        secondaryAction.checked=Player.secondarySubtitleVisible
    }
    Action {
        id: primarySubtitleAction; text: "显示主字幕"; checkable: true
        checked: true
        onCheckedChanged: Qt.callLater(root.syncSubtitleActions)
        onTriggered: Player.setSetting("mainSubtitleVisible", !Player.mainSubtitleVisible)
    }
    Action {
        id: secondaryAction; text: "显示次字幕"; checkable: true
        checked: true
        onCheckedChanged: Qt.callLater(root.syncSubtitleActions)
        onTriggered: Player.setSetting("secondarySubtitleVisible", !Player.secondarySubtitleVisible)
    }
    function revealChrome() { overlayVisible = true; idleChrome.restart() }
    readonly property bool chromeRequested: root.visible && root.floatingChrome && root.overlayVisible && !root.dialogOpen && root.visibility !== Window.Minimized
    property bool geometryReady: false
    property bool dockChanging: false
    property int normalWidth: 1320
    property int normalHeight: 850
    property int dockRevision: 0
    property real rememberedSidebarWidth: Player.settings.chatWidth || 480
    property real liveSidebarWidth: -1
    property bool windowResizeInProgress: false
    property bool rightSidebarResize: false
    property real resizeStartWindowWidth: 0
    property real resizeStartSidebarWidth: 0
    property real videoDragStartX: 0
    property real edgeDragStartY: 0
    property real edgeStartY: 0
    property real edgeStartHeight: 0
    function beginWindowResize(edges) {
        if(windowResizeInProgress)return
        windowResizeInProgress=true
        resizeStartWindowWidth=width;resizeStartSidebarWidth=sidebar.width
        if(chromeVisible && sideVisible)liveSidebarWidth=sidebar.width
        rightSidebarResize=chromeVisible && sideVisible && (edges & Qt.RightEdge) !== 0
    }
    function finishWindowResize() {
        if(!windowResizeInProgress)return
        if(liveSidebarWidth >= 0)Player.setSetting("chatWidth",Math.round(liveSidebarWidth))
        rightSidebarResize=false;windowResizeInProgress=false;liveSidebarWidth=-1
        geometrySaver.restart()
    }
    function beginVideoResize(globalX) {
        beginWindowResize(Qt.LeftEdge);videoDragStartX=globalX
    }
    function dragVideoResize(globalX) {
        if(!windowResizeInProgress)return
        width=Math.max(minimumWidth,Math.min(screen.width,resizeStartWindowWidth+globalX-videoDragStartX))
    }
    function beginRightEdgeResize(globalX,globalY,edges) {
        beginWindowResize(edges);videoDragStartX=globalX;edgeDragStartY=globalY
        edgeStartY=y;edgeStartHeight=height
    }
    function dragRightEdgeResize(globalX,globalY,edges) {
        if(!windowResizeInProgress)return
        const nextWidth=Math.max(minimumWidth,Math.min(maximumWidth,screen.width,resizeStartWindowWidth+globalX-videoDragStartX))
        // Windows may report widthChanged after the mouse is released. Save
        // the sidebar increment now rather than relying on that later signal.
        if(rightSidebarResize)liveSidebarWidth=resizeStartSidebarWidth+nextWidth-resizeStartWindowWidth
        width=nextWidth
        if((edges & Qt.TopEdge) !== 0) {
            height=Math.max(minimumHeight,Math.min(screen.height,edgeStartHeight-globalY+edgeDragStartY))
            y=edgeStartY+edgeStartHeight-height
        }else if((edges & Qt.BottomEdge) !== 0) {
            height=Math.max(minimumHeight,Math.min(screen.height,edgeStartHeight+globalY-edgeDragStartY))
        }
    }
    function rememberGeometry() {
        if(!geometryReady || dockChanging || videoFullscreen || visibility !== Window.Windowed)return
        normalWidth=Math.round(width);normalHeight=Math.round(height)
        Player.saveWindowSize(normalWidth,normalHeight)
    }
    Timer { id: geometrySaver; interval: 400; onTriggered: root.rememberGeometry() }
    onWidthChanged: {
        if(rightSidebarResize)liveSidebarWidth=resizeStartSidebarWidth+width-resizeStartWindowWidth
        if(geometryReady)geometrySaver.restart()
    }
    onHeightChanged: if(geometryReady)geometrySaver.restart()
    onVisibilityChanged: function(nextVisibility) {
        if(nextVisibility === Window.Windowed)geometrySaver.restart()
        else geometrySaver.stop()
    }
    Component.onCompleted: {
        root.width=Math.max(minimumWidth,Math.min(screen.width,Player.settings.windowWidth || 1320))
        root.height=Math.max(minimumHeight,Math.min(screen.height,Player.settings.windowHeight || 850))
        normalWidth=width;normalHeight=height;geometryReady=true
        syncSubtitleActions()
    }
    onClosing: { geometrySaver.stop();rememberGeometry();Player.saveWindowSize(normalWidth,normalHeight) }
    function minimizeWindow() { rememberGeometry();showMinimized() }
    function toggleMaximized() {
        rememberGeometry()
        if (videoFullscreen) toggleVideoFullscreen()
        else if (visibility === Window.Maximized) showNormal()
        else showMaximized()
        revealChrome()
    }
    function loadSubtitleFile(slot) { fileSlot=slot;subtitleFile.open() }
    function showScriptDebug() { scriptDebugWindow.open() }
    function showTracks() { tracksDialog.open() }
    function showSettings() { settingsDialog.open() }
    function showMediaInfo(information) { mediaInfoWindow.open(information) }
    function openUrl() { sideTabs.currentIndex=0; Player.open(urlField.text) }
    function showVideoMenu(item) {
        let p = item.mapToGlobal(0,item.height)
        let local = root.contentItem.mapFromGlobal(p.x,p.y)
        videoMenu.popup(local.x,local.y)
    }
    onFloatingChromeChanged: revealChrome()
    Timer {
        id: idleChrome; interval: 2500
        onTriggered: {
            if (root.dialogOpen || videoMenu.opened || floatingBar.interacting) restart()
            else root.overlayVisible = false
        }
    }
    HoverHandler { parent: root.contentItem; onPointChanged: if(hovered)root.handlePointer(point.position) }
    function setDockLayout(show,mode) {
        finishWindowResize()
        const wasDocked=chromeVisible && sideVisible
        const normal=visibility === Window.Windowed && !videoFullscreen
        const previousWidth=root.width
        const panelWidth=wasDocked ? Math.round(sidebar.width) : rememberedSidebarWidth
        if(wasDocked)rememberedSidebarWidth=panelWidth
        const nextWidth=Math.max(show ? 866 : 620,Math.min(screen.width,previousWidth+(show ? panelWidth+6 : -panelWidth-6)))
        dockChanging=true;const revision=++dockRevision
        // Change visibility and geometry in one update; the video keeps its
        // width while the normal window's right edge expands or contracts.
        if(normal && show && !wasDocked)root.width=nextWidth
        Player.setSetting("viewMode",mode)
        Player.setSetting("chatVisible",show)
        if(normal && wasDocked !== show)root.width=nextWidth
        Qt.callLater(function() {
            if(revision !== root.dockRevision)return
            if(normal && root.visibility === Window.Windowed && wasDocked !== show) {
                root.width=nextWidth
                const left=root.screen.virtualX || 0
                root.x=Math.max(left,Math.min(root.x,left+root.screen.width-root.width))
            }
            root.dockChanging=false;root.geometrySaverRestart()
        })
        root.contentItem.forceActiveFocus();revealChrome()
    }
    function geometrySaverRestart() { geometrySaver.restart() }
    function setViewMode(mode) {
        if(videoFullscreen){toggleVideoFullscreen();Qt.callLater(function(){root.setViewMode(mode)});return}
        setDockLayout(mode === "videoAndPanel",mode)
    }
    function setSidebarVisible(show) {
        if(videoFullscreen){toggleVideoFullscreen();Qt.callLater(function(){root.setSidebarVisible(show)});return}
        setDockLayout(show,"videoAndPanel")
    }
    function toggleSidebar() {
        if(videoFullscreen) {
            fullscreenSidebarVisible=!fullscreenSidebarVisible
        }else setSidebarVisible(!(chromeVisible && sideVisible))
    }
    function toggleViewMode() { setViewMode(viewMode === "videoOnly" ? "videoAndPanel" : "videoOnly") }
    function toggleSidebarPage(page) {
        if(page === 1 && !Player.scriptEnabled)return
        if(sidebar.visible && sideTabs.currentIndex === page)closeSidebar()
        else {
            if(videoFullscreen)fullscreenSidebarVisible=true
            else if(!sidebar.visible)setSidebarVisible(true)
            sideTabs.currentIndex=page
        }
        revealChrome()
    }
    function toggleVideoFullscreen() {
        fullscreenSidebarVisible=false
        if (videoFullscreen) {
            videoFullscreen = false
            if (wasMaximized) root.showMaximized(); else root.showNormal()
        } else if (Player.hasMedia) {
            rememberGeometry()
            wasMaximized = root.visibility === Window.Maximized
            videoFullscreen = true
            root.contentItem.forceActiveFocus()
            root.showFullScreen()
        }
        revealChrome()
    }
    function escapeVideoView() {
        if (videoFullscreen) toggleVideoFullscreen()
        else if (viewMode === "videoOnly") setViewMode("videoAndPanel")
    }
    function focusLink() {
        setViewMode("videoAndPanel")
        Qt.callLater(function() { urlField.forceActiveFocus();Player.focusInputWindow(root);urlField.selectAll() })
    }
    function setWindowWidth(value) {
        finishWindowResize()
        if(videoFullscreen)toggleVideoFullscreen()
        if(root.visibility !== Window.Windowed)root.showNormal()
        root.width=Math.max(root.minimumWidth,Math.min(root.screen.width,value))
    }
    function setWindowHeight(value) {
        finishWindowResize()
        if(videoFullscreen)toggleVideoFullscreen()
        if(root.visibility !== Window.Windowed)root.showNormal()
        root.height=Math.max(root.minimumHeight,Math.min(root.screen.height,value))
    }
    property int fileSlot: 0
    property int messageId: -1
    property var messageQueue: []
    function showNextMessage() {
        if (scriptMessage.opened || messageQueue.length === 0) return
        let message = messageQueue.shift()
        root.messageId=message.id;scriptMessage.title=message.title;scriptMessage.bodyText=message.body;scriptMessage.buttonType=message.buttons;scriptMessage.open()
    }
    property bool dialogOpen: settingsDialog.opened || tracksDialog.opened || diagnosticDialog.opened || scriptMessage.opened || mediaInfoWindow.opened
    function formatTime(value) {
        let s = Math.max(0, Math.floor(value)), h = Math.floor(s / 3600)
        return (h > 0 ? h + ":" : "") + String(Math.floor(s / 60) % 60).padStart(2,"0") + ":" + String(s % 60).padStart(2,"0")
    }
    readonly property bool textEditing: root.activeFocusItem !== null && typeof root.activeFocusItem.selectAll === "function" && typeof root.activeFocusItem.text === "string"
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Space"; enabled: !root.textEditing && !root.dialogOpen; onActivated: Player.togglePause() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Ctrl+L"; onActivated: root.focusLink() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Ctrl+Shift+V"; enabled: !root.dialogOpen; onActivated: root.toggleViewMode() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Escape"; enabled: !root.dialogOpen; onActivated: root.escapeVideoView() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "F11"; enabled: !root.dialogOpen; onActivated: root.toggleVideoFullscreen() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Left"; enabled: Player.canSeek && !root.textEditing && !root.dialogOpen; onActivated: Player.seek(Player.position - 5) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Right"; enabled: Player.canSeek && !root.textEditing && !root.dialogOpen; onActivated: Player.seek(Player.position + 5) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "F6"; autoRepeat: false; enabled: !root.dialogOpen; onActivated: root.toggleSidebarPage(0) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "F9"; autoRepeat: false; enabled: Player.scriptEnabled && !root.dialogOpen; onActivated: root.toggleSidebarPage(1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "C"; enabled: Player.canChangePlaybackRate && !root.textEditing && !root.dialogOpen; onActivated: Player.adjustPlaybackRate(0.25) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "X"; enabled: Player.canChangePlaybackRate && !root.textEditing && !root.dialogOpen; onActivated: Player.adjustPlaybackRate(-0.25) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Z"; enabled: Player.canChangePlaybackRate && !root.textEditing && !root.dialogOpen; onActivated: Player.setPlaybackRate(1.0) }

    ColumnLayout {
        anchors.fill: parent; spacing: 0
        TitleStrip { shell: root; visible: root.chromeVisible; Layout.fillWidth: true; Layout.minimumHeight: 32; Layout.maximumHeight: 32 }
        RowLayout {
            id: split; objectName: "contentSplit"; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            ColumnLayout {
                id: videoColumn
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: root.videoFullscreen ? 200 : 620; spacing: 0
                RowLayout {
                    visible: root.chromeVisible; Layout.fillWidth: true; Layout.minimumHeight: 34; Layout.maximumHeight: 34; spacing: 3
                    TextField {
                        id: urlField; objectName: "urlField"; Layout.fillWidth: true; Layout.fillHeight: true
                        placeholderText: "粘贴链接 · Ctrl+L"; selectByMouse: true
                        persistentSelection: true
                        onActiveFocusChanged: if(activeFocus)Player.focusInputWindow(root)
                        Keys.onShortcutOverride: function(event) { if(event.modifiers & Qt.ControlModifier || event.key === Qt.Key_C || event.key === Qt.Key_X || event.key === Qt.Key_Z)event.accepted=true }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onTapped: function(eventPoint,button) {
                                urlField.forceActiveFocus();Player.focusInputWindow(root)
                                if(button === Qt.RightButton)urlEditMenu.popup(eventPoint.position.x,eventPoint.position.y)
                            }
                        }
                        TextEditMenu { id: urlEditMenu; field: urlField }
                        onAccepted: root.openUrl()
                        leftPadding: 8; topPadding: 4; bottomPadding: 4
                        background: Rectangle { color: Theme.panel; border.color: urlField.activeFocus ? Theme.accent : Theme.raised }
                    }
                    ToolButton { text: "打开url"; implicitHeight: 32; onClicked: root.openUrl() }
                    ToolButton { text: "打开文件"; implicitHeight: 32; onClicked: mediaFile.open() }
                }
                Rectangle {
                    id: videoArea; objectName: "videoArea"
                    Layout.fillWidth: true; Layout.fillHeight: true; color: "#000000"
                    WindowContainer { anchors.fill: parent; window: Player.videoWindow; visible: Player.hasMedia }
                    ColumnLayout {
                        anchors.centerIn: parent; width: Math.min(parent.width-64,520); spacing: 12; visible: !Player.hasMedia
                        Label { Layout.alignment: Qt.AlignHCenter; text: "▶"; color: Theme.accent; font.pixelSize: 52 }
                        Label { Layout.alignment: Qt.AlignHCenter; text: Player.state === "就绪" ? "粘贴链接，开始观看" : Player.state; color: "#edf0fa"; font.pixelSize: 22 }
                        Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: Player.error.length ? Player.error : "Ctrl+L 打开链接，或从菜单选择本地文件"; wrapMode: Text.Wrap; color: Player.error.length ? "#ff9aa5" : "#8d99b1" }
                        BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: Player.state === "解析链接" || Player.state === "连接媒体" }
                    }
                }
                PlaybackBar { id: normalBar; shell: root; visible: root.chromeVisible; Layout.fillWidth: true; Layout.minimumHeight: implicitHeight; Layout.maximumHeight: implicitHeight }
            }
            Rectangle {
                objectName: "videoResizeDivider"
                visible: sidebar.visible; Layout.fillHeight: true; Layout.preferredWidth: 6; Layout.minimumWidth: 6; Layout.maximumWidth: 6
                color: videoDividerMouse.pressed || videoDividerMouse.containsMouse ? Theme.accent : Theme.raised
                Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
                MouseArea {
                    id: videoDividerMouse; anchors.fill: parent; hoverEnabled: true; preventStealing: true
                    enabled: root.visibility === Window.Windowed
                    cursorShape: enabled ? Qt.SizeHorCursor : Qt.ArrowCursor
                    onPressed: function(mouse) { root.beginVideoResize(mapToGlobal(mouse.x,mouse.y).x) }
                    onPositionChanged: function(mouse) { if(pressed)root.dragVideoResize(mapToGlobal(mouse.x,mouse.y).x) }
                    onReleased: root.finishWindowResize()
                    onCanceled: root.finishWindowResize()
                }
            }
            Rectangle {
                id: sidebar; objectName: "contentSidebar"
                Layout.fillHeight: true
                Layout.preferredWidth: root.liveSidebarWidth >= 0 ? root.liveSidebarWidth : Player.settings.chatWidth || 480
                Layout.minimumWidth: 240; Layout.maximumWidth: 960
                visible: root.chromeVisible && root.sideVisible || root.videoFullscreen && root.fullscreenSidebarVisible; color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; spacing: 0
                        TabBar {
                            id: sideTabs; objectName: "sideTabs"; Layout.fillWidth: true; implicitWidth: 0; currentIndex: 0
                            TabButton {
                                id: playlistTab; objectName: "playlistTab"
                                palette.brightText: Theme.text
                                width: sideTabs.width/(Player.scriptEnabled ? 2 : 1); text: "播放列表"; implicitHeight: 32
                                background: Rectangle { color: playlistTab.checked ? Theme.raised : Theme.background; Behavior on color { ColorAnimation { duration: Theme.motionDuration } } }
                                NativeTip { host: parent; active: parent.hovered; text: "F6 开关播放列表；Ctrl + 滚轮调整条目大小" }
                            }
                            TabButton {
                                id: chatTab; objectName: "chatTab"; visible: Player.scriptEnabled
                                palette.brightText: Theme.text
                                width: Player.scriptEnabled ? sideTabs.width/2 : 0; text: "聊天"; implicitHeight: 32
                                background: Rectangle { color: chatTab.checked ? Theme.raised : Theme.background; Behavior on color { ColorAnimation { duration: Theme.motionDuration } } }
                                NativeTip { host: parent; active: parent.hovered; text: "F9 开关聊天" }
                            }
                        }
                        ToolButton { text: "×"; implicitWidth: 28; implicitHeight: 32; onClicked: root.closeSidebar() }
                    }
                    StackLayout {
                        currentIndex: Player.scriptEnabled ? sideTabs.currentIndex : 0; Layout.fillWidth: true; Layout.fillHeight: true
                        Item {
                            ListView {
                                id: playlistView; objectName: "playlistView"
                                anchors.fill: parent
                                // Keep the thumb outside the window's native resize hit area.
                                anchors.rightMargin: root.windowResizable ? root.resizeBorderWidth : 0
                                anchors.bottomMargin: (!Player.scriptEnabled ? 36 : 0) + (root.windowResizable ? root.resizeBorderWidth : 0)
                                clip: true; spacing: 0; model: Player.playlist; cacheBuffer: 240
                                boundsBehavior: Flickable.StopAtBounds
                                property bool locateAfterChange: true
                                property real savedScrollOffset: 0
                                property int modelRevision: 0
                                property bool modelChangePending: false
                                function prepareModelChange(locate) {
                                    locateAfterChange=locate
                                    if(!modelChangePending)savedScrollOffset=contentY-originY
                                    modelChangePending=true
                                    scrollMotion.stop();cancelFlick()
                                    // Equal model values may emit no onModelChanged. Do not
                                    // retain an old snapshot after such a notification.
                                    const revision=modelRevision
                                    Qt.callLater(function() { if(revision===modelRevision)modelChangePending=false })
                                }
                                function restoreModelPosition() {
                                    forceLayout()
                                    if(locateAfterChange)locatePlayingItem()
                                    else {
                                        for(let i=0;i<count;i++)if(model[i].current) { currentIndex=i;break }
                                        contentY=originY+Math.max(0,Math.min(savedScrollOffset,contentHeight-height))
                                    }
                                    modelChangePending=false
                                }
                                function locatePlayingItem() {
                                    for(let i=0;i<count;i++) {
                                        const active=model[i].current
                                        if(active === true || active === 1 || active === "1") {
                                            scrollMotion.stop();cancelFlick();currentIndex=i
                                            forceLayout();positionViewAtIndex(i,ListView.Center)
                                            return
                                        }
                                    }
                                }
                                onModelChanged: {
                                    const revision=++modelRevision
                                    Qt.callLater(function() { if(revision===modelRevision)restoreModelPosition() })
                                }
                                Component.onCompleted: Qt.callLater(locatePlayingItem)
                                property real scrollDestination: 0
                                property real lastWheelTime: 0
                                property int lastWheelDirection: 0
                                function scrollBy(angle,pixels) {
                                    if(!angle && !pixels)return
                                    const lower=originY,upper=originY+Math.max(0,contentHeight-height)
                                    const distance=pixels ? pixels : angle/120*(Player.settings.playlistItemHeight || 76)
                                    const direction=Math.sign(distance),now=Date.now()
                                    const continuing=!pixels && scrollMotion.running && direction===lastWheelDirection
                                    const start=continuing ? scrollDestination : contentY
                                    const interval=lastWheelTime && direction===lastWheelDirection ? now-lastWheelTime : 120
                                    cancelFlick()
                                    scrollMotion.stop()
                                    scrollDestination=Math.max(lower,Math.min(upper,start-distance))
                                    lastWheelDirection=direction;lastWheelTime=pixels ? 0 : now
                                    if(pixels)contentY=scrollDestination
                                    else {
                                        // Shorter wheel intervals shorten the transition; retain
                                        // every notch while responding immediately to reversal.
                                        scrollMotion.duration=Math.max(40,Math.min(120,interval))
                                        scrollMotion.from=contentY;scrollMotion.to=scrollDestination;scrollMotion.start()
                                    }
                                }
                                NumberAnimation { id: scrollMotion; target: playlistView; property: "contentY"; duration: 120; easing.type: Easing.OutCubic }
                                onDraggingChanged: if(dragging) { scrollMotion.stop();lastWheelTime=0 }
                                WheelHandler {
                                    target: null; acceptedModifiers: Qt.NoModifier
                                    onWheel: function(event) { playlistView.scrollBy(event.angleDelta.y,event.pixelDelta.y);event.accepted=true }
                                }
                                WheelHandler {
                                    target: null; acceptedModifiers: Qt.ControlModifier
                                    onWheel: function(event) {
                                        let delta=event.angleDelta.y || event.pixelDelta.y
                                        if(delta)Player.setSetting("playlistItemHeight",Math.max(56,Math.min(180,(Player.settings.playlistItemHeight || 76)+(delta>0?8:-8))))
                                        event.accepted=true
                                    }
                                }
                                ScrollBar.vertical: ScrollBar {
                                    objectName: "playlistScrollBar"; policy: ScrollBar.AlwaysOn; minimumSize: 0.06; implicitWidth: 10
                                    stepSize: (Player.settings.playlistItemHeight || 76)/Math.max(1,playlistView.contentHeight)
                                    onPressedChanged: if(pressed) { scrollMotion.stop();playlistView.cancelFlick();playlistView.lastWheelTime=0 }
                                    WheelHandler {
                                        target: null; acceptedModifiers: Qt.NoModifier
                                        onWheel: function(event) { playlistView.scrollBy(event.angleDelta.y,event.pixelDelta.y);event.accepted=true }
                                    }
                                }
                                delegate: ItemDelegate {
                                    required property var modelData
                                    required property int index
                                    readonly property real zoom: (Player.settings.playlistItemHeight || 76)/76
                                    width: ListView.view.width-12; height: Player.settings.playlistItemHeight || 76; padding: 6
                                    objectName: "playlistItem_"+index
                                    NativeTip { host: parent; active: parent.hovered; delay: 700; text: MetadataFormat.preview(modelData) }
                                    background: Rectangle {
                                        color: parent.hovered || modelData.current ? Theme.raised : "transparent"
                                        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
                                    }
                                    contentItem: RowLayout {
                                        spacing: 8
                                        Rectangle {
                                            Layout.preferredWidth: Math.min(160,88*zoom); Layout.preferredHeight: Math.min(90,50*zoom); radius: 5; color: Theme.background; clip: true
                                            Image {
                                                id: cover; objectName: "playlistThumbnail"
                                                anchors.fill: parent; source: modelData.thumbnail || ""; asynchronous: true; cache: true
                                                sourceSize: Qt.size(192,108); fillMode: Image.PreserveAspectCrop
                                            }
                                            Label { anchors.centerIn: parent; visible: cover.status !== Image.Ready; text: "▶"; color: Theme.muted; font.pixelSize: 20 }
                                            Label {
                                                objectName: "playlistDuration_"+index
                                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                                text: MetadataFormat.duration(modelData.duration); visible: text.length > 0
                                                color: "white"; font.pixelSize: Math.max(10,Math.min(14,10*zoom)); padding: 2
                                                background: Rectangle { color: "#bb000000" }
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 2
                                            Label { text: modelData.title; textFormat: Text.PlainText; color: modelData.current ? Theme.accent : Theme.text; wrapMode: Text.Wrap; maximumLineCount: zoom < 0.9 ? 1 : 2; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: Math.max(11,Math.min(18,12*zoom)) }
                                            Label { objectName: "playlistByline_"+index; text: MetadataFormat.byline(modelData,true); textFormat: Text.PlainText; visible: text.length>0; color: Theme.muted; font.pixelSize: Math.max(10,Math.min(14,10*zoom)); elide: Text.ElideRight; Layout.fillWidth: true }
                                            Label { objectName: "playlistStats_"+index; text: MetadataFormat.stats(modelData); textFormat: Text.PlainText; visible: text.length>0 && zoom>=0.9; color: Theme.muted; font.pixelSize: Math.max(10,Math.min(14,10*zoom)); elide: Text.ElideRight; Layout.fillWidth: true }
                                        }
                                        ToolButton { objectName: "playlistInfo_"+index; text: "⋯"; implicitWidth: 24; font.pixelSize: 16; onClicked: root.showMediaInfo(modelData); NativeTip { host: parent; active: parent.hovered; text: "查看完整信息" } }
                                    }
                                    onClicked: playlistView.currentIndex=index
                                    onDoubleClicked: { urlField.text=modelData.url; Player.openPlaylistItem(index) }
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton | Qt.MiddleButton
                                        onTapped: function(point,button) {
                                            if(button===Qt.MiddleButton)Player.openInNewInstance(modelData.url)
                                            else {
                                                playlistMenu.entryUrl=modelData.url
                                                const p=parent.mapToItem(root.contentItem,point.position.x,point.position.y)
                                                playlistMenu.popup(p.x,p.y)
                                            }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                visible: !Player.scriptEnabled
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: root.windowResizable ? root.resizeBorderWidth : 0; height: 36
                                ToolButton { text: "添加文件"; onClicked: mediaFile.open() }
                                ToolButton { text: "移除"; enabled: playlistView.currentIndex >= 0 && Player.playlist.length>0; onClicked: Player.removePlaylistItem(playlistView.currentIndex) }
                                ToolButton { text: "清空"; enabled: Player.playlist.length>0; onClicked: Player.clearPlaylist() }
                                Item { Layout.fillWidth: true }
                            }
                            Label { anchors.centerIn: parent; width: parent.width-32; visible: Player.playlist.length === 0; text: Player.scriptEnabled ? "播放列表由解析脚本提供。\n当前尚无播放列表。" : "点击“打开文件”添加本地媒体。\n支持一次选择多个文件，双击列表播放。"; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: Theme.muted; lineHeight: 1.5 }
                        }
                        ColumnLayout {
                            spacing: 0
                            Item {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                WindowContainer { anchors.fill: parent; anchors.rightMargin: 8; anchors.bottomMargin: 8; window: Player.browser.chatWindow; visible: Player.chatAvailable }
                                Label { anchors.centerIn: parent; width: parent.width-24; visible: !Player.chatAvailable; text: "当前内容未提供聊天页面。\n聊天页默认静音，不占字幕槽。"; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: Theme.muted }
                            }
                        }
                    }
                }
            }
        }
    }

    // Owned native tool windows keep controls above the DirectShow child HWND.
    // Their visibility changes never participate in the video area's layout.
    ApplicationWindow {
        id: floatingTitle; objectName: "floatingTitle"
        transientParent: root; flags: Qt.Tool | Qt.FramelessWindowHint
        x: root.x; y: root.y; width: root.width-(root.videoFullscreen && sidebar.visible ? sidebar.width+6 : 0); height: 46
        color: "transparent"; palette: root.palette; font: root.font
        property real reveal: root.chromeRequested ? 1 : 0
        Behavior on reveal { NumberAnimation { duration: Theme.chromeMotionDuration; easing.type: Easing.OutCubic } }
        visible: root.visible && root.floatingChrome && reveal > 0.001 && root.visibility !== Window.Minimized
        Item {
            width: parent.width; height: parent.height; y: -8*(1-floatingTitle.reveal); opacity: floatingTitle.reveal
            TitleStrip { width: parent.width; height: 32; shell: root }
            Rectangle {
                objectName: "titleShadow"; y: 32; width: parent.width; height: 14
                gradient: Gradient { GradientStop { position: 0; color: "#80000000" } GradientStop { position: 1; color: "#00000000" } }
            }
        }
        HoverHandler { onPointChanged: if(hovered) { const p=floatingTitle.contentItem.mapToGlobal(point.position.x,point.position.y);root.handlePointer(root.contentItem.mapFromGlobal(p.x,p.y)) } }
    }
    ApplicationWindow {
        id: floatingControls; objectName: "floatingControls"
        transientParent: root; flags: Qt.Tool | Qt.FramelessWindowHint
        x: root.x; y: root.y+root.height-height; width: root.width-(root.videoFullscreen && sidebar.visible ? sidebar.width+6 : 0); height: floatingBar.implicitHeight+14
        color: "transparent"; palette: root.palette; font: root.font
        property real reveal: root.chromeRequested ? 1 : 0
        Behavior on reveal { NumberAnimation { duration: Theme.chromeMotionDuration; easing.type: Easing.OutCubic } }
        visible: root.visible && root.floatingChrome && reveal > 0.001 && root.visibility !== Window.Minimized
        Item {
            width: parent.width; height: parent.height; y: 8*(1-floatingControls.reveal); opacity: floatingControls.reveal
            Rectangle {
                objectName: "controlsShadow"; width: parent.width; height: 14
                gradient: Gradient { GradientStop { position: 0; color: "#00000000" } GradientStop { position: 1; color: "#80000000" } }
            }
            PlaybackBar { id: floatingBar; y: 14; width: parent.width; height: implicitHeight; shell: root }
        }
        HoverHandler { onPointChanged: if(hovered) { const p=floatingControls.contentItem.mapToGlobal(point.position.x,point.position.y);root.handlePointer(root.contentItem.mapFromGlobal(p.x,p.y)) } }
    }
    // Frameless windows retain dragging, maximizing and native edge resizing.
    Repeater {
        model: [Qt.LeftEdge,Qt.RightEdge,Qt.TopEdge,Qt.BottomEdge,Qt.LeftEdge|Qt.TopEdge,Qt.RightEdge|Qt.TopEdge,Qt.LeftEdge|Qt.BottomEdge,Qt.RightEdge|Qt.BottomEdge]
        delegate: MouseArea {
            required property int modelData
            objectName: "windowResizeEdge"+modelData
            parent: root.contentItem
            z: 100
            hoverEnabled: true
            preventStealing: true
            readonly property bool corner: (modelData & (Qt.LeftEdge|Qt.RightEdge)) !== 0 && (modelData & (Qt.TopEdge|Qt.BottomEdge)) !== 0
            visible: root.windowResizable
            x: (modelData & Qt.RightEdge) ? root.width-width : 0
            y: (modelData & Qt.BottomEdge) ? root.height-height : 0
            width: corner || (modelData & (Qt.LeftEdge|Qt.RightEdge)) ? root.resizeBorderWidth : root.width
            height: corner || (modelData & (Qt.TopEdge|Qt.BottomEdge)) ? root.resizeBorderWidth : root.height
            cursorShape: corner ? ((modelData === (Qt.LeftEdge|Qt.TopEdge) || modelData === (Qt.RightEdge|Qt.BottomEdge)) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : ((modelData & (Qt.LeftEdge|Qt.RightEdge)) ? Qt.SizeHorCursor : Qt.SizeVerCursor)
            onPressed: function(mouse) {
                if((modelData & Qt.RightEdge) !== 0) {
                    const point=mapToGlobal(mouse.x,mouse.y)
                    root.beginRightEdgeResize(point.x,point.y,modelData)
                }else { root.beginWindowResize(modelData);if(!root.startSystemResize(modelData))root.finishWindowResize() }
            }
            onPositionChanged: function(mouse) {
                if(pressed && (modelData & Qt.RightEdge) !== 0) {
                    const point=mapToGlobal(mouse.x,mouse.y)
                    root.dragRightEdgeResize(point.x,point.y,modelData)
                }
            }
            onReleased: if((modelData & Qt.RightEdge) !== 0)root.finishWindowResize()
            onCanceled: if((modelData & Qt.RightEdge) !== 0)root.finishWindowResize()
        }
    }

    FontMetrics { id: playlistMenuFont; font: playlistMenu.font }
    StableMenu {
        id: playlistMenu; objectName: "playlistContextMenu"
        property string entryUrl: ""
        width: Math.ceil(playlistMenuFont.advanceWidth("新实例打开")+labelPadding)
        PlayerMenuItem { objectName: "openPlaylistInNewInstance"; text: "新实例打开"; onTriggered: Player.openInNewInstance(playlistMenu.entryUrl) }
    }

    StableMenu {
        id: videoMenu; objectName: "videoMenu"
        width: 290; rows: 9; separators: 2
        popupType: Popup.Window
        PlayerMenuItem { height: Theme.controlSize; text: Player.state === "已暂停" ? "继续播放 · 空格" : "暂停 · 空格"; enabled: Player.hasMedia; onTriggered: Player.togglePause() }
        MenuSeparator { height: 8 }
        PlayerMenuItem { height: Theme.controlSize; text: "仅显示视频"; checkable: true; checked: root.viewMode === "videoOnly"; onTriggered: root.setViewMode("videoOnly") }
        PlayerMenuItem { height: Theme.controlSize; text: "显示视频与内容面板"; checkable: true; checked: root.viewMode === "videoAndPanel" && root.sideVisible; onTriggered: root.setViewMode("videoAndPanel") }
        PlayerMenuItem { height: Theme.controlSize; text: root.videoFullscreen ? "退出视频全屏 · F11 / Esc" : "视频全屏 · F11 / 鼠标中键"; enabled: Player.hasMedia; onTriggered: root.toggleVideoFullscreen() }
        RepeatMenu {}
        MenuSeparator { height: 8 }
        PlayerMenuItem { height: Theme.controlSize; text: "字幕 / 音轨"; enabled: Player.hasMedia; onTriggered: tracksDialog.open() }
        PlayerMenuItem { height: Theme.controlSize; text: "打开链接 · Ctrl+L"; onTriggered: root.focusLink() }
        PlayerMenuItem { height: Theme.controlSize; text: "设置"; onTriggered: settingsDialog.open() }
        PlayerMenuItem { objectName: "diagnosticMenuItem"; height: Theme.controlSize; text: "播放诊断"; onTriggered: { videoMenu.close(); diagnosticDialog.open() } }
    }

    FileDialog { id: mediaFile; title: "打开本地媒体"; fileMode: FileDialog.OpenFiles; onAccepted: { urlField.text=selectedFiles[0].toString(); Player.openFiles(selectedFiles) } }
    FileDialog { id: subtitleFile; title: "选择文本字幕"; nameFilters: ["文本字幕 (*.ass *.ssa *.srt *.vtt)"]; onAccepted: Player.loadSubtitle(root.fileSlot,selectedFile) }
    Dialog {
        id: tracksDialog; objectName: "tracksDialog"; title: "画质、音轨与双字幕"; anchors.centerIn: parent; width: Math.min(root.width-80,720); modal: true; standardButtons: Dialog.Close
        popupType: Popup.Window; dim: false
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motionDuration } }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: "画质"; font.bold: true; color: Theme.text }
            StableComboBox {
                Layout.fillWidth: true; model: [{id:"",label:"脚本默认"}].concat(Player.qualities.filter(c => !c.audio)); textRole: "label"; valueRole: "id"
                onActivated: Player.selectQuality(currentValue)
                Component.onCompleted: currentIndex=indexOfValue(Player.selectedQuality)
            }
            Label { text: "音轨"; font.bold: true; color: Theme.text }
            StableComboBox {
                id: audioChoice; Layout.fillWidth: true
                model: [{id:"",audioLabel:"自动 · 最高码率 / 媒体内音轨"}].concat(Player.qualities.filter(c => c.audio))
                textRole: "audioLabel"; valueRole: "id"
                onActivated: Player.selectAudio(currentValue)
                function syncSelection() { currentIndex=Math.max(0,indexOfValue(Player.selectedAudio)) }
                onModelChanged: Qt.callLater(syncSelection)
                Component.onCompleted: syncSelection()
                Connections { target: Player; function onChanged() { Qt.callLater(audioChoice.syncSelection) } }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.raised }
            Repeater {
                model: 2
                delegate: ColumnLayout {
                    id: subtitleRow
                    required property int index
                    Layout.fillWidth: true; spacing: 8
                    Label { text: index === 0 ? "主字幕 · 底部" : "次字幕 · 顶部 / 点播弹幕"; color: Theme.text; font.bold: true }
                    RowLayout {
                        StableComboBox {
                            id: subtitleChoice; Layout.fillWidth: true; model: Player.subtitles.length ? Player.subtitles : [{id:"",label:"关闭"}]; textRole: "label"; valueRole: "id"
                            onActivated: Player.setSubtitle(subtitleRow.index,currentValue,offset.value/10,danmaku.checked)
                            function sync() { currentIndex=Math.max(0,indexOfValue(subtitleRow.index === 0 ? Player.selectedMainSubtitle : Player.selectedSecondarySubtitle)) }
                            onModelChanged: Qt.callLater(sync)
                            Component.onCompleted: sync()
                            Connections { target: Player; function onSubtitleSelectionChanged() { subtitleChoice.sync() } }
                        }
                        Button { text: "外置文件"; onClicked: { root.fileSlot=subtitleRow.index; subtitleFile.open() } }
                    }
                    RowLayout {
                        Label { text: "延迟（秒）"; color: Theme.muted }
                        SpinBox { id: offset; from: -6000; to: 6000; value: 0; editable: true; textFromValue: function(v){return (v/10).toFixed(1)}; valueFromText:function(t){return Math.round(Number(t)*10)}; onValueModified: Player.adjustSubtitle(subtitleRow.index,value/10,danmaku.checked) }
                        PlayerCheckBox { id: danmaku; visible: subtitleRow.index === 1; text: "此轨道用作 ASS 弹幕"; onToggled: Player.adjustSubtitle(subtitleRow.index,offset.value/10,checked) }
                    }
                }
            }
            Label { text: "次字幕与点播 ASS 弹幕共用一个槽。聊天侧栏独立显示。"; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    SettingsWindow { id: settingsDialog; shell: root }
    MediaInfoWindow { id: mediaInfoWindow; shell: root }
    ScriptDebugWindow { id: scriptDebugWindow; shell: root }
    DiagnosticWindow { id: diagnosticDialog; shell: root }
    Dialog {
        id: scriptMessage; anchors.centerIn: parent; width: Math.min(root.width-80,640); modal: true
        popupType: Popup.Window; dim: false
        property int buttonType: 1
        property string bodyText: ""
        contentItem: Label { text: scriptMessage.bodyText; color: Theme.text; wrapMode: Text.Wrap }
        footer: DialogButtonBox {
            Button { text: "确认"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { Player.answerMessage(root.messageId,1); scriptMessage.close() } }
            Button { text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { Player.answerMessage(root.messageId,3); scriptMessage.close() } }
        }
        onRejected: Player.answerMessage(root.messageId,3)
        onClosed: Qt.callLater(root.showNextMessage)
    }
    Connections {
        target: Player
        function onPlaylistAboutToChange(locateCurrent) { playlistView.prepareModelChange(locateCurrent) }
        function onSettingsChanged() {
            if(!Player.scriptEnabled)sideTabs.currentIndex=0
            // Finish Qt's interactive check-state transition before syncing
            // every menu instance from the persisted visibility preferences.
            Qt.callLater(root.syncSubtitleActions)
        }
        function onVideoResizeRequested(edges) { root.beginWindowResize(edges);if(!root.startSystemResize(edges))root.finishWindowResize() }
        function onWindowResizeStarted(edges) { root.beginWindowResize(edges) }
        function onWindowResizeFinished() { root.finishWindowResize() }
        function onVideoPointerMoved(position) { root.handlePointer(position) }
        function onVideoFullscreenRequested() { root.toggleVideoFullscreen() }
        function onVideoEscapeRequested() { root.escapeVideoView() }
        function onVideoLayoutRequested() { root.toggleViewMode() }
        function onVideoLinkRequested() { root.focusLink() }
        function onSidebarPageRequested(page) { root.toggleSidebarPage(page) }
        function onVideoMenuRequested(position) { videoMenu.popup(position.x,position.y) }
        function onAskMessage(id,title,message,buttons) { root.messageQueue.push({id:id,title:title,body:message,buttons:buttons});root.showNextMessage() }
        function onScriptMessagesCancelled() { root.messageQueue=[];scriptMessage.close() }
    }
}
