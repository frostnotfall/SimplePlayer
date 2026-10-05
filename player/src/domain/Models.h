#pragma once
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <json/json.h>

namespace bp {
using MediaTime = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
enum class State { Idle, Resolving, Opening, Buffering, Playing, Paused, Seeking, Switching, Reconnecting, Completed, Closing, Error };
enum class StreamRole { Unknown, Video, Audio, Muxed };
struct Error : std::runtime_error {
    std::string code;
    Error(std::string code, const std::string& message) : std::runtime_error(message), code(std::move(code)) {}
};
struct Cancellation {
    std::atomic_bool cancelled{false};
    std::mutex mutex;
    std::condition_variable wake;
    void cancel() { cancelled = true; wake.notify_all(); }
    void check() const { if (cancelled) throw Error("Cancelled", "任务已取消"); }
    void sleep(int milliseconds) {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(std::max(0, milliseconds)), [&]{ return cancelled.load(); });
        check();
    }
};
using Cancel = std::shared_ptr<Cancellation>;
struct SessionKey {
    uint64_t session = 0, generation = 0;
    bool operator==(const SessionKey&) const = default;
};
struct Candidate {
    std::string id, url, quality, detail, resolution, format, audioName, audioCode;
    int64_t bitrateBps = 0;
    double fps = 0;
    size_t order = 0;
    std::optional<int> itag;
    bool audioDefault = false, hdrHint = false;
    StreamRole role = StreamRole::Unknown;
};
struct SubtitleSource { std::string id, label, language, kind, content, url; };
struct Chapter { MediaTime time; std::string label; };
struct PlaylistItem { std::string title, url, author, thumbnail; bool current = false; Json::Value information{Json::objectValue}; };
struct ChatPage { SessionKey key; std::string url, script; bool operator==(const ChatPage&) const = default; };
struct PlaybackPlan {
    SessionKey key;
    std::string originalUrl, webUrl, opaqueVid, defaultUrl, title, fileExt;
    std::vector<Candidate> candidates;
    std::vector<SubtitleSource> subtitles;
    std::vector<Chapter> chapters;
    std::vector<PlaylistItem> playlist;
    std::optional<ChatPage> chat;
    std::vector<std::string> unknownFields;
    Json::Value information{Json::objectValue};
    bool live = false, imageOnly = false;
};
PlaybackPlan adaptResult(SessionKey key, const std::string& logicalUrl, const std::string& defaultUrl,
                         const Json::Value& metadata, const Json::Value& quality,
                         const Json::Value& playlist = Json::Value(Json::arrayValue));
bool playlistItemCurrent(const Json::Value& item);
const Candidate* preferredAudioCandidate(const std::vector<Candidate>& candidates);
bool supportedUrl(const std::string& url, bool local = false);
bool domainMatches(const std::string& host, const std::string& domain, bool hostOnly = false);
bool cookiePathMatches(const std::string& requestPath, const std::string& cookiePath);
std::string redactUrl(const std::string& input);
int64_t subtitleMillis(MediaTime media, MediaTime offset);
bool validTransition(State from, State to);
struct Cookie {
    enum class SameSite {Unspecified,None,Lax,Strict};
    std::string name, value, domain, path = "/";
    bool secure = false, httpOnly = false, hostOnly = true;
    int64_t expires = 0;
    SameSite sameSite=SameSite::Unspecified;
};
struct CookieSnapshot { uint64_t epoch = 1, revision = 0; std::vector<Cookie> cookies; };
}
