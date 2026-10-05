#include "Models.h"
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>
#include <limits>
#include <set>

namespace bp {
static std::string field(const Json::Value& value, const char* key) {
    if (!value.isMember(key) || value[key].isNull()) return {};
    if (!value[key].isString()) throw Error("ResolveContract", std::string(key) + " 必须是字符串");
    return value[key].asString();
}
static bool flag(const Json::Value& value, const char* key) {
    if (!value.isMember(key)) return false;
    if (value[key].isBool()) return value[key].asBool();
    // Fixed profile: dictionary bool assignments are stored as int64 by the official add-on.
    if (value[key].isIntegral() && (value[key].asInt64() == 0 || value[key].asInt64() == 1)) return value[key].asInt64() != 0;
    throw Error("ResolveContract", std::string(key) + " 必须是布尔值");
}
static Json::Value displayInformation(const Json::Value& value) {
    Json::Value result(Json::objectValue);
    // Only public descriptive fields belong in the UI, not script credentials,
    // request headers, chat JavaScript, or large inline subtitle contents.
    for(const auto* name:{"title","author","date","duration","viewCount","likeCount","dislikeCount","webUrl","content"})
        if(value[name].isString() || value[name].isNumeric())result[name]=value[name];
    return result;
}
bool playlistItemCurrent(const Json::Value& item) {
    // The pinned upstream script uses string "1" for the active playlist item.
    // Keep this compatibility local to current; other boolean fields remain strict.
    if(item["current"].isString()) {
        const auto value=item["current"].asString();
        if(value=="1")return true;
        if(value=="0")return false;
    }
    return flag(item,"current");
}
bool supportedUrl(const std::string& input, bool local) {
    if (input.empty() || input.find('\0') != std::string::npos) return false;
    QUrl url(QString::fromUtf8(input));
    return url.isValid() && ((url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty()
                             || local && (url.isLocalFile() || url.scheme().isEmpty()));
}
PlaybackPlan adaptResult(SessionKey key, const std::string& logicalUrl, const std::string& defaultUrl,
                         const Json::Value& meta, const Json::Value& quality, const Json::Value& playlist) {
    if (!meta.isObject() || !quality.isArray() || !playlist.isArray()) throw Error("ResolveContract", "脚本结果类型无效");
    auto message = field(meta, "errorMessage");
    if (!message.empty()) throw Error("ResolveDenied", message);
    if (!supportedUrl(defaultUrl, true)) throw Error("ResolveDenied", "脚本没有返回有效播放地址");
    if (quality.size() > 1024 || playlist.size() > 5000) throw Error("ResolveContract", "脚本返回列表超过上限");
    PlaybackPlan plan;
    plan.information=displayInformation(meta);
    plan.key = key; plan.originalUrl = logicalUrl; plan.defaultUrl = defaultUrl;
    plan.webUrl = field(meta,"webUrl"); plan.opaqueVid = field(meta,"vid"); plan.fileExt = field(meta,"fileExt");
    plan.title = field(meta,"title");if(plan.title.empty())plan.title=logicalUrl;
    plan.live=QUrl(QString::fromUtf8(logicalUrl)).host().compare("live.bilibili.com",Qt::CaseInsensitive)==0
        || QUrl(QString::fromUtf8(plan.webUrl)).host().compare("live.bilibili.com",Qt::CaseInsensitive)==0
        || meta["live"].isBool() && meta["live"].asBool();
    plan.imageOnly = plan.fileExt == "jpg" || plan.fileExt == "png" || plan.fileExt == "webp";
    std::set<int> tags;
    for (const auto& value : quality) {
        Candidate c;
        c.url = field(value,"url");
        if (!supportedUrl(c.url,true)) throw Error("ResolveContract", "画质项 url 无效");
        c.quality=field(value,"quality"); c.detail=field(value,"qualityDetail"); c.resolution=field(value,"resolution");
        c.format=field(value,"format"); c.audioName=field(value,"audioName"); c.audioCode=field(value,"audioCode");
        if(value["fps"].isNumeric())c.fps=value["fps"].asDouble();
        c.audioDefault=flag(value,"audioIsDefault"); c.hdrHint=flag(value,"isHDR"); c.order=plan.candidates.size();
        const auto va=field(value,"va");
        if(va=="va")c.role=StreamRole::Muxed;
        else if(va=="v")c.role=StreamRole::Video;
        else if(va=="a")c.role=StreamRole::Audio;
        else if(!va.empty())throw Error("ResolveContract","va 必须是 va、v 或 a");
        if(value.isMember("bitrateVal")) {
            if(!value["bitrateVal"].isInt64() || value["bitrateVal"].asInt64()<0)throw Error("ResolveContract","bitrateVal 必须是非负整数");
            c.bitrateBps=value["bitrateVal"].asInt64();
        }
        if(c.bitrateBps==0) {
            const auto match=QRegularExpression("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*([kKmMgG]?)(?:bps|bit/s|b/s)?\\s*$").match(QString::fromStdString(field(value,"bitrate")));
            if(match.hasMatch()) {
                auto unit=match.captured(2).toLower();double scale=unit=="k"?1e3:unit=="m"?1e6:unit=="g"?1e9:1;
                const double bits=match.captured(1).toDouble()*scale;
                if(bits<double(std::numeric_limits<int64_t>::max()))c.bitrateBps=int64_t(bits);
            }
        }
        if (value.isMember("itag")) {
            if (!value["itag"].isInt()) throw Error("ResolveContract", "itag 超出 int32 范围");
            c.itag=value["itag"].asInt();
            if (!tags.insert(*c.itag).second) throw Error("ResolveContract", "脚本返回重复 itag");
        }
        c.id = c.itag ? "itag:"+std::to_string(*c.itag) : "index:"+std::to_string(c.order);
        plan.candidates.push_back(std::move(c));
    }
    if (meta.isMember("subtitle")) {
        if (!meta["subtitle"].isArray() || meta["subtitle"].size()>256) throw Error("ResolveContract", "subtitle 列表无效");
        for (const auto& value : meta["subtitle"]) {
            SubtitleSource s;
            s.id="script:"+std::to_string(plan.subtitles.size()); s.language=field(value,"langCode");
            s.label=field(value,"name");
            if(s.label.empty())s.label=field(value,"langTranslated");
            if(s.label.empty())s.label=field(value,"langOriginal");
            s.kind=field(value,"kind"); s.content=field(value,"fileContent"); s.url=field(value,"url");
            if (s.content.empty() && !supportedUrl(s.url)) throw Error("ResolveContract","字幕缺少内容或有效 url");
            if (s.content.size()>32*1024*1024) throw Error("ResolveContract","字幕超过长度上限");
            plan.subtitles.push_back(std::move(s));
        }
    }
    if (meta.isMember("chapter")) {
        if (!meta["chapter"].isArray()) throw Error("ResolveContract", "chapter 列表无效");
        for (const auto& c : meta["chapter"]) {
            double seconds = -1;
            if (c["time_second"].isNumeric()) seconds=c["time_second"].asDouble();
            if (seconds<0 && c["time"].isString()) {
                auto parts=QString::fromStdString(c["time"].asString()).split(':');
                seconds=0; for(const auto& part:parts){ bool ok; double n=part.toDouble(&ok); if(!ok){seconds=-1;break;} seconds=seconds*60+n; }
            }
            if (seconds>=0 && seconds<86400000) plan.chapters.push_back({MediaTime(int64_t(seconds*10000000)), "章节 "+std::to_string(plan.chapters.size()+1)});
        }
    }
    for (const auto& value:playlist) {
        PlaylistItem item{field(value,"title"),field(value,"url"),field(value,"author"),field(value,"thumbnail"),playlistItemCurrent(value)};
        item.information=displayInformation(value);
        if (!supportedUrl(item.url)) throw Error("ResolveContract","播放列表 url 无效");
        if(item.current && !item.title.empty()) plan.title=item.title;
        plan.playlist.push_back(std::move(item));
    }
    auto chatUrl=field(meta,"chatUrl"); auto chatScript=field(meta,"chatScript");
    if(!chatUrl.empty()) {
        if(!supportedUrl(chatUrl)) throw Error("ResolveContract","chatUrl 必须是 HTTP/HTTPS 地址");
        plan.chat=ChatPage{key,chatUrl,chatScript};
    }
    const std::set<std::string> known={"title","duration","vid","webUrl","author","content","date","fileExt","thumbnail","viewCount","likeCount","dislikeCount","chatUrl","chatScript","errorMessage","type3D","is360","subtitle","chapter","live"};
    for(const auto& name:meta.getMemberNames()) if(!known.contains(name)) plan.unknownFields.push_back(name);
    return plan;
}
const Candidate* preferredAudioCandidate(const std::vector<Candidate>& candidates) {
    const Candidate* best=nullptr;
    for(const auto& c:candidates) {
        const bool audio=c.role==StreamRole::Audio || c.role==StreamRole::Unknown && (!c.audioName.empty() || !c.audioCode.empty() || c.audioDefault);
        // Equal or unavailable bitrates keep the original script order.
        if(audio && (!best || c.bitrateBps>best->bitrateBps))best=&c;
    }
    return best;
}
bool domainMatches(const std::string& host, const std::string& raw, bool hostOnly) {
    auto h=QString::fromStdString(host).toLower(); auto d=QString::fromStdString(raw).toLower();
    if(d.startsWith('.')) d.remove(0,1);
    return h==d || !hostOnly && !d.isEmpty() && h.endsWith('.'+d);
}
bool cookiePathMatches(const std::string& r,const std::string& c) {
    if(c.empty() || c=="/") return true;
    return r==c || r.starts_with(c) && (c.back()=='/' || r.size()>c.size() && r[c.size()]=='/');
}
std::string redactUrl(const std::string& input) {
    QUrl u(QString::fromUtf8(input)); u.setQuery({}); u.setFragment({}); u.setUserInfo({});
    return u.toString().toStdString();
}
int64_t subtitleMillis(MediaTime media,MediaTime offset){return std::chrono::duration_cast<std::chrono::milliseconds>(media-offset).count();}
bool validTransition(State from,State to) {
    if(from==to) return true;
    if(to==State::Closing || to==State::Error) return from!=State::Idle;
    switch(from) {
    case State::Idle: return to==State::Resolving;
    case State::Resolving: return to==State::Opening;
    case State::Seeking: return to==State::Playing || to==State::Paused || to==State::Completed;
    case State::Opening: case State::Switching: return to==State::Buffering || to==State::Playing || to==State::Paused;
    case State::Buffering: return to==State::Playing || to==State::Paused || to==State::Reconnecting;
    case State::Playing: return to==State::Paused || to==State::Seeking || to==State::Switching || to==State::Reconnecting || to==State::Completed;
    case State::Paused: return to==State::Playing || to==State::Seeking || to==State::Switching || to==State::Reconnecting;
    case State::Reconnecting: return to==State::Resolving || to==State::Opening;
    case State::Closing: case State::Error: return to==State::Idle;
    case State::Completed: return to==State::Seeking;
    }
    return false;
}
}
