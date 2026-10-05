#include "PlaybackControlsProbe.h"
#include "app/PlayerController.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QColor>
#include <QFontMetricsF>
#include <QImage>
#include <cmath>
#include <limits>

static QQuickItem* findItem(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* match=findItem(child,name))return match;
    return nullptr;
}
static void clickControl(QQuickItem* item,Qt::MouseButton button=Qt::LeftButton) {
    if(!item || !item->window())return;
    auto* window=item->window();const auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    QMouseEvent press(QEvent::MouseButtonPress,p,window->mapToGlobal(p.toPoint()),button,button,Qt::NoModifier);QCoreApplication::sendEvent(window,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,p,window->mapToGlobal(p.toPoint()),button,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
}
static void keyEvent(QObject* target,int key) {
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier);QCoreApplication::sendEvent(target,&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier);QCoreApplication::sendEvent(target,&release);
}
void startPlaybackControlsProbe(bp::PlayerController& player,QQuickWindow& window,QJsonArray& report) {
    // Restore the clipboard, including non-text formats, even if a check times out.
    auto original=std::make_shared<QMimeData>();
    for(const auto& format:QGuiApplication::clipboard()->mimeData()->formats())original->setData(format,QGuiApplication::clipboard()->mimeData()->data(format));
    QObject::connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,&player,[original]{auto* copy=new QMimeData;for(const auto& format:original->formats())copy->setData(format,original->data(format));QGuiApplication::clipboard()->setMimeData(copy);});
    auto elapsed=std::make_shared<QElapsedTimer>();
    auto* timer=new QTimer(&player);timer->setInterval(250);
    QObject::connect(timer,&QTimer::timeout,&player,[&player,&window,&report,timer,elapsed,stage=0,ticks=0,start=0.0,serial=qulonglong(0),menuFont=QFont(),menuWidth=0.0]() mutable {
        auto note=[&](QString name,bool passed,double measured=-1){QJsonObject value{{"check",name},{"passed",passed}};if(measured>=0)value["measured"]=measured;report.append(value);};
        auto* url=findItem(window.contentItem(),"urlField");
        auto* side=findItem(window.contentItem(),"contentSidebar");
        auto* tabs=window.findChild<QObject*>("sideTabs");
        auto* edit=url?url->findChild<QObject*>("textEditMenu"):nullptr;
        auto nativeKey=[&](int key){keyEvent(player.videoWindow(),key);};
        auto clearFocus=[&]{window.contentItem()->forceActiveFocus();SetFocus(HWND(player.videoWindow()->winId()));};
        auto copyItem=[&](const char* name){return qobject_cast<QQuickItem*>(edit?edit->findChild<QObject*>(name):nullptr);};
        auto rateReady=[&](double value){return player.canChangePlaybackRate() && std::abs(player.playbackRate()-value)<.001;};
        if(stage==0) {
            if(player.state()!="播放中")return;
            serial=player.diagnostics().value("graphOpenSerial").toULongLong();
            note("pitchPreservingBackendAvailable",player.canChangePlaybackRate() && player.diagnostics().value("pitchPreserved").toBool() && player.diagnostics().value("tempoBackend").toString().contains("atempo"));
            clearFocus();url->setProperty("text",QString{});clickControl(url);
            ++stage;
        }else if(stage==1) {
            note("urlNativeKeyboardFocusReturns",url->hasActiveFocus() && GetFocus()==HWND(window.winId()));
            for(auto ch:QString("cxz https://fixture.test/new"))PostMessageW(GetFocus(),WM_CHAR,ch.unicode(),1);
            ++stage;
        }else if(stage==2) {
            note("urlEditableDuringNativePlayback",url->property("text").toString()=="cxz https://fixture.test/new");
            note("typingDoesNotChangePlaybackSpeed",rateReady(1));
            QMetaObject::invokeMethod(url,"selectAll");clickControl(url,Qt::RightButton);++stage;
        }else if(stage==3) {
            note("urlRightClickMenuOpened",edit && edit->property("opened").toBool());clickControl(copyItem("editCopy"));++stage;
        }else if(stage==4) {
            note("urlMenuCopiesSelection",QGuiApplication::clipboard()->text()=="cxz https://fixture.test/new");
            QGuiApplication::clipboard()->setText("https://fixture.test/pasted");QMetaObject::invokeMethod(url,"selectAll");clickControl(url,Qt::RightButton);++stage;
        }else if(stage==5) {
            clickControl(copyItem("editPaste"));++stage;
        }else if(stage==6) {
            note("urlMenuPastesClipboard",url->property("text").toString()=="https://fixture.test/pasted");
            clearFocus();if(side->isVisible())QMetaObject::invokeMethod(&window,"closeSidebar");nativeKey(Qt::Key_F6);++stage;
        }else if(stage==7) {
            note("f6OpensPlaylist",side->isVisible() && tabs->property("currentIndex").toInt()==0);nativeKey(Qt::Key_F6);++stage;
        }else if(stage==8) {
            note("f6ClosesPlaylist",!side->isVisible());nativeKey(Qt::Key_F9);++stage;
        }else if(stage==9) {
            note("f9OpensChat",side->isVisible() && tabs->property("currentIndex").toInt()==1);nativeKey(Qt::Key_F9);++stage;
        }else if(stage==10) {
            note("f9ClosesChat",!side->isVisible());nativeKey(Qt::Key_F6);++stage;
        }else if(stage==11) {
            auto* selected=window.findChild<QObject*>("playlistTab");auto* unselected=window.findChild<QObject*>("chatTab");
            auto background=[](QObject* tab){return tab->property("background").value<QObject*>()->property("color").value<QColor>();};
            note("activeSidebarTabUsesRaisedColor",background(selected)==QColor(player.theme().value("raised").toString()) && background(unselected)==QColor(player.theme().value("background").toString()));
            player.setSetting("theme","light");QMetaObject::invokeMethod(&window,"toggleVideoFullscreen");nativeKey(Qt::Key_F9);ticks=0;++stage;
        }else if(stage==12) {
            auto background=[](QObject* tab){return tab->property("background").value<QObject*>()->property("color").value<QColor>();};
            const bool colorsMatch=background(window.findChild<QObject*>("chatTab"))==QColor(player.theme().value("raised").toString()) && background(window.findChild<QObject*>("playlistTab"))==QColor(player.theme().value("background").toString());
            // Fullscreen window creation can delay the first animation frame.
            // Check its final colors after settling, with a bounded deadline.
            if(!colorsMatch && ++ticks<5)return;
            note("f9FullscreenOpensManualSidebar",window.property("fullscreenSidebarVisible").toBool() && tabs->property("currentIndex").toInt()==1);
            note("sidebarTabsFollowLightTheme",colorsMatch);
            player.setSetting("theme","dark");nativeKey(Qt::Key_F6);++stage;
        }else if(stage==13) {
            note("f6FullscreenSwitchesToPlaylist",window.property("fullscreenSidebarVisible").toBool() && tabs->property("currentIndex").toInt()==0);nativeKey(Qt::Key_F6);++stage;
        }else if(stage==14) {
            note("f6FullscreenClosesSidebar",!window.property("fullscreenSidebarVisible").toBool());QMetaObject::invokeMethod(&window,"toggleVideoFullscreen");++stage;
        }else if(stage==15) {
            clickControl(findItem(window.contentItem(),"speedButton"));++stage;
        }else if(stage==16) {
            auto* button=findItem(window.contentItem(),"speedButton");auto* menu=button?button->findChild<QObject*>("speedMenu"):nullptr;
            note("speedMenuOpened",menu && menu->property("opened").toBool());
            auto* content=menu?qobject_cast<QQuickItem*>(menu->property("contentItem").value<QObject*>()):nullptr;
            auto* row=content?findItem(content,"speed_2"):nullptr;
            note("speedMenuRowAvailable",row && row->isVisible() && row->isEnabled());
            menuFont=menu->property("font").value<QFont>();menuWidth=menu->property("width").toDouble();
            const auto textWidth=QFontMetricsF(menuFont).horizontalAdvance(QStringLiteral("1× · 正常速度"));
            note("speedMenuFitsTextWithoutExcessWidth",menuWidth>=textWidth+30 && menuWidth<=textWidth+42,menuWidth);
            auto* indicator=row?row->property("indicator").value<QObject*>():nullptr;
            note("menuCheckMatchesTextSize",indicator && indicator->property("width").toDouble()==menuFont.pixelSize());
            note("parsePlaylistOptionRemoved",!findItem(window.contentItem(),"parsePlaylist"));
            if(row && row->window())row->window()->grabWindow().save(".local/speed-menu.png");
            auto larger=menuFont;larger.setPixelSize(menuFont.pixelSize()+6);menu->setProperty("font",larger);stage=161;
        }else if(stage==161) {
            auto* menu=findItem(window.contentItem(),"speedButton")->findChild<QObject*>("speedMenu");
            auto* content=qobject_cast<QQuickItem*>(menu->property("contentItem").value<QObject*>());
            auto* row=findItem(content,"speed_2");auto* indicator=row->property("indicator").value<QObject*>();
            note("speedMenuAndCheckGrowWithFont",menu->property("width").toDouble()>menuWidth+20 && indicator->property("width").toDouble()==menuFont.pixelSize()+6);
            menu->setProperty("font",menuFont);stage=162;
        }else if(stage==162) {
            auto* menu=findItem(window.contentItem(),"speedButton")->findChild<QObject*>("speedMenu");
            auto* content=qobject_cast<QQuickItem*>(menu->property("contentItem").value<QObject*>());
            clickControl(findItem(content,"speed_2"));stage=17;
        }else if(stage==17) {
            if(!rateReady(2))return;
            note("menuChangesActualRate",player.diagnostics().value("playbackRate").toDouble()==2 && serial==player.diagnostics().value("graphOpenSerial").toULongLong());
            start=player.position();elapsed->restart();ticks=0;++stage;
        }else if(stage==18) {
            if(elapsed->elapsed()<2000)return;
            const double measured=(player.position()-start)/(elapsed->elapsed()/1000.0);
            note("doubleSpeedMediaClock",measured>1.65 && measured<2.35,measured);player.setPlaybackRate(.5);++stage;
        }else if(stage==19) {
            if(!rateReady(.5))return;start=player.position();elapsed->restart();++stage;
        }else if(stage==20) {
            if(elapsed->elapsed()<2000)return;
            const double measured=(player.position()-start)/(elapsed->elapsed()/1000.0);
            note("halfSpeedMediaClock",measured>.35 && measured<.7,measured);
            player.setPlaybackRate(0);player.setPlaybackRate(5);player.setPlaybackRate(std::numeric_limits<double>::quiet_NaN());
            note("invalidRatesRejected",rateReady(.5));clearFocus();nativeKey(Qt::Key_Z);++stage;
        }else if(stage==21) {
            if(!rateReady(1))return;note("zRestoresNormalRate",true);nativeKey(Qt::Key_C);++stage;
        }else if(stage==22) {
            if(!rateReady(1.25))return;note("cIncreasesRate",true);nativeKey(Qt::Key_X);++stage;
        }else if(stage==23) {
            if(!rateReady(1))return;note("xDecreasesRate",true);window.contentItem()->forceActiveFocus();player.focusInputWindow(&window);
            const auto scan=LPARAM(MapVirtualKeyW('C',MAPVK_VK_TO_VSC))<<16;
            PostMessageW(HWND(window.winId()),WM_KEYDOWN,'C',scan|1);PostMessageW(HWND(window.winId()),WM_KEYUP,'C',scan|LPARAM(0xc0000001));++stage;
        }else if(stage==24) {
            if(!rateReady(1.25))return;note("qmlWindowSpeedShortcut",true);player.togglePause();++stage;
        }else if(stage==25) {
            if(player.state()!="已暂停")return;start=player.position();player.setPlaybackRate(2);ticks=0;++stage;
        }else if(stage==26) {
            if(!rateReady(2) || ++ticks<3)return;
            note("pausedRateChangeDoesNotResume",player.state()=="已暂停" && std::abs(player.position()-start)<.2);player.seek(3);++stage;
        }else if(stage==27) {
            if(player.state()!="已暂停" || std::abs(player.position()-3)>.3)return;
            note("pausedSeekRetainsRate",rateReady(2));player.togglePause();++stage;
        }else if(stage==28) {
            if(player.state()!="播放中")return;serial=player.diagnostics().value("graphOpenSerial").toULongLong();player.selectQuality(player.selectedQuality());++stage;
        }else if(stage==29) {
            if(player.state()!="播放中" || player.diagnostics().value("graphOpenSerial").toULongLong()==serial)return;
            note("streamRebuildRetainsSpeed",rateReady(2) && player.diagnostics().value("playbackRate").toDouble()==2);
            start=player.position();elapsed->restart();stage=290;
        }else if(stage==290) {
            if(elapsed->elapsed()<2000)return;
            const double measured=(player.position()-start)/(elapsed->elapsed()/1000.0);
            note("rebuiltMediaClockRetainsSpeed",measured>1.65 && measured<2.35,measured);player.setPlaybackRate(1);stage=30;
        }else if(stage==30) {
            if(!rateReady(1))return;
            note("normalSpeedAndVideoRestored",player.error().isEmpty() && player.diagnostics().value("rendererFramesDrawn").toInt()>0);
            player.setSetting("scriptEnabled",false);nativeKey(Qt::Key_F9);++stage;
        }else if(stage==31) {
            note("f9DisabledInLocalMode",!window.findChild<QObject*>("chatTab")->property("visible").toBool());timer->stop();
        }
    });
    timer->start();
}
