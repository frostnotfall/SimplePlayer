#include "PlayerController.h"
#include "SmokeProbe.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QNetworkProxy>
#include <iostream>
#include <string_view>

int main(int argc,char** argv) {
    static_assert(sizeof(void*)==8);
    // Clear inherited proxies before Qt's platform/network initialization can cache them.
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--direct-network") {
        for(auto name:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})qunsetenv(name);
        break;
    }
    auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;
    {
        QGuiApplication app(argc,argv);app.setApplicationName("SimplePlayer");app.setApplicationVersion("0.1.0");app.setOrganizationName("SimplePlayer");
        QQuickStyle::setStyle("Basic");
        QCommandLineParser parser;parser.setApplicationDescription("SimplePlayer · 原生在线播放器");parser.addHelpOption();parser.addVersionOption();
        parser.addOption({"data-dir","便携数据目录","path",QCoreApplication::applicationDirPath()+"/data"});
        parser.addOption({"direct-network","仅本进程直连，不读取代理环境变量或系统代理"});
        parser.addOption({"subtitle-scroll-exercise","验证字幕勾选、反复全屏和列表滚动"});
        parser.addOption({"live-exercise","验证直播缓存回看、暂停缓存和追赶直播"});
        parser.addOption({"playlist-focus-exercise","验证重新解析后的播放列表自动定位"});
        parser.addOption({"final-features-exercise","验证循环播放和全屏侧栏"});
        parser.addOption({"playback-controls-exercise","验证播放中 URL 编辑、剪贴板、倍速和页签快捷键"});
        parser.addOption({"diagnostic-exercise","验证播放诊断窗口、刷新、复制、导出与全屏"});
        parser.addOption({"open","启动时打开链接或文件","url"});parser.addOption({"smoke-test","导出界面截图与状态后退出"});parser.addOption({"smoke-seconds","验证时长","seconds","5"});
        parser.addOption({"screenshot","验证截图路径","path"});parser.addOption({"exercise","烟雾测试中执行暂停、seek、切图、停止及重开"});parser.addOption({"ui-exercise","烟雾测试中验证侧栏拖动及原生视频上方的设置、字幕弹窗"});parser.addOption({"subtitle-main","启动时选择主字幕文件","path"});parser.addOption({"subtitle-secondary","启动时选择次字幕文件","path"});parser.addOption({"embedded-main","启动时选择容器内嵌文本字幕"});parser.addOption({"window-exercise","验证窗口尺寸保存、向外停靠和窗口按钮"});parser.addOption({"subtitle-exercise","验证字幕元数据、显示开关和本地播放器"});parser.addOption({"local-fixture","本地播放验证文件","path"});parser.process(app);
        if(parser.isSet("smoke-test"))qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){std::cerr<<message.toUtf8().constData()<<'\n';});
        if(parser.isSet("direct-network")) {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        try {
            bp::PlayerController player(parser.value("data-dir"),parser.isSet("smoke-test"));QQmlApplicationEngine engine;
            qmlRegisterUncreatableType<bp::BrowserSession>("SimplePlayer",1,0,"BrowserSession","由播放器创建");
            engine.rootContext()->setContextProperty("Player",&player);
            engine.loadFromModule("SimplePlayer","Main");
            if(engine.rootObjects().isEmpty())throw bp::Error("UI","界面无法加载");
            result=runPlayerEventLoop(app,parser,player,engine);
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    }
    if(SUCCEEDED(apartment))CoUninitialize();return result;
}
