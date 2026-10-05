#include "FinalFeaturesProbe.h"
#include "app/PlayerController.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QTimer>
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <cmath>

static QQuickItem* item(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* found=item(child,name))return found;
    return nullptr;
}
static void click(QQuickItem* target) {
    if(!target || !target->window())return;
    auto* window=target->window();const auto p=target->mapToScene(QPointF(target->width()/2,target->height()/2));
    QMouseEvent press(QEvent::MouseButtonPress,p,window->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,p,window->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&release);
}
void startFinalFeaturesProbe(bp::PlayerController& player,QQuickWindow& window,QJsonArray& report,QString dataDirectory) {
    auto* timer=new QTimer(&player);timer->setInterval(200);
    QObject::connect(timer,&QTimer::timeout,&player,[&player,&window,&report,dataDirectory,timer,stage=0,ticks=0,loops=0,last=0.0,serial=qulonglong(0),pausePosition=0.0,frames=0,primary=qulonglong(0),secondary=qulonglong(0),originalWidth=0]() mutable {
        auto note=[&](QString name,bool passed){report.append(QJsonObject{{"check",name},{"passed",passed}});};
        auto* sidebar=item(window.contentItem(),"contentSidebar");
        auto* rightMenu=window.findChild<QObject*>("videoMenu");
        auto* rightRepeat=rightMenu->findChild<QObject*>("repeatMenu");
        auto* button=item(window.contentItem(),"repeatButton");
        auto* barRepeat=button?button->findChild<QObject*>("repeatMenu"):nullptr;
        auto selected=[&](QObject* menu,bool one){return menu && menu->findChild<QObject*>("repeatOne")->property("checked").toBool()==one && menu->findChild<QObject*>("repeatNone")->property("checked").toBool()!=one;};
        auto hover=[&](double x,int y=-1){
            auto* host=player.videoWindow();const auto global=window.mapToGlobal(QPoint(int(x),y<0?window.height()/2:y));
            QMouseEvent move(QEvent::MouseMove,host->mapFromGlobal(global),global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(host,&move);
        };
        auto invoke=[&](const char* method){QMetaObject::invokeMethod(&window,method);};
        auto modeOne=[&]{return player.settings().value("loopMode").toString()=="one";};
        const auto p=player.position();
        if(stage==0) {
            if(player.state()!="播放完毕")return;
            note("defaultNoRepeatCompletes",!modeOne() && selected(barRepeat,false) && selected(rightRepeat,false));
            serial=player.diagnostics().value("graphOpenSerial").toULongLong();
            click(button);++stage;
        }else if(stage==1) {
            note("bottomRepeatPopupOpened",barRepeat && barRepeat->property("opened").toBool());
            click(qobject_cast<QQuickItem*>(barRepeat->findChild<QObject*>("repeatOne")));
            note("bottomRepeatSelectsOne",modeOne() && selected(barRepeat,true) && selected(rightRepeat,true));
            player.loadSubtitle(0,QUrl::fromLocalFile(dataDirectory+"/repeat-primary.srt"));
            player.loadSubtitle(1,QUrl::fromLocalFile(dataDirectory+"/repeat-secondary.srt"));
            player.togglePause();last=0;loops=0;++stage;
        }else if(stage==2) {
            if(last>p+.5)++loops;last=p;
            if(loops<2)return;
            note("repeatedEndRestartsWithoutGraphRebuild",player.state()=="播放中" && serial==player.diagnostics().value("graphOpenSerial").toULongLong());
            note("repeatKeepsRendering",player.diagnostics().value("rendererFramesDrawn").toInt()>0);
            note("repeatPreservesDualSubtitles",player.diagnostics().value("primarySubtitleFrames").toULongLong()>0 && player.diagnostics().value("secondarySubtitleFrames").toULongLong()>0 && player.selectedMainSubtitle().startsWith("external:") && player.selectedSecondarySubtitle().startsWith("external:"));
            player.togglePause();ticks=0;++stage;
        }else if(stage==3) {
            if(player.state()!="已暂停")return;
            pausePosition=p;ticks=0;++stage;
        }else if(stage==4) {
            if(++ticks<4)return;
            note("repeatDoesNotResumePausedMedia",player.state()=="已暂停" && std::abs(p-pausePosition)<.2);
            player.seek(player.duration()-.25);
            note("seekImmediatelyKeepsTarget",player.seeking() && std::abs(player.position()-player.duration()+.25)<.01);
            ticks=0;++stage;
        }else if(stage==5) {
            if(player.state()!="已暂停" || p<player.duration()-.6)return;
            QMetaObject::invokeMethod(rightMenu,"open");++stage;
        }else if(stage==6) {
            QMetaObject::invokeMethod(rightRepeat,"open");++stage;
        }else if(stage==7) {
            note("rightRepeatSubmenuOpened",rightRepeat->property("opened").toBool());
            click(qobject_cast<QQuickItem*>(rightRepeat->findChild<QObject*>("repeatNone")));
            QMetaObject::invokeMethod(rightMenu,"close");
            note("rightRepeatSelectsNoneAndSyncsBottom",!modeOne() && selected(barRepeat,false) && selected(rightRepeat,false));
            player.togglePause();++stage;
        }else if(stage==8) {
            if(player.state()!="播放完毕")return;
            note("disablingRepeatStopsAtEnd",p>=player.duration()-.3);
            player.setSetting("loopMode","one");player.setSetting("loopMode","invalid");
            note("invalidRepeatModeRejected",modeOne());
            player.seek(0);++stage;
        }else if(stage==9) {
            if(player.state()!="播放中")return;
            QMetaObject::invokeMethod(&window,"setSidebarVisible",Q_ARG(QVariant,false));ticks=0;++stage;
        }else if(stage==10) {
            if(++ticks<3)return;
            originalWidth=window.width();invoke("toggleVideoFullscreen");++stage;
        }else if(stage==11) {
            note("fullscreenStartsWithoutSidebar",window.visibility()==QWindow::FullScreen && !sidebar->isVisible());
            hover(window.width()-2,window.height()-20);++stage;
        }else if(stage==12) {
            note("bottomRightHoverDoesNotOpenSidebar",!sidebar->isVisible());
            hover(window.width()-2,12);++stage;
        }else if(stage==13) {
            note("topRightHoverDoesNotOpenSidebar",!sidebar->isVisible());
            invoke("toggleSidebar");QCoreApplication::processEvents();
            note("fullscreenButtonOpensSidebar",sidebar->isVisible() && std::abs(sidebar->width()-480)<1);
            note("fullscreenDockPreservesSettings",player.settings().value("chatVisible").toBool()==false);
            note("floatingControlsLeaveSidebarUncovered",window.findChild<QWindow*>("floatingControls")->width()==window.width()-486);
            auto* title=window.findChild<QWindow*>("floatingTitle");auto* controls=window.findChild<QWindow*>("floatingControls");
            note("floatingChromeHasEdgeShadows",title->findChild<QObject*>("titleShadow") && controls->findChild<QObject*>("controlsShadow"));
            ticks=0;++stage;
        }else if(stage==14) {
            if(++ticks<14)return;
            note("manualSidebarRemainsOpen",sidebar->isVisible());
            hover(window.width()-481);ticks=0;++stage;
        }else if(stage==15) {
            if(++ticks<2)return;
            note("pointerDoesNotCloseManualSidebar",sidebar->isVisible());
            invoke("closeSidebar");player.setSetting("chatWidth",360);ticks=0;++stage;
        }else if(stage==16) {
            if(ticks++==0){window.setProperty("overlayVisible",false);return;}
            auto* title=window.findChild<QWindow*>("floatingTitle");auto* controls=window.findChild<QWindow*>("floatingControls");
            note("floatingChromeHidesWithin200ms",!title->isVisible() && !controls->isVisible());
            note("changedSidebarWidthDoesNotEnableHover",!sidebar->isVisible());hover(window.width()-359);++stage;
        }else if(stage==17) {
            auto* title=window.findChild<QWindow*>("floatingTitle");auto* controls=window.findChild<QWindow*>("floatingControls");
            note("floatingChromeReappearsWithin200ms",title->isVisible() && controls->isVisible() && controls->property("reveal").toDouble()>.99);
            invoke("toggleSidebar");QCoreApplication::processEvents();
            note("buttonUsesConfiguredSidebarWidth",sidebar->isVisible() && std::abs(sidebar->width()-360)<1);
            invoke("closeSidebar");hover(window.width()-200);++stage;
        }else if(stage==18) {
            note("closeDoesNotReopenOnHover",!sidebar->isVisible());hover(200);hover(window.width()-200);++stage;
        }else if(stage==19) {
            note("reenterRightZoneKeepsSidebarClosed",!sidebar->isVisible());
            invoke("toggleSidebar");
            frames=player.diagnostics().value("rendererFramesDrawn").toInt();
            primary=player.diagnostics().value("primarySubtitleFrames").toULongLong();secondary=player.diagnostics().value("secondarySubtitleFrames").toULongLong();
            invoke("toggleVideoFullscreen");ticks=0;++stage;
        }else if(stage==20) {
            if(++ticks<3)return;
            note("exitFullscreenRestoresWindowAndSidebar",window.visibility()==QWindow::Windowed && window.width()==originalWidth && !sidebar->isVisible());
            note("fullscreenTransitionsRetainMediaGraph",serial==player.diagnostics().value("graphOpenSerial").toULongLong() && player.diagnostics().value("rendererFramesDrawn").toInt()>frames);
            note("fullscreenSidebarRetainsDualSubtitles",player.diagnostics().value("primarySubtitleFrames").toULongLong()>primary && player.diagnostics().value("secondarySubtitleFrames").toULongLong()>secondary);
            player.setSetting("chatWidth",480);QMetaObject::invokeMethod(&window,"setSidebarVisible",Q_ARG(QVariant,true));++stage;
        }else if(stage==21) {
            invoke("toggleVideoFullscreen");hover(200);ticks=0;++stage;
        }else if(stage==22) {
            if(++ticks<2)return;
            note("normallyDockedSidebarHiddenInFullscreen",!sidebar->isVisible());invoke("toggleVideoFullscreen");++stage;
        }else if(stage==23) {
            note("exitRestoresPreviouslyDockedSidebar",sidebar->isVisible() && player.settings().value("chatVisible").toBool());
            player.stop();ticks=0;++stage;
        }else if(stage==24) {
            if(++ticks<3)return;
            note("repeatNeverRestartsAfterStop",player.state()=="就绪" && !player.hasMedia());
            QFile settings(dataDirectory+"/settings.json");
            note("repeatSettingPersisted",settings.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(settings.readAll()).object().value("loopMode")=="one");
            // Release the Windows read handle before QSaveFile replaces this file.
            settings.close();
            player.setSetting("loopMode","none");timer->stop();
        }
    });timer->start();
}
