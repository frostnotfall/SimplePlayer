#pragma once
#include "domain/Models.h"
#include <QByteArray>
#include <QThread>
#include <QUrl>
#include <functional>
#include <map>
#include <future>
class QNetworkAccessManager;
namespace bp {
struct RequestPolicy { std::string userAgent, referer; };
struct HttpRequest {
    std::string url, method="GET", body, headers;
    bool noCookie=false;
    size_t limit=32*1024*1024;
    int timeoutMs=20000;
    Cancel cancel;
    uint64_t epoch=0;
    std::chrono::steady_clock::time_point deadline{};
};
struct HttpResponse {
    int status=0;
    std::string body, finalUrl, error;
    std::vector<std::pair<std::string,std::string>> headers;
};
class HttpService {
public:
    HttpService();
    ~HttpService();
    HttpResponse request(HttpRequest request);
    void publishCookies(CookieSnapshot snapshot);
    bool publishCookiesIfUnchanged(CookieSnapshot snapshot,uint64_t expectedRevision);
    CookieSnapshot snapshot() const;
    std::string cookieHeader(const QUrl& url,uint64_t epoch) const;
    void setUserAgent(std::string domain,std::string value);
    void setReferer(std::string domain,std::string value);
    RequestPolicy policyFor(const std::string& url) const;
    std::function<void(std::vector<Cookie>,uint64_t)> cookieChanges;
private:
    QThread thread_;
    QObject* worker_=nullptr;
    QNetworkAccessManager* manager_=nullptr;
    mutable std::mutex mutex_;
    CookieSnapshot cookies_;
    std::map<std::string,RequestPolicy> policies_;
    void startRequest(HttpRequest request,std::shared_ptr<std::promise<HttpResponse>> completion,int redirects=0);
};
}
