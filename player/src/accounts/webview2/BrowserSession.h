#pragma once
#include <Windows.h>
#include <Unknwn.h>
#include "network/HttpService.h"
#include <QObject>
#include <QWindow>
#include <QTimer>
#include <WebView2.h>
#include <wrl/client.h>
#include <memory>
namespace bp {
class BrowserSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* chatWindow READ chatWindow CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    BrowserSession(HttpService* http,QString dataDirectory);
    ~BrowserSession();
    QWindow* chatWindow() const{return chatHost_;}
    QString status() const{return status_;}
    void syncCookies(std::function<void()> complete);
    void applyCookies(std::vector<Cookie> cookies,uint64_t epoch);
    void login(QString url);
    void setChat(std::optional<ChatPage> page);
    void closeChat();
    void logout(std::function<void()> complete);
signals:
    void statusChanged();
    void accountChanged();
    void sidebarPageRequested(int page);
private:
    struct View;
    HttpService* http_;
    QString dataDirectory_,status_="浏览器未初始化";
    QWindow* chatHost_;
    std::shared_ptr<View> chat_;
    std::vector<std::shared_ptr<View>> windows_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookies_;
    Microsoft::WRL::ComPtr<ICoreWebView2Profile2> profile_;
    HMODULE loader_=nullptr;
    bool creating_=false,syncing_=false;
    std::vector<std::function<void()>> waiters_,cookieWaiters_;
    std::optional<ChatPage> desiredChat_;
    QTimer cookieTimer_;
    uint64_t epoch_=1,chatSerial_=0,cookieSerial_=0,ensureSerial_=0;
    void ensure(std::function<void()> complete);
    void createView(QWindow* host,bool chat,std::function<void(std::shared_ptr<View>)> complete);
    void navigateChat();
    void resizeView(std::shared_ptr<View> view);
    void setStatus(QString message);
};
}
