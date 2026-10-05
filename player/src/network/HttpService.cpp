#include "HttpService.h"
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>

namespace bp {
HttpService::HttpService() {
    worker_=new QObject; worker_->moveToThread(&thread_);
    QObject::connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
    thread_.setObjectName("NetworkThread"); thread_.start();
    QMetaObject::invokeMethod(worker_,[this]{manager_=new QNetworkAccessManager(worker_);},Qt::BlockingQueuedConnection);
}
HttpService::~HttpService() { thread_.quit(); thread_.wait(); }
CookieSnapshot HttpService::snapshot() const { std::lock_guard lock(mutex_); return cookies_; }
void HttpService::publishCookies(CookieSnapshot snapshot) {
    std::lock_guard lock(mutex_);
    if(snapshot.epoch>cookies_.epoch || snapshot.epoch==cookies_.epoch && snapshot.revision>=cookies_.revision) cookies_=std::move(snapshot);
}
bool HttpService::publishCookiesIfUnchanged(CookieSnapshot snapshot,uint64_t expectedRevision) {
    std::lock_guard lock(mutex_);
    if(snapshot.epoch!=cookies_.epoch || expectedRevision!=cookies_.revision)return false;
    snapshot.revision=expectedRevision+1;cookies_=std::move(snapshot);return true;
}
void HttpService::setUserAgent(std::string domain,std::string value){std::lock_guard lock(mutex_); policies_[std::move(domain)].userAgent=std::move(value);}
void HttpService::setReferer(std::string domain,std::string value){std::lock_guard lock(mutex_); policies_[std::move(domain)].referer=std::move(value);}
RequestPolicy HttpService::policyFor(const std::string& input) const {
    QUrl url(QString::fromStdString(input)); auto host=url.host().toStdString();
    std::lock_guard lock(mutex_); RequestPolicy result;
    size_t longest=0;
    for(const auto& [domain,policy]:policies_) if(domain.size()>=longest && domainMatches(host,domain)) {result=policy; longest=domain.size();}
    return result;
}
std::string HttpService::cookieHeader(const QUrl& url,uint64_t epoch) const {
    auto snapshot=this->snapshot(); if(epoch!=snapshot.epoch) return {};
    std::vector<Cookie> matches;
    for(const auto& c:snapshot.cookies)
        if(domainMatches(url.host().toStdString(),c.domain,c.hostOnly) && cookiePathMatches(url.path().isEmpty()?"/":url.path().toStdString(),c.path)
           && (!c.secure || url.scheme()=="https") && (!c.expires || c.expires>QDateTime::currentSecsSinceEpoch())) matches.push_back(c);
    std::stable_sort(matches.begin(),matches.end(),[](const Cookie& a,const Cookie& b){return a.path.size()>b.path.size();});
    std::string header;
    for(const auto& c:matches) { if(!header.empty())header+="; "; header+=c.name+"="+c.value; }
    return header;
}
HttpResponse HttpService::request(HttpRequest request) {
    if(QThread::currentThread()==&thread_) throw Error("NetworkConnect","禁止在网络线程同步等待请求");
    if(!request.cancel) request.cancel=std::make_shared<Cancellation>();
    request.cancel->check(); if(!request.epoch) request.epoch=snapshot().epoch;
    request.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::max(1,request.timeoutMs));
    auto promise=std::make_shared<std::promise<HttpResponse>>(); auto future=promise->get_future();
    QMetaObject::invokeMethod(worker_,[this,request=std::move(request),promise]() mutable {startRequest(std::move(request),promise);});
    return future.get();
}
void HttpService::startRequest(HttpRequest request,std::shared_ptr<std::promise<HttpResponse>> completion,int redirects) {
    if(request.cancel->cancelled){completion->set_value({0,{},{},"Cancelled"});return;}
    auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(request.deadline-std::chrono::steady_clock::now()).count();
    if(remaining<=0){completion->set_value({0,{},{},"Timeout"});return;}
    request.timeoutMs=int(std::min<int64_t>(remaining,20000));
    QUrl url(QString::fromUtf8(request.url));
    if(!supportedUrl(request.url)){completion->set_value({0,{},{},"Invalid URL"});return;}
    QNetworkRequest qrequest(url);
    qrequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    qrequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    qrequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    auto policy=policyFor(request.url);
    if(!policy.userAgent.empty())qrequest.setRawHeader("User-Agent",QByteArray::fromStdString(policy.userAgent));
    if(!policy.referer.empty())qrequest.setRawHeader("Referer",QByteArray::fromStdString(policy.referer));
    if(!request.noCookie){auto cookie=cookieHeader(url,request.epoch); if(!cookie.empty())qrequest.setRawHeader("Cookie",QByteArray::fromStdString(cookie));}
    for(auto line:QByteArray::fromStdString(request.headers).split('\n')) {
        line=line.trimmed(); auto pos=line.indexOf(':');
        if(pos>0) {auto name=line.left(pos).trimmed();auto value=line.mid(pos+1).trimmed();
            if(request.noCookie && name.toLower()=="cookie")continue;
            if(!value.contains('\r') && !name.contains(' '))qrequest.setRawHeader(name,value);
        }
    }
    // Use our deadline/cancellation timer: Qt's transfer timer can finish with
    // OperationCanceledError before we can record that the cause was a timeout.
    qrequest.setTransferTimeout(0);
    auto* reply=manager_->sendCustomRequest(qrequest,QByteArray::fromStdString(request.method),QByteArray::fromStdString(request.body));
    auto data=std::make_shared<QByteArray>(); auto reason=std::make_shared<std::string>();
    auto timer=new QTimer(reply); timer->setTimerType(Qt::PreciseTimer);timer->setInterval(20);
    auto clock=std::make_shared<QElapsedTimer>();clock->start();
    QObject::connect(timer,&QTimer::timeout,reply,[reply,request,clock,reason]{
        if(request.cancel->cancelled || clock->elapsed()>=request.timeoutMs) {*reason=request.cancel->cancelled?"Cancelled":"Timeout";reply->abort();}
    });timer->start();
    QObject::connect(reply,&QIODevice::readyRead,reply,[reply,data,request,reason]{
        *data+=reply->readAll(); if(size_t(data->size())>request.limit){*reason="ResponseLimit";reply->abort();}
    });
    QObject::connect(reply,&QNetworkReply::finished,reply,[this,reply,data,reason,request,completion,redirects]() mutable {
        if(reply->isOpen())*data+=reply->readAll();
        HttpResponse response; response.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.finalUrl=reply->url().toString(QUrl::FullyEncoded).toStdString();
        if(size_t(data->size())>request.limit) *reason="ResponseLimit";
        if(!reason->empty())response.error=*reason;
        else if(reply->error()==QNetworkReply::TimeoutError)response.error="Timeout";
        else if(request.cancel->cancelled)response.error="Cancelled";
        else if(reply->error()!=QNetworkReply::NoError && response.status==0)response.error=reply->errorString().toStdString();
        if(response.error!="ResponseLimit")response.body=data->toStdString();
        std::vector<Cookie> changes;
        for(const auto& header:reply->rawHeaderPairs()) {
            response.headers.emplace_back(header.first.toStdString(),header.second.toStdString());
            if(!request.noCookie && header.first.toLower()=="set-cookie")for(auto qc:QNetworkCookie::parseCookies(header.second)) {
                bool hostOnly=qc.domain().isEmpty(),defaultPath=qc.path().isEmpty();qc.normalize(reply->url());
                if(defaultPath){const auto originPath=reply->url().path();const auto lastSlash=originPath.lastIndexOf('/');qc.setPath(lastSlash>0?originPath.left(lastSlash):QString("/"));}
                auto domain=qc.domain().toLower().toStdString();
                if(!domainMatches(reply->url().host().toStdString(),domain,hostOnly))continue;
                Cookie c{qc.name().toStdString(),qc.value().toStdString(),domain,qc.path().isEmpty()?"/":qc.path().toStdString(),qc.isSecure(),qc.isHttpOnly(),hostOnly,qc.isSessionCookie()?0:qc.expirationDate().toSecsSinceEpoch()};
                switch(qc.sameSitePolicy()) {
                case QNetworkCookie::SameSite::None:c.sameSite=Cookie::SameSite::None;break;
                case QNetworkCookie::SameSite::Lax:c.sameSite=Cookie::SameSite::Lax;break;
                case QNetworkCookie::SameSite::Strict:c.sameSite=Cookie::SameSite::Strict;break;
                default:break;
                }
                changes.push_back(std::move(c));
            }
        }
        if(!changes.empty()) {
            bool accepted=false;
            {std::lock_guard lock(mutex_);if(request.epoch==cookies_.epoch) {
                for(const auto& c:changes) {
                    std::erase_if(cookies_.cookies,[&](const Cookie& other){return c.name==other.name && c.domain==other.domain && c.path==other.path;});
                    if(!c.expires || c.expires>QDateTime::currentSecsSinceEpoch())cookies_.cookies.push_back(c);
                }
                ++cookies_.revision;accepted=true;
            }}
            if(accepted && cookieChanges)cookieChanges(std::move(changes),request.epoch);
        }
        auto redirect=reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        auto origin=reply->url();reply->deleteLater();
        if(!redirect.isEmpty() && response.error.empty()) {
            if(redirects>=8){response.error="RedirectLimit";completion->set_value(std::move(response));return;}
            auto target=origin.resolved(redirect);
            if(origin.scheme()=="https" && target.scheme()!="https"){response.error="UnsafeRedirect";completion->set_value(std::move(response));return;}
            if(origin.host()!=target.host() || origin.port()!=target.port()) {
                QByteArray clean;
                for(auto line:QByteArray::fromStdString(request.headers).split('\n')) {
                    auto name=line.left(line.indexOf(':')).trimmed().toLower();
                    if(name!="cookie" && name!="authorization" && name!="proxy-authorization")clean+=line+'\n';
                }request.headers=clean.toStdString();
            }
            if(response.status==303 || (response.status==301 || response.status==302) && request.method=="POST") {request.method="GET";request.body.clear();}
            request.url=target.toString(QUrl::FullyEncoded).toStdString();
            startRequest(std::move(request),completion,redirects+1);return;
        }
        completion->set_value(std::move(response));
    });
}
}
