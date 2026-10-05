#include "BrowserSession.h"
#include <wrl.h>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>
#include <shlwapi.h>

namespace bp {
using Microsoft::WRL::Callback;using Microsoft::WRL::ComPtr;
struct BrowserSession::View {
    QPointer<QWindow> host;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::wstring registration;
    bool chat=false;
    ~View(){if(controller)controller->Close();}
};
static std::string takeString(LPWSTR value){std::string result=value?QString::fromWCharArray(value).toUtf8().toStdString():"";CoTaskMemFree(value);return result;}
static std::wstring wide(const std::string& value){return QString::fromUtf8(value).toStdWString();}
static QString origin(const QString& value){QUrl u(value);return u.scheme()+"://"+u.host()+(u.port()!=-1?":"+QString::number(u.port()):QString{});}
static QString jsString(const QString& value){auto bytes=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}
BrowserSession::BrowserSession(HttpService* http,QString directory):http_(http),dataDirectory_(QDir(directory).absolutePath()) {
    chatHost_=new QWindow;chatHost_->setFlags(Qt::FramelessWindowHint);chatHost_->create();
    cookieTimer_.setInterval(3000);connect(&cookieTimer_,&QTimer::timeout,this,[this]{if(cookies_)syncCookies([]{});});cookieTimer_.start();
}
BrowserSession::~BrowserSession(){cookieTimer_.stop();chat_.reset();for(auto& view:windows_){if(view->controller)view->controller->Close();if(view->host)delete view->host.data();}windows_.clear();cookies_.Reset();profile_.Reset();environment_.Reset();delete chatHost_;if(loader_)FreeLibrary(loader_);}
void BrowserSession::setStatus(QString message){status_=std::move(message);emit statusChanged();}
void BrowserSession::ensure(std::function<void()> complete) {
    if(chat_ && cookies_){complete();return;}
    waiters_.push_back(std::move(complete));if(creating_)return;creating_=true;
    auto serial=++ensureSerial_;
    QTimer::singleShot(10000,this,[this,serial]{if(serial!=ensureSerial_ || !creating_)return;++ensureSerial_;creating_=false;setStatus("WebView2 初始化超时，可继续匿名播放");auto callbacks=std::move(waiters_);for(auto& cb:callbacks)cb();});
    if(environment_){createView(chatHost_,true,[this,serial](auto view){if(serial!=ensureSerial_)return;chat_=view;creating_=false;auto callbacks=std::move(waiters_);for(auto& callback:callbacks)callback();});return;}
    auto dll=QDir(QCoreApplication::applicationDirPath()).filePath("WebView2Loader.dll");
    if(!loader_)loader_=LoadLibraryExW(reinterpret_cast<LPCWSTR>(dll.utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    using Create=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=loader_?reinterpret_cast<Create>(GetProcAddress(loader_,"CreateCoreWebView2EnvironmentWithOptions")):nullptr;
    auto fail=[this]{creating_=false;setStatus("WebView2 Runtime 或 Loader 缺失，可继续匿名播放");auto callbacks=std::move(waiters_);for(auto& cb:callbacks)cb();};
    if(!create){fail();return;}
    QDir().mkpath(dataDirectory_);auto folder=dataDirectory_.toStdWString();QPointer<BrowserSession> self(this);
    auto handler=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([self,serial](HRESULT hr,ICoreWebView2Environment* environment)->HRESULT {
        if(!self || serial!=self->ensureSerial_)return S_OK;
        if(FAILED(hr) || !environment){self->creating_=false;self->setStatus("WebView2 Runtime 无法初始化，可继续匿名播放");auto callbacks=std::move(self->waiters_);for(auto& cb:callbacks)cb();return S_OK;}
        self->environment_=environment;
        self->createView(self->chatHost_,true,[self,serial](auto view){if(!self || serial!=self->ensureSerial_)return;self->chat_=view;self->creating_=false;auto callbacks=std::move(self->waiters_);for(auto& cb:callbacks)cb();});return S_OK;
    });
    if(FAILED(create(nullptr,folder.c_str(),nullptr,handler.Get())))fail();
}
void BrowserSession::resizeView(std::shared_ptr<View> view) {
    if(!view->controller || !view->host)return;auto* host=view->host.data();double scale=host->devicePixelRatio();
    RECT bounds{0,0,LONG(host->width()*scale),LONG(host->height()*scale)};view->controller->put_Bounds(bounds);view->controller->put_IsVisible(host->isVisible());view->controller->NotifyParentWindowPositionChanged();
}
void BrowserSession::createView(QWindow* host,bool isChat,std::function<void(std::shared_ptr<View>)> complete) {
    if(!environment_){complete({});return;}
    QPointer<BrowserSession> self(this);QPointer<QWindow> target(host);
    auto handler=Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([self,target,isChat,complete](HRESULT hr,ICoreWebView2Controller* controller)->HRESULT {
        if(!self || !target){if(controller)controller->Close();return S_OK;}
        if(FAILED(hr) || !controller){self->setStatus(QString("浏览器窗口创建失败 (0x%1)").arg(uint32_t(hr),8,16,QChar('0')));complete({});return S_OK;}
        auto view=std::make_shared<View>();view->host=target;view->controller=controller;view->chat=isChat;controller->get_CoreWebView2(&view->web);
        ComPtr<ICoreWebView2Settings> settings;view->web->get_Settings(&settings);settings->put_AreDefaultContextMenusEnabled(TRUE);settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsWebMessageEnabled(TRUE);
        ComPtr<ICoreWebView2_8> audio;if(isChat && SUCCEEDED(view->web.As(&audio)))audio->put_IsMuted(TRUE);
        ComPtr<ICoreWebView2_2> v2;if(SUCCEEDED(view->web.As(&v2)))v2->get_CookieManager(&self->cookies_);
        ComPtr<ICoreWebView2_13> v13;if(SUCCEEDED(view->web.As(&v13))){ComPtr<ICoreWebView2Profile> profile;v13->get_Profile(&profile);if(profile)profile.As(&self->profile_);}
        auto weak=std::weak_ptr<View>(view);
        auto update=[self,weak]{if(self)if(auto v=weak.lock())self->resizeView(v);};
        QObject::connect(target,&QWindow::widthChanged,self,update);QObject::connect(target,&QWindow::heightChanged,self,update);QObject::connect(target,&QWindow::visibleChanged,self,update);QObject::connect(target,&QWindow::xChanged,self,update);QObject::connect(target,&QWindow::yChanged,self,update);
        EventRegistrationToken token;
        if(isChat)controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>([self,weak](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT {
            if(!self || weak.expired())return S_OK;
            UINT key=0;COREWEBVIEW2_KEY_EVENT_KIND kind;COREWEBVIEW2_PHYSICAL_KEY_STATUS physical{};
            if(FAILED(args->get_VirtualKey(&key)) || (key!=VK_F6 && key!=VK_F9) ||
               (GetKeyState(VK_CONTROL)&0x8000) || (GetKeyState(VK_SHIFT)&0x8000) || (GetKeyState(VK_MENU)&0x8000))return S_OK;
            args->put_Handled(TRUE);
            if(SUCCEEDED(args->get_KeyEventKind(&kind)) && kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
               SUCCEEDED(args->get_PhysicalKeyStatus(&physical)) && !physical.WasKeyDown) {
                // A browser accelerator callback is synchronous. Change Qt
                // geometry/visibility only after returning to the event loop.
                QMetaObject::invokeMethod(self,[self,key]{if(self)emit self->sidebarPageRequested(key==VK_F6?0:1);},Qt::QueuedConnection);
            }
            return S_OK;
        }).Get(),&token);
        view->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([self,weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
            if(self && !weak.expired()){BOOL success=FALSE;args->get_IsSuccess(&success);self->setStatus(success?"浏览器页面已加载":"浏览器页面加载失败");self->syncCookies([self]{if(self)emit self->accountChanged();});}return S_OK;
        }).Get(),&token);
        view->web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([self,weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
            if(!self || weak.expired())return S_OK;LPWSTR raw=nullptr;args->get_WebMessageAsJson(&raw);auto message=takeString(raw);
            if(message.size()<4096){auto object=QJsonDocument::fromJson(QByteArray::fromStdString(message)).object();if(object.value("type")=="bp.chat.error")self->setStatus("聊天脚本运行异常，视频播放可继续");}
            return S_OK;
        }).Get(),&token);
        view->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([self](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT {
            args->put_Handled(TRUE);if(!self)return S_OK;LPWSTR raw=nullptr;args->get_Uri(&raw);auto uri=takeString(raw);if(!supportedUrl(uri))return S_OK;
            auto* popup=new QWindow;popup->setTitle("SimplePlayer · 网页");popup->resize(880,720);popup->show();
            ComPtr<ICoreWebView2Deferral> deferral;args->GetDeferral(&deferral);ComPtr<ICoreWebView2NewWindowRequestedEventArgs> retained=args;
            self->createView(popup,false,[self,popup,uri,deferral,retained](auto v){if(self && v){self->windows_.push_back(v);retained->put_NewWindow(v->web.Get());}else popup->deleteLater();deferral->Complete();});return S_OK;
        }).Get(),&token);
        self->resizeView(view);self->setStatus("共享浏览器 Profile 已就绪");complete(view);return S_OK;
    });
    if(FAILED(environment_->CreateCoreWebView2Controller(HWND(host->winId()),handler.Get()))){setStatus("浏览器 controller 创建失败");complete({});}
}
void BrowserSession::syncCookies(std::function<void()> complete) {
    if(!cookies_){ensure([this,complete]{if(cookies_)syncCookies(complete);else complete();});return;}
    cookieWaiters_.push_back(std::move(complete));if(syncing_)return;syncing_=true;auto epoch=epoch_;auto serial=++cookieSerial_;QPointer<BrowserSession> self(this);
    QTimer::singleShot(3000,this,[this,serial]{if(serial!=cookieSerial_ || !syncing_)return;++cookieSerial_;syncing_=false;setStatus("Cookie 同步超时，使用现有快照");auto callbacks=std::move(cookieWaiters_);for(auto& cb:callbacks)cb();});
    const auto initialRevision=http_->snapshot().revision;
    auto handler=Callback<ICoreWebView2GetCookiesCompletedHandler>([self,epoch,serial,initialRevision](HRESULT hr,ICoreWebView2CookieList* list)->HRESULT {
        if(!self || serial!=self->cookieSerial_)return S_OK;self->syncing_=false;
        if(SUCCEEDED(hr) && list && epoch==self->epoch_) {
            CookieSnapshot snapshot;snapshot.epoch=epoch;UINT count=0;list->get_Count(&count);count=std::min<UINT>(count,10000);
            for(UINT i=0;i<count;++i){ComPtr<ICoreWebView2Cookie> cookie;list->GetValueAtIndex(i,&cookie);Cookie c;LPWSTR raw=nullptr;
                cookie->get_Name(&raw);c.name=takeString(raw);cookie->get_Value(&raw);c.value=takeString(raw);cookie->get_Domain(&raw);c.domain=takeString(raw);cookie->get_Path(&raw);c.path=takeString(raw);
                BOOL secure,httpOnly,session;double expiry;cookie->get_IsSecure(&secure);cookie->get_IsHttpOnly(&httpOnly);cookie->get_IsSession(&session);cookie->get_Expires(&expiry);
                c.secure=secure;c.httpOnly=httpOnly;c.hostOnly=!c.domain.starts_with('.');c.expires=session?0:int64_t(expiry);
                COREWEBVIEW2_COOKIE_SAME_SITE_KIND sameSite=COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX;
                if(SUCCEEDED(cookie->get_SameSite(&sameSite)))c.sameSite=sameSite==COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE?Cookie::SameSite::None:sameSite==COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT?Cookie::SameSite::Strict:Cookie::SameSite::Lax;
                snapshot.cookies.push_back(std::move(c));
            }
            // A browser read begun before an HTTP Set-Cookie must not overwrite the newer snapshot.
            // HTTP changes are already queued to CookieManager; the next browser sync reads them back.
            self->http_->publishCookiesIfUnchanged(std::move(snapshot),initialRevision);
        }
        auto callbacks=std::move(self->cookieWaiters_);for(auto& cb:callbacks)cb();return S_OK;
    });
    if(FAILED(cookies_->GetCookies(nullptr,handler.Get()))){syncing_=false;auto callbacks=std::move(cookieWaiters_);for(auto& cb:callbacks)cb();}
}
void BrowserSession::applyCookies(std::vector<Cookie> changes,uint64_t epoch) {
    if(epoch!=epoch_ || !cookies_)return;
    for(const auto& c:changes){ComPtr<ICoreWebView2Cookie> cookie;auto name=wide(c.name),value=wide(c.value),domain=wide(c.domain),path=wide(c.path);
        if(SUCCEEDED(cookies_->CreateCookie(name.c_str(),value.c_str(),domain.c_str(),path.c_str(),&cookie))){cookie->put_IsSecure(c.secure);cookie->put_IsHttpOnly(c.httpOnly);if(c.expires)cookie->put_Expires(double(c.expires));
            if(c.sameSite!=Cookie::SameSite::Unspecified)cookie->put_SameSite(c.sameSite==Cookie::SameSite::None?COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE:c.sameSite==Cookie::SameSite::Strict?COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT:COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
            cookies_->AddOrUpdateCookie(cookie.Get());}}
    syncCookies([]{});
}
void BrowserSession::login(QString url) {
    if(!supportedUrl(url.toStdString())){setStatus("脚本没有提供有效登录网页");return;}
    ensure([this,url]{if(!environment_)return;auto* host=new QWindow;host->setTitle("SimplePlayer · 登录");host->resize(920,740);host->show();
        createView(host,false,[this,host,url](auto view){if(!view){host->deleteLater();return;}windows_.push_back(view);auto address=url.toStdWString();view->web->Navigate(address.c_str());});});
}
void BrowserSession::setChat(std::optional<ChatPage> page) {
    if(!page){closeChat();return;}
    if(desiredChat_ && page && desiredChat_->url==page->url && desiredChat_->script==page->script && desiredChat_->key.session==page->key.session){desiredChat_=std::move(page);return;}
    desiredChat_=std::move(page);++chatSerial_;ensure([this]{navigateChat();});
}
void BrowserSession::closeChat(){desiredChat_.reset();++chatSerial_;if(chat_ && chat_->web){chat_->web->Stop();if(!chat_->registration.empty()){chat_->web->RemoveScriptToExecuteOnDocumentCreated(chat_->registration.c_str());chat_->registration.clear();}chat_->web->Navigate(L"about:blank");}}
void BrowserSession::navigateChat() {
    if(!chat_ || !chat_->web || !desiredChat_)return;auto web=chat_->web;
    web->Stop();if(!chat_->registration.empty()){web->RemoveScriptToExecuteOnDocumentCreated(chat_->registration.c_str());chat_->registration.clear();}
    auto spec=*desiredChat_;auto serial=chatSerial_;auto address=wide(spec.url);
    if(spec.script.empty()){web->Navigate(address.c_str());return;}
    auto allowed=jsString(origin(QString::fromUtf8(spec.url)));
    auto code=QString("(()=>{if(window.top!==window || location.origin!==%1)return;const run=()=>{if(window.__bpChatDocument)return;window.__bpChatDocument=true;try{(function(){\n%2\n}).call(window);}catch(e){window.chrome?.webview?.postMessage({type:'bp.chat.error'});}};if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',run,{once:true});else run();})();").arg(allowed,QString::fromUtf8(spec.script));
    QPointer<BrowserSession> self(this);auto script=code.toStdWString();
    auto handler=Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([self,web,serial,address](HRESULT hr,LPCWSTR id)->HRESULT {
        if(!self || serial!=self->chatSerial_){if(id)web->RemoveScriptToExecuteOnDocumentCreated(id);return S_OK;}
        if(FAILED(hr)){self->setStatus("聊天脚本注册失败");return S_OK;}self->chat_->registration=id;web->Navigate(address.c_str());return S_OK;
    });web->AddScriptToExecuteOnDocumentCreated(script.c_str(),handler.Get());
}
void BrowserSession::logout(std::function<void()> complete) {
    ++epoch_;http_->publishCookies({epoch_,0,{}});closeChat();
    if(chat_ && chat_->controller)chat_->controller->Close();chat_.reset();
    for(auto& v:windows_){if(v->controller)v->controller->Close();if(v->host)v->host->deleteLater();}windows_.clear();
    if(cookies_)cookies_->DeleteAllCookies();
    QPointer<BrowserSession> self(this);auto finish=[self,complete]{if(self){self->setStatus("已清除全局登录状态");emit self->accountChanged();}complete();};
    if(profile_){auto handler=Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>([finish](HRESULT)->HRESULT{finish();return S_OK;});if(SUCCEEDED(profile_->ClearBrowsingDataAll(handler.Get())))return;}
    finish();
}
}
