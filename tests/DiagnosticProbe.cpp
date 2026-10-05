#include "DiagnosticProbe.h"
#include "app/PlayerController.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QColor>
#include <cmath>
#include <iostream>

static QQuickItem* item(QQuickItem* root,const QString& name) {
    if(!root)return nullptr;
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* match=item(child,name))return match;
    return nullptr;
}
void startDiagnosticProbe(bp::PlayerController& player,QQuickWindow& window,QJsonArray& report,QString dataDirectory) {
    auto* timer=new QTimer(&player);timer->setInterval(250);
    QObject::connect(timer,&QTimer::timeout,&player,[&player,&window,&report,dataDirectory,timer,stage=0,start=0.0,serial=qulonglong(0)]() mutable {
        auto note=[&](QString check,bool ok){report.append(QJsonObject{{"check",check},{"passed",ok}});std::cerr<<"Diagnostic check: "<<check.toStdString()<<" "<<(ok?"passed":"FAILED")<<'\n';};
        auto* diagnostic=window.findChild<QQuickWindow*>("diagnosticDialog");
        auto* body=diagnostic?item(diagnostic->contentItem(),"diagnosticText"):nullptr;
        auto button=[&](QString name){auto* target=diagnostic?item(diagnostic->contentItem(),name):nullptr;if(target)QMetaObject::invokeMethod(target,"clicked");};
        auto open=[&]{auto* menu=window.findChild<QObject*>("videoMenu");QMetaObject::invokeMethod(menu,"open");QMetaObject::invokeMethod(menu->findChild<QObject*>("diagnosticMenuItem"),"triggered");};
        auto text=[&]{return body?body->property("text").toString():QString();};
        auto bounds=[&]{
            auto* scroll=diagnostic?item(diagnostic->contentItem(),"diagnosticScrollView"):nullptr;
            auto* close=diagnostic?item(diagnostic->contentItem(),"closeDiagnostics"):nullptr;
            return scroll && close && scroll->height()>100 && scroll->width()>450 && scroll->mapToScene({0,scroll->height()}).y()<=close->mapToScene({0,0}).y() && close->mapToScene({close->width(),close->height()}).x()<=diagnostic->width() && close->mapToScene({0,close->height()}).y()<=diagnostic->height();
        };
        if(stage==0){
            if(player.state()!="播放中" || player.diagnostics().value("rendererFramesDrawn").toInt()<1)return;
            start=player.position();serial=player.diagnostics().value("graphOpenSerial").toULongLong();open();++stage;
        }else if(stage==1){
            note("diagnosticMenuClosesBeforeWindow",!window.findChild<QObject*>("videoMenu")->property("opened").toBool());
            note("diagnosticNativeWindowGeometry",diagnostic && diagnostic->isVisible() && diagnostic->width()==760 && diagnostic->height()==540 && diagnostic->transientParent()==&window && diagnostic->modality()==Qt::NonModal);
            const auto data=QJsonDocument::fromJson(text().toUtf8()).object();
            note("diagnosticShowsEngineData",data.value("videoConnected").toBool() && data.value("audioConnected").toBool() && data.value("state")=="播放中");
            note("diagnosticSelectablePlainText",body && body->property("readOnly").toBool() && body->property("selectByMouse").toBool() && body->property("textFormat").toInt()==0);
            note("diagnosticsHaveScrollAndEditMenu",bounds() && body && body->findChild<QObject*>("textEditMenu"));
            note("diagnosticKeepsVideoAndSidebar",player.videoWindow()->isVisible() && item(window.contentItem(),"contentSidebar")->isVisible() && player.state()=="播放中");
            button("exportDiagnostics");++stage;
        }else if(stage==2){
            QFile file(dataDirectory+"/diagnostics.json");const bool readable=file.open(QIODevice::ReadOnly);const auto data=QJsonDocument::fromJson(file.readAll()).object();file.close();
            note("diagnosticExportsValidJSON",readable && data.value("applicationVersion")=="0.1.0" && data.value("videoConnected").toBool() && data.value("audioConnected").toBool() && data.value("state")=="播放中");
            auto* status=item(diagnostic->contentItem(),"diagnosticExportStatus");
            note("diagnosticExportFeedbackShowsPath",status && status->isVisible() && status->property("text").toString().contains(QDir::toNativeSeparators(dataDirectory+"/diagnostics.json")));
            auto* saved=new QMimeData;const auto* original=QGuiApplication::clipboard()->mimeData();if(original)for(const auto& format:original->formats())saved->setData(format,original->data(format));
            button("copyDiagnostics");note("diagnosticCopiesAllText",!text().isEmpty() && QGuiApplication::clipboard()->text()==text());QGuiApplication::clipboard()->setMimeData(saved);
            note("diagnosticPlaybackContinues",player.position()>start+.02 && serial==player.diagnostics().value("graphOpenSerial").toULongLong());
            diagnostic->grabWindow().save(dataDirectory+"/diagnostic-dark.png");diagnostic->resize(520,340);++stage;
        }else if(stage==3){
            note("diagnosticSmallWindowKeepsContentAndButtons",bounds());player.setSetting("theme","light");++stage;
        }else if(stage==4){
            note("diagnosticLightTheme",diagnostic->color()==QColor(player.theme().value("background").toString()) && body->property("color").value<QColor>()==QColor(player.theme().value("text").toString()));
            diagnostic->grabWindow().save(dataDirectory+"/diagnostic-light.png");player.setSetting("theme","dark");++stage;
        }else if(stage==5){
            note("diagnosticDarkTheme",diagnostic->color()==QColor(player.theme().value("background").toString()) && body->property("color").value<QColor>()==QColor(player.theme().value("text").toString()));player.togglePause();++stage;
        }else if(stage==6){
            if(player.state()!="已暂停")return;
            button("refreshDiagnostics");note("diagnosticRefreshShowsLatestState",QJsonDocument::fromJson(text().toUtf8()).object().value("state")=="已暂停");
            player.togglePause();diagnostic->close();QMetaObject::invokeMethod(&window,"toggleVideoFullscreen");++stage;
        }else if(stage==7){
            if(player.state()!="播放中")return;
            open();++stage;
        }else if(stage==8){
            note("diagnosticOpensInFullscreen",window.visibility()==QWindow::FullScreen && diagnostic->isVisible() && diagnostic->transientParent()==&window && player.videoWindow()->isVisible());
            note("diagnosticFullscreenContentVisible",bounds() && QJsonDocument::fromJson(text().toUtf8()).object().value("state")=="播放中");
            const auto path=dataDirectory+"/diagnostics.json",backup=path+".saved";
            const bool moved=QFile::rename(path,backup),blocked=moved && QDir().mkdir(path);
            if(blocked)button("exportDiagnostics");
            note("diagnosticExportFailureExplained",blocked && diagnostic->property("exportFailed").toBool() && diagnostic->property("exportMessage").toString().contains("诊断导出失败") && player.error().isEmpty());
            const bool unblocked=!blocked || QDir().rmdir(path),restored=moved && QFile::rename(backup,path);
            note("diagnosticExportFixtureRestored",unblocked && restored);
            button("closeDiagnostics");QMetaObject::invokeMethod(&window,"revealChrome");++stage;
        }else if(stage==9){
            auto* controls=window.findChild<QWindow*>("floatingControls");
            note("diagnosticCloseRestoresFullscreenControls",!diagnostic->isVisible() && controls && controls->isVisible() && player.state()=="播放中" && serial==player.diagnostics().value("graphOpenSerial").toULongLong());
            QMetaObject::invokeMethod(&window,"toggleVideoFullscreen");++stage;
        }else if(stage==10){
            note("diagnosticReturnsToNormalLayout",window.visibility()==QWindow::Windowed && player.videoWindow()->isVisible() && item(window.contentItem(),"contentSidebar")->isVisible());player.stop();open();++stage;
        }else if(stage==11){
            note("diagnosticWorksWithoutMedia",diagnostic->isVisible() && bounds() && QJsonDocument::fromJson(text().toUtf8()).object().value("state")=="就绪");diagnostic->close();timer->stop();
        }
    });timer->start();
}
