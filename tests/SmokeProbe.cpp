#include "SmokeProbe.h"
#include "app/PlayerController.h"
#include "FinalFeaturesProbe.h"
#include "PlaybackControlsProbe.h"
#include "DiagnosticProbe.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFontMetricsF>
#include <QColor>
#include <QStyleHints>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QSaveFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkProxy>
#include <iostream>
#include <string_view>
#include <cmath>

static QQuickItem* visualItem(QQuickItem* root,const QString& name){
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* match=visualItem(child,name))return match;
    return nullptr;
}
int runPlayerEventLoop(QGuiApplication& app,const QCommandLineParser& parser,bp::PlayerController& player,QQmlApplicationEngine& engine) {
    QJsonArray uiVerification;
    auto* mainWindow=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* sideItem=mainWindow->findChild<QQuickItem*>("contentSidebar");
    auto* settingsPopup=mainWindow->findChild<QObject*>("settingsDialog");
    auto* tracksPopup=mainWindow->findChild<QObject*>("tracksDialog");
    if(parser.isSet("final-features-exercise") && parser.isSet("smoke-test"))startFinalFeaturesProbe(player,*mainWindow,uiVerification,parser.value("data-dir"));
    if(parser.isSet("playback-controls-exercise") && parser.isSet("smoke-test"))startPlaybackControlsProbe(player,*mainWindow,uiVerification);
    if(parser.isSet("diagnostic-exercise") && parser.isSet("smoke-test"))startDiagnosticProbe(player,*mainWindow,uiVerification,parser.value("data-dir"));
    if(parser.isSet("playlist-focus-exercise") && parser.isSet("smoke-test")){
        auto childPid=std::make_shared<qint64>(0);
        QObject::connect(&player,&bp::PlayerController::instanceOpened,&player,[childPid](qint64 pid,const QString&){*childPid=pid;});
        auto* timer=new QTimer(&app);timer->setInterval(200);
        QObject::connect(timer,&QTimer::timeout,&player,[&,timer,childPid,stage=0,serial=qulonglong(0),savedY=0.0]() mutable {
            auto* list=visualItem(mainWindow->contentItem(),"playlistView");if(!list || !player.hasMedia())return;
            auto note=[&](QString check,bool ok){uiVerification.append(QJsonObject{{"check",check},{"passed",ok}});};
            auto click=[&](int index,Qt::MouseButton button,bool doubleClick=false){
                auto* item=visualItem(list,"playlistItem_"+QString::number(index));if(!item)return false;
                const auto point=item->mapToScene({item->width()/2,item->height()/2});
                const auto local=list->mapFromScene(point);if(local.y()<0 || local.y()>=list->height())return false;
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
                    QMouseEvent event(type,point,mainWindow->mapToGlobal(point.toPoint()),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&event);
                }
                if(doubleClick)for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}){
                    QMouseEvent event(type,point,mainWindow->mapToGlobal(point.toPoint()),button,type!=QEvent::MouseButtonRelease?button:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&event);
                }
                return true;
            };
            if(stage==0){
                const int index=list->property("currentIndex").toInt();const double y=list->property("contentY").toDouble();
                note("currentStringOneLocated",index==40 && y>1000);
                note("playlistParsedByDefault",player.playlist().size()==60);
                note("playlistTabDefault",mainWindow->findChild<QObject*>("sideTabs")->property("currentIndex").toInt()==0);
                note("parsePlaylistOptionRemoved",!mainWindow->findChild<QObject*>("parsePlaylist"));
                auto information=player.mediaInformation();auto entry=player.playlist()[40].toMap();
                note("mediaMetadataPreserved",information["author"].toString()=="@测试作者" && information["viewCount"].toString()=="32001" && information["dislikeCount"].toString()=="0" && information["duration"].toString()=="93123");
                note("playlistMetadataPreserved",entry["date"].toString()=="2026-10-05 12:30:00" && entry["duration"].toString()=="93123" && entry["content"].toString().contains("完整列表简介"));
                auto* summary=visualItem(mainWindow->contentItem(),"mediaSummary");
                note("mediaSummaryVisible",summary && summary->isVisible() && summary->property("text").toString().contains("播放 3.2万") && summary->property("text").toString().contains("@测试作者"));
                auto* video=visualItem(mainWindow->contentItem(),"videoArea");auto* timeline=visualItem(mainWindow->contentItem(),"playbackTimeline");
                note("mediaSummaryBelowVideoAboveTimeline",summary && video && timeline && summary->mapToScene({0,0}).y()>=video->mapToScene({0,video->height()}).y()-1 && summary->mapToScene({0,summary->height()}).y()<=timeline->mapToScene({0,0}).y()+1);
                auto* duration=visualItem(list,"playlistDuration_40");auto* stats=visualItem(list,"playlistStats_40");auto* byline=visualItem(list,"playlistByline_40");
                note("playlistDurationAndStatsVisible",duration && duration->isVisible() && duration->property("text").toString()=="01:33" && stats && stats->isVisible() && stats->property("text").toString().contains("踩 0"));
                note("playingItemMetadataWithoutRedundantStatus",byline && !byline->property("text").toString().contains("正在播放") && byline->property("text").toString().contains("@测试作者") && byline->property("text").toString().contains("2026-10-05"));
                mainWindow->grabWindow().save(".local/media-playlist.png");
                QMetaObject::invokeMethod(mainWindow,"showMediaInfo",Q_ARG(QVariant,QVariant(information)));stage=-1;
            }else if(stage==-1){
                auto* info=mainWindow->findChild<QQuickWindow*>("mediaInfoWindow");auto* body=info?visualItem(info->contentItem(),"mediaInfoText"):nullptr;
                note("mediaInfoContainsCompleteData",body && body->property("text").toString().contains("完整媒体简介") && body->property("text").toString().contains("32001") && body->property("text").toString().contains("<b>普通文字</b>") && body->property("text").toString().contains("https://fixture.test/item/40"));
                note("mediaInfoSelectablePlainText",body && body->property("readOnly").toBool() && body->property("selectByMouse").toBool() && body->property("textFormat").toInt()==0);
                note("mediaInfoKeepsPlaybackAndSidebar",info && info->isVisible() && player.videoWindow()->isVisible() && sideItem->isVisible() && player.state()=="播放中");
                auto* savedClipboard=new QMimeData;
                const auto* original=QGuiApplication::clipboard()->mimeData();
                if(original)for(const auto& format:original->formats())savedClipboard->setData(format,original->data(format));
                auto* copy=info?visualItem(info->contentItem(),"copyMediaInformation"):nullptr;
                if(copy)QMetaObject::invokeMethod(copy,"clicked");
                note("mediaInfoCopiesCompleteText",body && QGuiApplication::clipboard()->text()==body->property("text").toString());
                QGuiApplication::clipboard()->setMimeData(savedClipboard);
                if(info){info->grabWindow().save(".local/media-information.png");info->close();}
                QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");QMetaObject::invokeMethod(mainWindow,"revealChrome");stage=-2;
            }else if(stage==-2){
                auto* controls=mainWindow->findChild<QQuickWindow*>("floatingControls");auto* content=controls?controls->contentItem():nullptr;
                auto* summary=content?visualItem(content,"mediaSummary"):nullptr;auto* timeline=content?visualItem(content,"playbackTimeline"):nullptr;
                auto* bar=content?visualItem(content,"playbackBar"):nullptr;auto* time=content?visualItem(content,"playbackTime"):nullptr;
                auto* status=content?visualItem(content,"playbackStatus"):nullptr;auto* speed=content?visualItem(content,"speedButton"):nullptr;
                note("fullscreenSummaryAboveTimeline",controls && controls->isVisible() && summary && summary->isVisible() && timeline && summary->mapToScene({0,summary->height()}).y()<=timeline->mapToScene({0,0}).y()+1);
                note("fullscreenPlaybackBarFitsWindow",bar && controls && std::abs(bar->height()-80)<1 && std::abs(bar->mapToScene({0,bar->height()}).y()-controls->height())<1);
                note("playbackStatusImmediatelyAfterTime",status && time && speed && status->isVisible() && status->property("text").toString()==player.state() && std::abs(status->x()-(time->x()+time->width()+4))<1 && status->x()+status->width()<=speed->x());
                QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");stage=-3;
            }else if(stage==-3){
                serial=player.diagnostics()["resolveSerial"].toULongLong();savedY=list->property("contentY").toDouble();
                note("actualPlaylistDoubleClickDelivered",click(42,Qt::LeftButton,true));stage=1;
            }else if(stage==1 && player.state()=="播放中" && player.diagnostics()["resolveSerial"].toULongLong()>serial){
                note("doubleClickOnlyParsesSelectedStream",player.playlist().size()==60 && player.mediaInformation()["webUrl"].toString()=="https://fixture.test/item/42" && player.debugLog().count("Focus fixture: PlaylistParse")==1 && player.debugLog().count("Focus fixture: PlayitemParse")==2);
                note("doubleClickKeepsExactScrollPosition",list->property("currentIndex").toInt()==42 && std::abs(list->property("contentY").toDouble()-savedY)<1);
                player.setSetting("scriptEnabled",true);stage=14;
            }else if(stage==14){
                note("unchangedListDoesNotRetainStaleScrollSnapshot",!list->property("modelChangePending").toBool());
                note("actualPlaylistRightClickDelivered",click(42,Qt::RightButton));stage=10;
            }else if(stage==10){
                auto* menu=mainWindow->findChild<QObject*>("playlistContextMenu");
                note("playlistContextMenuOpensWithSelectedUrl",menu && menu->property("opened").toBool() && menu->property("entryUrl").toString()=="https://fixture.test/item/42");
                auto* action=mainWindow->findChild<QObject*>("openPlaylistInNewInstance");if(action)QMetaObject::invokeMethod(action,"triggered");if(menu)QMetaObject::invokeMethod(menu,"close");
                note("contextActionStartsIndependentProcess",*childPid>0 && *childPid!=QCoreApplication::applicationPid());stage=11;
            }else if(stage==11 || stage==13){
                QFile file(QDir(parser.value("data-dir")).absoluteFilePath("smoke-result.json"));if(!file.open(QIODevice::ReadOnly))return;
                const auto report=QJsonDocument::fromJson(file.readAll()).object();if(report["processId"].toVariant().toLongLong()!=*childPid)return;
                const int expected=stage==11?42:40;const QString prefix=stage==11?"contextInstance":"middleClickInstance";
                note(prefix+"ResolvesSelectedPage",report["error"].toString().isEmpty() && report["state"].toString()=="播放中" && report["mediaInformation"].toObject()["webUrl"].toString()=="https://fixture.test/item/"+QString::number(expected) && report["diagnostics"].toObject()["resolveSerial"].toInt()==1);
                note(prefix+"LocatesCurrentInNewList",report["playlistItems"].toInt()==60 && report["playlistCurrentIndex"].toInt()==expected && report["playlistContentY"].toDouble()>1000);
                note(prefix+"LeavesParentListUnchanged",list->property("currentIndex").toInt()==42 && std::abs(list->property("contentY").toDouble()-savedY)<1 && player.diagnostics()["resolveSerial"].toULongLong()==serial+1);
                if(stage==11){*childPid=0;note("actualPlaylistMiddleClickDelivered",click(40,Qt::MiddleButton));stage=12;}
                else {list->setProperty("contentY",0);player.setSubtitle(0,"");player.setSetting("mainSubtitleVisible",false);stage=2;}
            }else if(stage==12){
                note("middleClickStartsIndependentProcess",*childPid>0 && *childPid!=QCoreApplication::applicationPid());stage=13;
            }else if(stage==2){
                note("unrelatedChangesKeepScroll",list->property("contentY").toDouble()==0);
                mainWindow->findChild<QObject*>("sideTabs")->setProperty("currentIndex",1);
                visualItem(mainWindow->contentItem(),"urlField")->setProperty("text","https://fixture.test/item/short");
                QMetaObject::invokeMethod(mainWindow,"openUrl");++stage;
            }else if(stage==3 && player.state()=="播放中"){
                note("scriptThreeItemsReplaceOldList",player.playlist().size()==3);
                note("openUrlSelectsPlaylist",mainWindow->findChild<QObject*>("sideTabs")->property("currentIndex").toInt()==0);
                auto* details=visualItem(list,"playlistInfo_0");if(details)QMetaObject::invokeMethod(details,"clicked");stage=31;
            }else if(stage==31){
                auto* info=mainWindow->findChild<QQuickWindow*>("mediaInfoWindow");auto* body=info?visualItem(info->contentItem(),"mediaInfoText"):nullptr;
                note("playlistInfoShowsOwnMetadata",info && info->isVisible() && body && body->property("text").toString().contains("完整列表简介") && body->property("text").toString().contains("https://fixture.test/item/0"));
                if(info)info->close();player.open("https://fixture.test/item/empty");stage=4;
            }else if(stage==4 && player.state()=="播放中"){
                note("scriptEmptyListClearsOldItems",player.playlist().isEmpty());
                note("missingMetadataDoesNotReuseOldFields",!player.mediaInformation().contains("author") && !player.mediaInformation().contains("viewCount"));
                player.open("https://fixture.test/item/solo");++stage;
            }else if(stage==5 && player.state()=="播放中"){
                note("scriptOneItemCountPreserved",player.playlist().size()==1 && player.playlist().front().toMap().value("url").toString()=="https://fixture.test/item/solo");timer->stop();
            }
        });timer->start();
    }
    if(parser.isSet("live-exercise") && parser.isSet("smoke-test")){
        auto* timer=new QTimer(&app);timer->setInterval(100);
        QObject::connect(timer,&QTimer::timeout,&player,[&,timer,stage=0,serial=qulonglong(0),pauseEnd=0.0,pausePosition=0.0,oldGap=0.0,pauseAt=qint64(0),lastPosition=-1.0,monotonic=true,consistent=true]() mutable {
            auto note=[&](QString check,bool ok){uiVerification.append(QJsonObject{{"check",check},{"passed",ok},{"position",player.position()},{"cacheStart",player.seekStart()},{"cacheEnd",player.seekEnd()}});};
            auto dragTimeline=[&](double fraction){
                auto* timeline=visualItem(mainWindow->contentItem(),"playbackTimeline");
                QMetaObject::invokeMethod(mainWindow,"revealChrome");
                const auto a=timeline->mapToScene(QPointF(timeline->width()*.5,timeline->height()/2));
                const auto b=timeline->mapToScene(QPointF(8+(timeline->width()-16)*fraction,timeline->height()/2));
                QMouseEvent press(QEvent::MouseButtonPress,a,mainWindow->mapToGlobal(a.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&press);
                QMouseEvent move(QEvent::MouseMove,b,mainWindow->mapToGlobal(b.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&move);
                QMouseEvent release(QEvent::MouseButtonRelease,b,mainWindow->mapToGlobal(b.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&release);
            };
            const auto opened=player.diagnostics()["graphOpenSerial"].toULongLong();
            if(stage==0 && player.hasMedia() && player.state()=="播放中" && player.seekEnd()>3){
                monotonic=monotonic && player.position()+.01>=lastPosition;lastPosition=player.position();
                auto* timeline=visualItem(mainWindow->contentItem(),"playbackTimeline");
                const double end=timeline->property("to").toDouble(),start=timeline->property("from").toDouble(),value=timeline->property("value").toDouble();
                const double fraction=std::clamp((player.position()-start)/std::max(.01,end-start),0.0,1.0);
                consistent=consistent && std::abs(value-player.position())<.4 && std::abs(timeline->property("displayFraction").toDouble()-fraction)<.08;
            }
            if(stage==0 && player.hasMedia() && player.canSeek() && player.seekEnd()>9){
                note("liveProgressDoesNotMoveBackwards",monotonic);note("liveTimelineFollowsCoherentPositionAndRange",consistent);
                note("liveHasSeekableCache",player.isLive() && player.diagnostics()["liveCache"].toBool());serial=opened;dragTimeline(.35);++stage;
            }else if(stage==1 && player.state()=="播放中" && opened>serial && player.diagnostics()["rendererFramesDrawn"].toInt()>0){
                note("liveRewindRendered",player.position()>=2 && player.position()<player.seekEnd()-4);player.togglePause();++stage;
            }else if(stage==2 && player.state()=="已暂停"){
                pauseEnd=player.seekEnd();pausePosition=player.position();pauseAt=QDateTime::currentMSecsSinceEpoch();++stage;
            }else if(stage==3 && QDateTime::currentMSecsSinceEpoch()-pauseAt>2500){
                note("pausedLiveStillCaches",player.seekEnd()>pauseEnd+1 && std::abs(player.position()-pausePosition)<.25);
                note("livePitchPreservingSpeedAvailable",player.canChangePlaybackRate() && player.diagnostics()["pitchPreserved"].toBool());
                player.setPlaybackRate(1.5);stage=30;
            }else if(stage==30 && player.canChangePlaybackRate()){
                note("pausedLiveChangesSpeed",player.state()=="已暂停" && std::abs(player.playbackRate()-1.5)<.01 && std::abs(player.position()-pausePosition)<.25);
                oldGap=player.seekEnd()-player.position();serial=opened;dragTimeline(1);stage=4;
            }else if(stage==4 && player.state()=="已暂停" && opened>serial){
                note("rightDragReducesDelayKeepsPaused",player.seekEnd()-player.position()<oldGap-2 && player.seekEnd()-player.position()<4);serial=opened;player.returnToLive();++stage;
            }else if(stage==5 && player.state()=="播放中" && opened>serial && player.diagnostics()["rendererFramesDrawn"].toInt()>0){
                note("returnLiveResumesAndReducesDelay",player.seekEnd()-player.position()<oldGap-2 && player.seekEnd()-player.position()<4);
                note("timelineUsesCachedRange",player.seekEnd()>player.seekStart() && player.canSeek());
                note("liveRebuildRetainsSpeed",std::abs(player.playbackRate()-1.5)<.01);player.setPlaybackRate(1);stage=50;
            }else if(stage==50 && player.canChangePlaybackRate()){
                note("liveNormalSpeedRestored",std::abs(player.playbackRate()-1)<.01 && player.state()=="播放中");player.stop();stage=6;
            }else if(stage==6){note("stopReleasesLiveCache",!player.isLive() && !player.hasMedia() && !player.canSeek());player.open(parser.value("open"));++stage;
            }else if(stage==7 && player.hasMedia() && player.state()=="播放中"){note("liveReopenRendered",player.canSeek() && player.isLive());timer->stop();}
        });timer->start();
    }
    if(parser.isSet("ui-exercise") && parser.isSet("smoke-test")) {
        player.setSetting("viewMode","videoAndPanel");player.setSetting("chatVisible",true);
        auto* uiTimer=new QTimer(&app);uiTimer->setInterval(250);
        QObject::connect(uiTimer,&QTimer::timeout,&player,[&,uiTimer,uiStage=0,popupWait=0,startWidth=0.0,dragPoint=QPointF{},openedPosition=0.0,popupWindow=QPointer<QWindow>{},popupGeometry=QRect{}]() mutable {
            auto note=[&](QString check,bool ok){uiVerification.append(QJsonObject{{"check",check},{"passed",ok}});};
            auto mouse=[&](QEvent::Type type,QPointF point,Qt::MouseButtons buttons){
                QMouseEvent event(type,point,mainWindow->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,buttons,Qt::NoModifier);
                QCoreApplication::sendEvent(mainWindow,&event);
            };
            auto wheel=[&](QQuickItem* item,int amount,Qt::KeyboardModifiers modifiers){
                if(!item)return;
                auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
                QWheelEvent event(point,mainWindow->mapToGlobal(point.toPoint()),{},QPoint(0,amount),Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
                QCoreApplication::sendEvent(mainWindow,&event);
            };
            auto rememberPopup=[&]{
                QCoreApplication::processEvents();popupWindow=nullptr;
                for(auto* w:QGuiApplication::allWindows())if(w->isVisible() && w!=mainWindow && w!=player.videoWindow() && (w->flags()&Qt::WindowType_Mask)==Qt::Popup){popupWindow=w;popupGeometry=w->geometry();}
            };
            if(uiStage==0) {
                if(!player.hasMedia() || player.state()!="播放中")return;
                note("defaultSidebar480",player.settings().value("chatWidth").toInt()==480 && std::abs(sideItem->width()-480)<1);
                note("nativeVideoVisible",player.videoWindow()->isVisible());
                startWidth=sideItem->width();dragPoint=sideItem->mapToScene(QPointF(-3,250));
                mouse(QEvent::MouseMove,dragPoint,Qt::NoButton);mouse(QEvent::MouseButtonPress,dragPoint,Qt::LeftButton);
            } else if(uiStage==1)mouse(QEvent::MouseMove,dragPoint-QPointF(96,0),Qt::LeftButton);
            else if(uiStage==2)mouse(QEvent::MouseButtonRelease,dragPoint-QPointF(96,0),Qt::NoButton);
            else if(uiStage==3) {
                note("videoDividerPreservesSidebar",std::abs(sideItem->width()-startWidth)<1 && mainWindow->width()==1224 && std::abs(player.settings().value("chatWidth").toInt()-startWidth)<1);
                mainWindow->resize(980,850);
            } else if(uiStage==4) {
                note("windowResizePreservesSavedWidth",std::abs(player.settings().value("chatWidth").toInt()-startWidth)<1);
                mainWindow->resize(1320,850);
            } else if(uiStage==5) {openedPosition=player.position();popupWait=0;QMetaObject::invokeMethod(mainWindow,"showSettings");}
            else if(uiStage==6) {
                if(!settingsPopup->property("opened").toBool() && popupWait++<6)return;
                note("settingsKeepsNativeVideoAndSidebar",settingsPopup->property("opened").toBool() && player.videoWindow()->isVisible() && sideItem->isVisible());
                QMetaObject::invokeMethod(settingsPopup,"close");
            } else if(uiStage==7){popupWait=0;QMetaObject::invokeMethod(mainWindow,"showTracks");}
            else if(uiStage==8) {
                // Popup.Window enter animations may finish after the
                // next tick on a busy desktop. Wait up to 1.5 seconds,
                // then still require the popup and native views.
                if(!tracksPopup->property("opened").toBool() && popupWait++<6)return;
                note("tracksKeepsNativeVideoAndSidebar",tracksPopup->property("opened").toBool() && player.videoWindow()->isVisible() && sideItem->isVisible());
                note("dialogsPreservePlayback",player.state()=="播放中" && player.position()>openedPosition);
                QMetaObject::invokeMethod(tracksPopup,"close");
                player.setVolume(20);player.toggleMute();
                note("mutePreservesVolume",player.settings().value("muted").toBool() && player.settings().value("volume").toInt()==20);
            } else if(uiStage==9) {
                auto* native=player.videoWindow();
                auto nativeMove=[&](QPointF point){QMouseEvent event(QEvent::MouseMove,point,native->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(native,&event);};
                nativeMove(QPointF(1,native->height()/2));bool edgeCursor=native->cursor().shape()==Qt::SizeHorCursor;
                nativeMove(QPointF(native->width()/2,native->height()/2));note("nativeResizeCursorRestores",edgeCursor && native->cursor().shape()==Qt::ArrowCursor);
                auto* volume=visualItem(mainWindow->contentItem(),"volumeSlider");
                wheel(volume,120,Qt::NoModifier);
                auto* percentage=visualItem(mainWindow->contentItem(),"volumePercent");
                note("volumeWheelAndPercent",player.settings().value("volume").toInt()==25 && !player.settings().value("muted").toBool() && percentage && percentage->property("text").toString()=="25%");
                player.setVolume(0);wheel(volume,-120,Qt::NoModifier);
                note("volumeLowerBound",player.settings().value("volume").toInt()==0);
                note("parsePlaylistOptionRemoved",!mainWindow->findChild<QObject*>("parsePlaylist"));
                player.setSetting("playlistItemHeight",76);
                wheel(mainWindow->findChild<QQuickItem*>("playlistView"),120,Qt::ControlModifier);
                QMetaObject::invokeMethod(mainWindow,"showSettings");
            } else if(uiStage==10) {
                note("playlistWheelZoom",player.settings().value("playlistItemHeight").toInt()==84);
                note("playlistScrollbar",mainWindow->findChild<QQuickItem*>("playlistScrollBar")->isVisible());
                auto* field=visualItem(qobject_cast<QQuickWindow*>(settingsPopup)->contentItem(),"path_lavDirectory");
                if(!field){note("windowsPathSelectable",false);++uiStage;return;}
                QMetaObject::invokeMethod(field,"selectAll");auto path=field->property("text").toString();
                note("windowsPathSelectable",!path.isEmpty() && !path.contains('/') && field->property("selectedText").toString()==path);
                auto* saved=new QMimeData;auto* clipboard=QGuiApplication::clipboard();
                if(auto* mime=clipboard->mimeData())for(const auto& format:mime->formats())saved->setData(format,mime->data(format));
                QMetaObject::invokeMethod(field,"copy");bool copied=clipboard->text()==path;
                QMetaObject::invokeMethod(field,"paste");note("pathCopyPaste",copied && field->property("text").toString()==path);clipboard->setMimeData(saved);
                auto* settingWindow=qobject_cast<QWindow*>(settingsPopup);settingWindow->resize(760,600);
                QMetaObject::invokeMethod(mainWindow,"setWindowWidth",Q_ARG(QVariant,1200));
            } else if(uiStage==11) {
                note("resizableSettingsAndWindowWidth",qobject_cast<QWindow*>(settingsPopup)->width()==760 && mainWindow->width()==1200);
                auto* combo=settingsPopup->findChild<QObject*>("themeSelector");
                if(!combo){note("comboPopupPositionStable",false);uiTimer->stop();return;}
                auto* popup=combo->property("popup").value<QObject*>();QMetaObject::invokeMethod(popup,"open");rememberPopup();
            } else if(uiStage==12) {
                note("comboPopupPositionStable",popupWindow && popupWindow->geometry()==popupGeometry);
                auto* combo=settingsPopup->findChild<QObject*>("themeSelector");QMetaObject::invokeMethod(combo->property("popup").value<QObject*>(),"close");
                QMetaObject::invokeMethod(settingsPopup,"close");mainWindow->resize(1320,850);
                auto* menu=mainWindow->findChild<QObject*>("videoMenu");QMetaObject::invokeMethod(menu,"open");rememberPopup();
            } else if(uiStage==13) {
                note("menuPopupPositionStable",popupWindow && popupWindow->geometry()==popupGeometry);
                QMetaObject::invokeMethod(mainWindow->findChild<QObject*>("videoMenu"),"close");uiTimer->stop();
            }
            ++uiStage;
        });uiTimer->start();
    }
    if(parser.isSet("subtitle-exercise") && parser.isSet("smoke-test")) {
        auto* features=new QTimer(&app);features->setInterval(350);
        QObject::connect(features,&QTimer::timeout,&player,[&,features,step=0,ticks=0,primary=qulonglong(0),secondary=qulonglong(0),serial=qulonglong(0),menuWindow=QPointer<QWindow>{},menuRect=QRect{},activeMenu=QPointer<QObject>{}]() mutable {
            auto note=[&](QString name,bool passed){uiVerification.append(QJsonObject{{"check",name},{"passed",passed}});};
            auto diag=player.diagnostics();auto p=diag.value("primarySubtitleFrames").toULongLong(),s=diag.value("secondarySubtitleFrames").toULongLong();
            if(step==0){
                if(!player.hasMedia() || p==0 || s==0)return;
                note("metadataDefaultPrimary",player.selectedMainSubtitle()=="script:1");
                note("metadataDefaultDanmaku",player.selectedSecondarySubtitle()=="script:0");
                note("metadataNameNotLanguage",player.subtitles()[2].toMap()["label"].toString()=="简体中文 · 主字幕");
                note("inlineContentBothRendered",p>0 && s>0);
                note("scriptDebugOutput",player.debugLog().contains("调用 PlayitemParse") && player.debugLog().contains("Subtitle fixture: PlayitemParse"));
                player.setSetting("mainSubtitleVisible",false);ticks=0;++step;
            }else if(step==1){if(++ticks<3)return;primary=p;secondary=s;++step;}
            else if(step==2){
                if(++ticks<6)return;
                note("hidePrimaryRetainsSelection",player.selectedMainSubtitle()=="script:1" && p==primary && s>secondary);
                player.setSetting("mainSubtitleVisible",true);ticks=0;++step;
            }else if(step==3){
                if(++ticks<3)return;note("showPrimaryRestoresContent",p>primary);
                player.selectSubtitle(0,"script:2");secondary=s;ticks=0;++step;
            }else if(step==4){
                if(++ticks<3)return;note("subtitleRadioSelection",player.selectedMainSubtitle()=="script:2" && player.selectedSecondarySubtitle()=="script:0" && s>secondary);
                auto* button=visualItem(mainWindow->contentItem(),"subtitleButton");activeMenu=button->findChild<QObject*>("subtitleMenu");QMetaObject::invokeMethod(button,"clicked");QCoreApplication::processEvents();
                for(auto* w:QGuiApplication::allWindows())if(w->isVisible() && (w->flags()&Qt::WindowType_Mask)==Qt::Popup){menuWindow=w;menuRect=w->geometry();}
                ticks=0;++step;
            }else if(step==5){
                auto* menu=activeMenu.data();note("subtitleTopMenuFourRows",menu && menu->property("count").toInt()==4 && menu->property("opened").toBool());
                auto font=menu->property("font").value<QFont>();QFontMetricsF metrics(font);
                bool fits=true;const auto entries=menu->property("contentItem").value<QQuickItem*>();
                std::function<void(QQuickItem*)> inspectLabels=[&](QQuickItem* item){
                    if(!item)return;
                    if(item->property("text").isValid() && item->property("contentItem").isValid()){
                        auto* label=item->property("contentItem").value<QQuickItem*>();
                        if(label)fits=fits && label->width()+1>=label->implicitWidth();
                    }
                    for(auto* child:item->childItems())inspectLabels(child);
                };inspectLabels(entries);
                uiVerification.append(QJsonObject{{"check","subtitlePrimaryFitsText"},{"passed",fits && menu->property("width").toDouble()<200},{"actualWidth",menu->property("width").toDouble()}});
                auto bigFont=font;bigFont.setPixelSize(font.pixelSize()+7);menu->setProperty("font",bigFont);QCoreApplication::processEvents();
                note("subtitlePrimaryAdaptsFont",menu->property("width").toDouble()>std::ceil(metrics.horizontalAdvance("显示主字幕"))+56);
                menu->setProperty("font",font);QCoreApplication::processEvents();
                auto* button=visualItem(mainWindow->contentItem(),"subtitleButton");
                note("subtitleMenuAboveControls",menuWindow && menuWindow->geometry().bottom()<=mainWindow->mapToGlobal(button->mapToScene(QPointF{}).toPoint()).y()+2);
                note("subtitleMenuPositionStable",menuWindow && menuWindow->geometry()==menuRect);
                auto* submenu=menu->findChild<QObject*>("mainSubtitleMenu");QMetaObject::invokeMethod(submenu,"open");ticks=0;++step;
            }else if(step==6){
                auto* menu=activeMenu.data();auto* submenu=menu->findChild<QObject*>("mainSubtitleMenu");
                note("subtitleSubmenuContentWidth",submenu && submenu->property("opened").toBool() && submenu->property("width").toDouble()>300 && submenu->property("count").toInt()==6);
                auto* first=menu->property("background").value<QObject*>();auto* second=submenu->property("background").value<QObject*>();
                note("subtitleMenusDarkTheme",first && second && first->property("color").value<QColor>()==QColor(player.theme().value("background").toString()) && second->property("color").value<QColor>()==first->property("color").value<QColor>());
                player.setSetting("theme","light");QCoreApplication::processEvents();
                note("subtitleMenusLiveLightTheme",first && second && first->property("color").value<QColor>()==QColor("#f4f5f9") && second->property("color").value<QColor>()==QColor("#f4f5f9"));
                player.setSetting("theme","dark");QCoreApplication::processEvents();
                note("subtitleMenusLiveDarkTheme",second && second->property("color").value<QColor>()==QColor("#0d1018"));
                QMetaObject::invokeMethod(submenu,"close");QMetaObject::invokeMethod(menu,"close");
                QMetaObject::invokeMethod(mainWindow,"showSettings");QMetaObject::invokeMethod(mainWindow,"setWindowHeight",Q_ARG(QVariant,700));QMetaObject::invokeMethod(mainWindow,"showScriptDebug");++step;
            }else if(step==7){
                auto* debug=mainWindow->findChild<QWindow*>("scriptDebugWindow");
                note("debugWindowKeepsVideo",debug && debug->isVisible() && player.videoWindow()->isVisible());
                note("windowHeightSetting",mainWindow->height()==700 && settingsPopup->findChild<QObject*>("windowHeightSetting"));
                QMetaObject::invokeMethod(debug,"close");QMetaObject::invokeMethod(settingsPopup,"close");
                serial=diag.value("resolveSerial").toULongLong();player.setSetting("scriptEnabled",false);player.setSetting("mediaScript","Z:\\missing-script.as");
                player.clearPlaylist();auto file=QUrl::fromLocalFile(parser.value("local-fixture"));player.openFiles(QVariantList{file,file});ticks=0;++step;
            }else if(step==8){
                if(player.state()!="播放中" || player.duration()<=0)return;
                auto* chat=mainWindow->findChild<QQuickItem*>("chatTab");
                note("localModeSkipsAngelScript",!player.scriptEnabled() && diag.value("resolveSerial").toULongLong()==serial && player.error().isEmpty());
                note("localModeChatHidden",chat && !chat->isVisible() && !player.chatAvailable());
                note("localPlaylistDeduplicated",player.playlist().size()==1);
                auto* summary=visualItem(mainWindow->contentItem(),"mediaSummary");
                note("localDurationInformationUpdates",player.mediaInformation().value("duration").toDouble()>0 && summary && summary->isVisible() && !summary->property("text").toString().isEmpty());
                player.stop();note("stopKeepsLocalPlaylist",player.playlist().size()==1);player.openPlaylistItem(0);++step;
            }else if(step==9){
                if(!player.hasMedia())return;note("localPlaylistReopensWithoutScript",player.error().isEmpty() && diag.value("resolveSerial").toULongLong()==serial);
                player.removePlaylistItem(0);note("localPlaylistRemove",player.playlist().isEmpty());
                features->stop();
            }
        });features->start();
    }
    if(parser.isSet("subtitle-scroll-exercise") && parser.isSet("smoke-test")) {
        auto* checks=new QTimer(&app);checks->setInterval(400);
        QObject::connect(checks,&QTimer::timeout,&player,[&,checks,step=0,round=0,settled=false,p=qulonglong(0),s=qulonglong(0),requests=qulonglong(0),clickAt=qint64(0),y=0.0,menu=QPointer<QObject>{}]() mutable {
            auto note=[&](QString name,bool ok){
                auto* main=menu?menu->findChild<QObject*>("mainSubtitleVisibility"):nullptr;auto* second=menu?menu->findChild<QObject*>("secondarySubtitleVisibility"):nullptr;
                auto* action=mainWindow->property("secondarySubtitleAction").value<QObject*>();auto* itemAction=second?second->property("action").value<QObject*>():nullptr;
                uiVerification.append(QJsonObject{{"check",name},{"passed",ok},{"primaryEnabled",player.settings().value("mainSubtitleVisible").toBool()},{"secondaryEnabled",player.settings().value("secondarySubtitleVisible").toBool()},{"primaryChecked",main && main->property("checked").toBool()},{"secondaryChecked",second && second->property("checked").toBool()},{"rootSecondaryActionExists",action!=nullptr},{"sameAction",action==itemAction},{"rootActionChecked",action && action->property("checked").toBool()},{"itemActionExists",itemAction!=nullptr},{"itemActionChecked",itemAction && itemAction->property("checked").toBool()},{"primaryFrames",qint64(player.diagnostics().value("primarySubtitleFrames").toULongLong())},{"secondaryFrames",qint64(player.diagnostics().value("secondarySubtitleFrames").toULongLong())},{"priorPrimary",qint64(p)},{"priorSecondary",qint64(s)}});
            };
            auto diag=player.diagnostics();auto primary=diag.value("primarySubtitleFrames").toULongLong(),secondary=diag.value("secondarySubtitleFrames").toULongLong();
            auto* list=mainWindow->findChild<QQuickItem*>("playlistView");auto* scroll=mainWindow->findChild<QQuickItem*>("playlistScrollBar");
            auto wheel=[&](QQuickItem* item,int angle,int pixels=0){auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));QWheelEvent event(point,mainWindow->mapToGlobal(point.toPoint()),QPoint(0,pixels),QPoint(0,angle),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(mainWindow,&event);};
            auto click=[&](const char* name){
                auto* entry=menu->findChild<QQuickItem*>(name);if(!entry)return false;
                const auto now=QDateTime::currentMSecsSinceEpoch();const auto delay=int(std::max(qint64(100),clickAt-now+200));clickAt=now+delay;
                QTimer::singleShot(delay-80,menu,[source=menu,mainWindow]{QMetaObject::invokeMethod(mainWindow,"revealChrome");QMetaObject::invokeMethod(source,"open");});
                QTimer::singleShot(delay,entry,[entry]{
                    auto* window=entry->window();if(!window)return;const auto point=entry->mapToScene(QPointF(entry->width()/2,entry->height()/2));
                    QMouseEvent press(QEvent::MouseButtonPress,point,window->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&press);
                    QMouseEvent release(QEvent::MouseButtonRelease,point,window->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
                });
                return true;
            };
            auto sync=[&](QObject* source,const char* name,const char* setting){auto* entry=source->findChild<QObject*>(name);return entry && entry->property("checked").toBool()==player.settings().value(setting).toBool();};
            if(step==0){
                if(!player.hasMedia() || primary==0 || secondary==0)return;
                menu=visualItem(mainWindow->contentItem(),"subtitleButton")->findChild<QObject*>("subtitleMenu");p=primary;s=secondary;
                note("secondaryClickExecuted",click("secondarySubtitleVisibility"));++step;
            }else if(step==1){
                if(!settled){p=primary;s=secondary;settled=true;return;}
                note("secondaryCheckMatchesHidden",!player.settings().value("secondarySubtitleVisible").toBool() && sync(menu,"secondarySubtitleVisibility","secondarySubtitleVisible") && secondary==s && primary>p);
                p=primary;s=secondary;settled=false;click("mainSubtitleVisibility");++step;
            }else if(step==2){
                if(!settled){p=primary;s=secondary;settled=true;return;}
                note("primaryCheckMatchesHidden",!player.settings().value("mainSubtitleVisible").toBool() && sync(menu,"mainSubtitleVisibility","mainSubtitleVisible") && primary==p && secondary==s);
                click("mainSubtitleVisibility");click("secondarySubtitleVisibility");settled=false;++step;
            }else if(step==3){
                if(!settled){settled=true;return;}
                note("bothChecksRestoreCaptions",primary>p && secondary>s && sync(menu,"mainSubtitleVisibility","mainSubtitleVisible") && sync(menu,"secondarySubtitleVisibility","secondarySubtitleVisible"));
                p=primary;s=secondary;QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==4){
                note(QString("fullscreenCaptionsRound%1").arg(round),mainWindow->visibility()==QWindow::FullScreen && primary>p && secondary>s);
                if(round==0){auto* floating=mainWindow->findChild<QObject*>("floatingControls");auto* floatingMenu=floating?floating->findChild<QObject*>("subtitleMenu"):nullptr;note("fullscreenMenuChecksSynced",floatingMenu && sync(floatingMenu,"mainSubtitleVisibility","mainSubtitleVisible") && sync(floatingMenu,"secondarySubtitleVisibility","secondarySubtitleVisible"));}
                p=primary;s=secondary;QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==5){
                note(QString("windowedCaptionsRound%1").arg(round),mainWindow->visibility()==QWindow::Windowed && primary>p && secondary>s);
                if(++round<4){p=primary;s=secondary;QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");step=4;return;}
                QVariantList items;for(int i=0;i<60;++i)items.push_back(QVariantMap{{"title",QString("Scroll fixture %1").arg(i)},{"url",""},{"current",false}});
                player.setSetting("playlistItemHeight",76);list->setProperty("model",items);++step;
            }else if(step==6){y=list->property("contentY").toDouble();wheel(list,-120);++step;}
            else if(step==7){
                double current=list->property("contentY").toDouble();note("playlistWheelUsesOneRow",std::abs(current-y-76)<1);
                y=current;wheel(scroll,-120);++step;
            }else if(step==8){
                double current=list->property("contentY").toDouble();note("playlistScrollbarWheelSameDistance",std::abs(current-y-76)<1);
                y=current;wheel(list,0,-80);++step;
            }else if(step==9){
                double current=list->property("contentY").toDouble();note("playlistPixelScrollOneToOne",std::abs(current-y-80)<1);
                note("playlistThumbTracksContent",std::abs(scroll->property("position").toDouble()-current/list->property("contentHeight").toDouble())<0.001);
                y=current;
                for(int i=0;i<5;++i)QTimer::singleShot(i*20,checks,[list,mainWindow]{
                    const auto point=list->mapToScene(QPointF(list->width()/2,list->height()/2));
                    QWheelEvent event(point,mainWindow->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(mainWindow,&event);
                });
                QTimer::singleShot(180,checks,[&,list,start=current]{
                    const double actual=list->property("contentY").toDouble()-start;
                    uiVerification.append(QJsonObject{{"check","playlistRapidWheelKeepsUp"},{"passed",std::abs(actual-5*76)<1},{"distance",actual}});
                });step=91;
            }else if(step==91){
                note("playlistRapidWheelPreservesEveryNotch",std::abs(list->property("contentY").toDouble()-y-5*76)<1);
                auto* right=visualItem(mainWindow->contentItem(),"windowResizeEdge4");auto* bottom=visualItem(mainWindow->contentItem(),"windowResizeEdge8");
                note("playlistThumbOutsideResizeEdges",right && bottom && scroll->mapToScene({scroll->width(),0}).x()<=right->x() && scroll->mapToScene({0,scroll->height()}).y()<=bottom->y());
                auto* thumb=scroll->property("contentItem").value<QQuickItem*>();
                const auto point=thumb->mapToScene(QPointF(thumb->width()/2,thumb->height()/2));
                QMouseEvent hover(QEvent::MouseMove,point,mainWindow->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&hover);
                note("playlistThumbHasArrowCursor",mainWindow->cursor().shape()==Qt::ArrowCursor);
                QMouseEvent press(QEvent::MouseButtonPress,point,mainWindow->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&press);
                note("playlistThumbReceivesDragPress",scroll->property("pressed").toBool());
                y=list->property("contentY").toDouble();const auto target=point+QPointF(0,120);
                QMouseEvent move(QEvent::MouseMove,target,mainWindow->mapToGlobal(target.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&move);
                QMouseEvent release(QEvent::MouseButtonRelease,target,mainWindow->mapToGlobal(target.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&release);
                step=92;
            }else if(step==92){
                note("playlistThumbDragScrollsWithoutResizing",list->property("contentY").toDouble()>y+76 && !scroll->property("pressed").toBool() && mainWindow->width()==player.settings().value("windowWidth").toInt() && std::abs(sideItem->width()-480)<1);
                y=list->property("contentY").toDouble();wheel(list,-360);wheel(list,120);step=93;
            }else if(step==93){
                note("playlistWheelReversalCancelsPendingDistance",std::abs(list->property("contentY").toDouble()-y+76)<1);
                wheel(list,-120000);step=10;
            }else if(step==10){
                const double end=list->property("originY").toDouble()+list->property("contentHeight").toDouble()-list->height();
                note("playlistScrollStopsAtBounds",std::abs(list->property("contentY").toDouble()-end)<1);
                player.togglePause();++step;
            }else if(step==11){
                if(player.state()!="已暂停")return;
                requests=diag.value("subtitleFrameRequests").toULongLong();click("mainSubtitleVisibility");++step;
            }else if(step==12){
                note("pausedHideRepaintsExistingFrame",player.state()=="已暂停" && !player.settings().value("mainSubtitleVisible").toBool() && diag.value("subtitleFrameRequests").toULongLong()>requests);
                requests=diag.value("subtitleFrameRequests").toULongLong();click("secondarySubtitleVisibility");++step;
            }else if(step==13){
                note("pausedBothHiddenMenuMatchesRenderer",!player.settings().value("secondarySubtitleVisible").toBool() && sync(menu,"mainSubtitleVisibility","mainSubtitleVisible") && sync(menu,"secondarySubtitleVisibility","secondarySubtitleVisible") && diag.value("subtitleFrameRequests").toULongLong()>requests);
                p=primary;s=secondary;QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==14){
                auto* floating=mainWindow->findChild<QObject*>("floatingControls");menu=floating->findChild<QObject*>("subtitleMenu");
                note("fullscreenHiddenChecksStaySynced",!menu->findChild<QObject*>("mainSubtitleVisibility")->property("checked").toBool() && !menu->findChild<QObject*>("secondarySubtitleVisibility")->property("checked").toBool() && primary==p && secondary==s);
                click("secondarySubtitleVisibility");++step;
            }else if(step==15){
                note("fullscreenPausedSecondaryReappears",secondary>s && primary==p && player.state()=="已暂停");
                click("mainSubtitleVisibility");++step;
            }else if(step==16){
                note("fullscreenPausedPrimaryReappears",primary>p && sync(menu,"mainSubtitleVisibility","mainSubtitleVisible"));
                QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==17){
                menu=visualItem(mainWindow->contentItem(),"subtitleButton")->findChild<QObject*>("subtitleMenu");
                note("windowedMenuReflectsFullscreenToggles",sync(menu,"mainSubtitleVisibility","mainSubtitleVisible") && sync(menu,"secondarySubtitleVisibility","secondarySubtitleVisible"));
                auto* timeline=visualItem(mainWindow->contentItem(),"playbackTimeline");
                const auto a=timeline->mapToScene(QPointF(timeline->width()/2,timeline->height()/2));
                const auto b=timeline->mapToScene(QPointF(8+(timeline->width()-16)*3/player.duration(),timeline->height()/2));
                QMouseEvent press(QEvent::MouseButtonPress,a,mainWindow->mapToGlobal(a.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&press);
                QMouseEvent move(QEvent::MouseMove,b,mainWindow->mapToGlobal(b.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&move);
                const double target=timeline->property("value").toDouble();
                QMouseEvent release(QEvent::MouseButtonRelease,b,mainWindow->mapToGlobal(b.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&release);
                note("vodSeekDoesNotReturnToOldPosition",player.seeking() && std::abs(target-3)<.1 && std::abs(player.position()-target)<.01 && std::abs(timeline->property("value").toDouble()-target)<.01);
                ++step;
            }else if(step==18){
                note("vodSeekFinishesAtRequestedPosition",player.state()=="已暂停" && std::abs(player.position()-3)<.25);
                checks->stop();
            }
        });checks->start();
    }
    if(parser.isSet("window-exercise") && parser.isSet("smoke-test")) {
        auto* windows=new QTimer(&app);windows->setInterval(650);
        QObject::connect(windows,&QTimer::timeout,&player,[&,windows,step=0,videoWidth=0.0,left=0]() mutable {
            auto note=[&](QString name,bool passed){uiVerification.append(QJsonObject{{"check",name},{"passed",passed}});};
            auto* video=visualItem(mainWindow->contentItem(),"videoArea");
            auto saved=[&](int w,int h){return player.settings().value("windowWidth").toInt()==w && player.settings().value("windowHeight").toInt()==h;};
            if(step==0){
                if(!player.hasMedia())return;
                mainWindow->setX(20);mainWindow->setY(20);
                QMetaObject::invokeMethod(mainWindow,"setWindowWidth",Q_ARG(QVariant,1200));QMetaObject::invokeMethod(mainWindow,"setWindowHeight",Q_ARG(QVariant,700));++step;
            }else if(step==1){
                note("settingsSizePersisted",saved(1200,700));videoWidth=video->width();left=mainWindow->x();
                auto* a=visualItem(mainWindow->contentItem(),"minimizeWindowButton");auto* b=visualItem(mainWindow->contentItem(),"maximizeWindowButton");auto* c=visualItem(mainWindow->contentItem(),"closeWindowButton");
                note("windowButtonsUniform",a && b && c && a->width()==32 && b->width()==32 && c->width()==32 && a->height()==32 && b->height()==32 && c->height()==32);
                note("windowButtonsDrawnIcons",a && b && c && a->property("iconKind").toString()=="minimize" && b->property("iconKind").toString()=="maximize" && c->property("iconKind").toString()=="close");
                QMetaObject::invokeMethod(mainWindow,"toggleSidebar");++step;
            }else if(step==2){
                note("sidebarCloseContractsWindow",!sideItem->isVisible() && std::abs(mainWindow->width()-videoWidth)<1 && mainWindow->x()==left && mainWindow->height()==700);
                note("sidebarClosePreservesVideoWidth",std::abs(video->width()-videoWidth)<1);
                QMetaObject::invokeMethod(mainWindow,"toggleSidebar");++step;
            }else if(step==3){
                note("sidebarOpenExpandsWindow",sideItem->isVisible() && mainWindow->width()==1200 && mainWindow->x()==left && mainWindow->height()==700);
                note("sidebarOpenPreservesVideoWidth",std::abs(video->width()-videoWidth)<1);
                note("sidebarRoundTripSizeSaved",saved(1200,700));
                QMetaObject::invokeMethod(mainWindow,"toggleMaximized");++step;
            }else if(step==4){
                note("maximizePreservesNormalSize",mainWindow->visibility()==QWindow::Maximized && saved(1200,700));
                auto* button=visualItem(mainWindow->contentItem(),"maximizeWindowButton");note("maximizeButtonRestoreIcon",button && button->property("iconKind").toString()=="restore");
                QMetaObject::invokeMethod(mainWindow,"toggleMaximized");++step;
            }else if(step==5){
                note("unmaximizeRestoresNormalSize",mainWindow->width()==1200 && mainWindow->height()==700 && saved(1200,700));
                QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==6){
                note("fullscreenPreservesNormalSize",mainWindow->visibility()==QWindow::FullScreen && saved(1200,700));
                QMetaObject::invokeMethod(mainWindow,"toggleVideoFullscreen");++step;
            }else if(step==7){
                note("fullscreenExitRestoresSize",mainWindow->width()==1200 && mainWindow->height()==700 && saved(1200,700));
                mainWindow->resize(1280,740);++step;
            }else if(step==8){
                note("manualResizeSizePersisted",saved(1280,740));
                auto* button=visualItem(mainWindow->contentItem(),"minimizeWindowButton");QMetaObject::invokeMethod(button,"clicked");++step;
            }else if(step==9){
                note("minimizeButtonPreservesSize",mainWindow->visibility()==QWindow::Minimized && saved(1280,740));
                mainWindow->showNormal();++step;
            }else if(step==10){
                note("minimizeRestoreSize",mainWindow->width()==1280 && mainWindow->height()==740 && saved(1280,740));
                const auto at=QPointF(mainWindow->width()-4,300);
                auto mouse=[&](QEvent::Type type,QPointF point,Qt::MouseButtons buttons){QMouseEvent event(type,point,mainWindow->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,buttons,Qt::NoModifier);QCoreApplication::sendEvent(mainWindow,&event);};
                mouse(QEvent::MouseMove,at,Qt::NoButton);mouse(QEvent::MouseButtonPress,at,Qt::LeftButton);mouse(QEvent::MouseMove,at+QPointF(96,0),Qt::LeftButton);mouse(QEvent::MouseButtonRelease,at+QPointF(96,0),Qt::NoButton);++step;
            }else if(step==11){
                auto* edge=visualItem(mainWindow->contentItem(),"windowResizeEdge4");
                note("rightEdgeInputGeometry",edge && edge->isVisible() && edge->width()==8 && edge->height()==740);
                note("rightEdgeOnlyResizesSidebar",std::abs(sideItem->width()-576)<1 && std::abs(visualItem(mainWindow->contentItem(),"videoArea")->width()-794)<1);
                QMetaObject::invokeMethod(mainWindow,"finishWindowResize");
                note("rightEdgeSavesSidebarWidth",player.settings().value("chatWidth").toInt()==576);
                QMetaObject::invokeMethod(mainWindow,"beginWindowResize",Q_ARG(QVariant,int(Qt::LeftEdge)));mainWindow->resize(1440,740);++step;
            }else if(step==12){
                note("leftEdgeOnlyResizesVideo",std::abs(sideItem->width()-576)<1 && std::abs(visualItem(mainWindow->contentItem(),"videoArea")->width()-858)<1);
                QMetaObject::invokeMethod(mainWindow,"finishWindowResize");
                QMetaObject::invokeMethod(mainWindow,"beginVideoResize",Q_ARG(QVariant,1000));QMetaObject::invokeMethod(mainWindow,"dragVideoResize",Q_ARG(QVariant,936));++step;
            }else if(step==13){
                note("dividerOnlyResizesVideo",mainWindow->width()==1376 && std::abs(sideItem->width()-576)<1 && std::abs(visualItem(mainWindow->contentItem(),"videoArea")->width()-794)<1);
                QMetaObject::invokeMethod(mainWindow,"finishWindowResize");QMetaObject::invokeMethod(mainWindow,"setSidebarVisible",Q_ARG(QVariant,false));++step;
            }else if(step==14){
                QMetaObject::invokeMethod(mainWindow,"beginWindowResize",Q_ARG(QVariant,int(Qt::RightEdge)));mainWindow->resize(826,740);++step;
            }else if(step==15){
                note("closedRightEdgeResizesVideo",!sideItem->isVisible() && std::abs(visualItem(mainWindow->contentItem(),"videoArea")->width()-826)<1);
                QMetaObject::invokeMethod(mainWindow,"finishWindowResize");QMetaObject::invokeMethod(mainWindow,"beginWindowResize",Q_ARG(QVariant,int(Qt::LeftEdge)));mainWindow->resize(858,740);++step;
            }else if(step==16){
                note("closedLeftEdgeResizesVideo",!sideItem->isVisible() && std::abs(visualItem(mainWindow->contentItem(),"videoArea")->width()-858)<1);
                QMetaObject::invokeMethod(mainWindow,"finishWindowResize");player.setSetting("chatWidth",480);QMetaObject::invokeMethod(mainWindow,"setSidebarVisible",Q_ARG(QVariant,true));++step;
            }else if(step==17){
                mainWindow->resize(1280,740);++step;
            }else if(step==18){
                note("resizeRulesFinalGeometrySaved",saved(1280,740) && std::abs(sideItem->width()-480)<1);windows->stop();
            }
        });windows->start();
    }
    QJsonArray verification;auto stage=std::make_shared<int>(0);qulonglong beforeSwitch=0;
    QString originalAudio,lowAudio,unavailableVideo;
    auto* exercise=new QTimer(&app);exercise->setInterval(100);
    QObject::connect(exercise,&QTimer::timeout,&player,[&,stage]{
        auto state=player.state();
        if(*stage==0 && state=="播放中"){
            if(parser.isSet("embedded-main"))player.setSubtitle(0,"embedded:0");
            if(parser.isSet("subtitle-main"))player.loadSubtitle(0,QUrl::fromLocalFile(parser.value("subtitle-main")));
            if(parser.isSet("subtitle-secondary"))player.loadSubtitle(1,QUrl::fromLocalFile(parser.value("subtitle-secondary")));
            if(!parser.isSet("exercise")){exercise->stop();return;}verification.append("opened");++*stage;player.togglePause();
        }else if(*stage==1 && state=="已暂停"){verification.append("paused");++*stage;player.seek(3);}
        else if(*stage==2 && state=="已暂停" && std::abs(player.position()-3)<0.3){verification.append("seeked");++*stage;player.togglePause();}
        else if(*stage==3 && state=="播放中" && player.position()>3.6){verification.append("resumed");beforeSwitch=player.diagnostics().value("graphOpenSerial").toULongLong();++*stage;player.selectQuality(player.selectedQuality());}
        else if(*stage==4 && state=="播放中" && player.position()>5 && player.error().isEmpty() && player.diagnostics().value("graphOpenSerial").toULongLong()>beforeSwitch){
            verification.append("switched");verification.append(QJsonObject{{"beforeStop",QJsonObject::fromVariantMap(player.diagnostics())}});
            originalAudio=player.selectedAudio();
            for(const auto& entry:player.qualities()){const auto choice=entry.toMap();if(choice.value("audio").toBool() && choice.value("id").toString()!=originalAudio)lowAudio=choice.value("id").toString();if(choice.value("quality").toString()=="不可用测试流")unavailableVideo=choice.value("id").toString();}
            if(!lowAudio.isEmpty()){*stage=7;beforeSwitch=player.diagnostics().value("graphOpenSerial").toULongLong();player.selectAudio(lowAudio);}
            else {*stage=5;player.stop();}
        }
        else if(*stage==7 && state=="播放中" && player.selectedAudio()==lowAudio && player.position()>4.7 && player.error().isEmpty() && player.diagnostics().value("graphOpenSerial").toULongLong()>beforeSwitch){verification.append("audioSwitched");*stage=8;beforeSwitch=player.diagnostics().value("graphOpenSerial").toULongLong();player.selectAudio(originalAudio);}
        else if(*stage==8 && state=="播放中" && player.selectedAudio()==originalAudio && player.error().isEmpty() && player.diagnostics().value("graphOpenSerial").toULongLong()>beforeSwitch){
            verification.append("audioRestored");
            if(!unavailableVideo.isEmpty()){*stage=9;beforeSwitch=player.diagnostics().value("graphOpenSerial").toULongLong();player.selectQuality(unavailableVideo);}
            else {*stage=5;player.stop();}
        }
        else if(*stage==9 && state=="播放中" && player.selectedQuality()!=unavailableVideo && player.error().startsWith("切换失败，已恢复原流") && player.position()>4.7 && player.diagnostics().value("graphOpenSerial").toULongLong()>beforeSwitch){verification.append("failedStreamRecovered");*stage=10;beforeSwitch=player.diagnostics().value("graphOpenSerial").toULongLong();player.selectQuality(player.selectedQuality());}
        else if(*stage==10 && state=="播放中" && player.error().isEmpty() && player.diagnostics().value("graphOpenSerial").toULongLong()>beforeSwitch){*stage=5;player.stop();}
        else if(*stage==5 && state=="就绪"){verification.append("stopped");++*stage;player.open(parser.value("open"));}
        else if(*stage==6 && state=="播放中"){verification.append("reopened");exercise->stop();}
    });
    if(parser.isSet("exercise") || parser.isSet("embedded-main") || parser.isSet("subtitle-main") || parser.isSet("subtitle-secondary"))exercise->start();
    if(parser.isSet("open")){auto uri=parser.value("open");QTimer::singleShot(100,&player,[&player,uri]{player.open(uri);});}
    if(parser.isSet("smoke-test")) {
        int seconds=std::clamp(parser.value("smoke-seconds").toInt(),2,120);
        QTimer::singleShot(seconds*1000,&app,[&]{
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
            if(window && parser.isSet("screenshot"))window->grabWindow().save(parser.value("screenshot"));
            QJsonObject report{{"state",player.state()},{"error",player.error()},{"position",player.position()},{"duration",player.duration()},{"canSeek",player.canSeek()},{"hasMedia",player.hasMedia()},{"diagnostics",QJsonObject::fromVariantMap(player.diagnostics())},{"browser",player.browser()->status()},{"windowWidth",window?window->width():0},{"windowHeight",window?window->height():0},{"scale",window?window->devicePixelRatio():0}};
            report["verification"]=verification;
            report["uiVerification"]=uiVerification;
            report["savedWindowWidth"]=player.settings().value("windowWidth").toInt();report["savedWindowHeight"]=player.settings().value("windowHeight").toInt();
            report["scriptEnabled"]=player.scriptEnabled();report["selectedMainSubtitle"]=player.selectedMainSubtitle();report["selectedSecondarySubtitle"]=player.selectedSecondarySubtitle();
            report["subtitles"]=QJsonArray::fromVariantList(player.subtitles());
            if(sideItem)report["sidebarWidth"]=sideItem->width();
            report["playlistItems"]=int(player.playlist().size());int currentItems=0;for(const auto& item:player.playlist())if(item.toMap()["current"].toBool())++currentItems;report["currentPlaylistItems"]=currentItems;
            report["processId"]=QCoreApplication::applicationPid();report["mediaInformation"]=QJsonObject::fromVariantMap(player.mediaInformation());
            if(auto* list=visualItem(window->contentItem(),"playlistView")){report["playlistCurrentIndex"]=list->property("currentIndex").toInt();report["playlistContentY"]=list->property("contentY").toDouble();}
            QSaveFile file(QDir(parser.value("data-dir")).absoluteFilePath("smoke-result.json"));if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(report).toJson());file.commit();}
            app.quit();
        });
    }
    return app.exec();
}
