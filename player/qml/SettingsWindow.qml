import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SimplePlayer

ApplicationWindow {
    id: settingsWindow
    required property var shell
    objectName: "settingsDialog"
    title: "播放器设置"; width: 900; height: 720
    minimumWidth: 680; minimumHeight: 440
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    transientParent: shell; modality: Qt.WindowModal; visible: false
    readonly property bool opened: visible
    color: Theme.background; palette: shell.palette; font: shell.font
    function open() { x=shell.x+(shell.width-width)/2;y=Math.max(0,shell.y+(shell.height-height)/2);show();requestActivate() }
    Shortcut { sequence: "Escape"; onActivated: settingsWindow.close() }
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; clip: true
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        ColumnLayout {
            width: settingsWindow.width-36; spacing: 12
            RowLayout {
                Label { text: "外观"; color: Theme.text; Layout.fillWidth: true }
                StableComboBox { objectName: "themeSelector"; model:["深色","浅色"]; currentIndex: Player.settings.theme === "light" ? 1 : 0; onActivated: Player.setSetting("theme",currentIndex === 1 ? "light" : "dark") }
            }
            RowLayout {
                Label { text: "播放器窗口宽度（px）"; color: Theme.text; Layout.fillWidth: true }
                SpinBox { from: shell.minimumWidth; to: Math.max(from,shell.screen.width); value: shell.width; editable: true; onValueModified: shell.setWindowWidth(value) }
            }
            RowLayout {
                Label { text: "播放器窗口高度（px）"; color: Theme.text; Layout.fillWidth: true }
                SpinBox { objectName: "windowHeightSetting"; from: shell.minimumHeight; to: Math.max(from,shell.screen.height); value: shell.height; editable: true; onValueModified: shell.setWindowHeight(value) }
            }
            PlayerCheckBox { objectName: "scriptEnabledSetting"; text: "使用 AngelScript（关闭后作为本地播放器）"; checked: Player.scriptEnabled; onToggled: Player.setSetting("scriptEnabled",checked) }
            Button { text: "打开 AngelScript 调试窗口"; onClicked: shell.showScriptDebug() }
            PlayerCheckBox { text: "启用用户安装的 Bluesky FRC（连接失败时回退普通播放）"; checked: Player.settings.bfrcEnabled || false; onToggled: Player.setSetting("bfrcEnabled",checked) }
            PlayerCheckBox { text: "启用脚本播放统计 / 历史同步"; enabled: Player.scriptEnabled; checked: Player.settings.statisticsEnabled || false; onToggled: Player.setSetting("statisticsEnabled",checked) }
            Button { text: "打开脚本配置文件"; enabled: Player.scriptEnabled; onClicked: Player.openScriptConfig() }
            Label { text: "依赖位置"; color: Theme.text; font.bold: true }
            Repeater {
                model: [
                    {key:"mediaScript",label:"Bilibili 解析脚本 (.as)",filters:["AngelScript (*.as)"]},
                    {key:"statisticsScript",label:"播放统计脚本 (.as)",filters:["AngelScript (*.as)"]},
                    {key:"lavDirectory",label:"LAV x64 文件夹",folder:true},
                    {key:"rendererPath",label:"MPC Video Renderer x64 (.ax)",filters:["DirectShow (*.ax)"]},
                    {key:"audioRendererPath",label:"WASAPI 音频 renderer (.ax)",filters:["DirectShow (*.ax)"]},
                    {key:"bfrcPath",label:"Bluesky FRC x64 (.dll)",filters:["动态库 (*.dll)"]},
                    {key:"ffmpegPath",label:"直播回看 FFmpeg (.exe)",filters:["程序 (*.exe)"]},
                    {key:"themePath",label:"可选 theme.json",filters:["主题 (*.json)"]}
                ]
                delegate: PathEntry { required property var modelData; spec: modelData }
            }
            Button { text: "退出登录并清除全局浏览器数据"; visible: Player.scriptEnabled; onClicked: Player.logout() }
        }
    }
    footer: ToolBar {
        background: Rectangle { color: Theme.background }
        implicitHeight: 48
        RowLayout { anchors.fill: parent; anchors.margins: 8; Item { Layout.fillWidth: true } Button { text: "关闭"; Layout.preferredWidth: 100; onClicked: settingsWindow.close() } }
    }
}
