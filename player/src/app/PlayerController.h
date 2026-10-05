#pragma once
#include "domain/Models.h"
#include "storage/Settings.h"
#include "scripts/ScriptRuntime.h"
#include "media/directshow/GraphSession.h"
#include "media/live/LiveTimeshift.h"
#include "accounts/webview2/BrowserSession.h"
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QVariantList>
#include <QWindow>
#include <QTimer>
#include <QMap>
#include <QPointF>
#include <array>

namespace bp {
class PlayerController : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY progressChanged)
    Q_PROPERTY(double duration READ duration NOTIFY progressChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY changed)
    Q_PROPERTY(double playbackRate READ playbackRate NOTIFY changed)
    Q_PROPERTY(bool canChangePlaybackRate READ canChangePlaybackRate NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool isLive READ isLive NOTIFY changed)
    Q_PROPERTY(double seekStart READ seekStart NOTIFY progressChanged)
    Q_PROPERTY(double seekEnd READ seekEnd NOTIFY progressChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY changed)
    Q_PROPERTY(bool chatAvailable READ chatAvailable NOTIFY changed)
    Q_PROPERTY(QWindow* videoWindow READ videoWindow CONSTANT)
    Q_PROPERTY(bp::BrowserSession* browser READ browser CONSTANT)
    Q_PROPERTY(QVariantList qualities READ qualities NOTIFY changed)
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(QVariantMap mediaInformation READ mediaInformation NOTIFY mediaInformationChanged)
    Q_PROPERTY(QVariantList subtitles READ subtitles NOTIFY changed)
    Q_PROPERTY(QString selectedQuality READ selectedQuality NOTIFY changed)
    Q_PROPERTY(QString selectedAudio READ selectedAudio NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QVariantMap theme READ theme NOTIFY themeChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountStatusChanged)
    Q_PROPERTY(bool scriptEnabled READ scriptEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString debugLog READ debugLog NOTIFY debugLogChanged)
    Q_PROPERTY(QString selectedMainSubtitle READ selectedMainSubtitle NOTIFY subtitleSelectionChanged)
    Q_PROPERTY(QString selectedSecondarySubtitle READ selectedSecondarySubtitle NOTIFY subtitleSelectionChanged)
    Q_PROPERTY(bool mainSubtitleVisible READ mainSubtitleVisible NOTIFY settingsChanged)
    Q_PROPERTY(bool secondarySubtitleVisible READ secondarySubtitleVisible NOTIFY settingsChanged)
public:
    explicit PlayerController(QString dataDirectory,bool validationMode=false);
    ~PlayerController();
    bool nativeEventFilter(const QByteArray& type,void* message,qintptr* result) override;
    QString state() const;
    QString title() const{return title_;}
    QString error() const{return error_;}
    QString detail() const{return detail_;}
    QVariantMap mediaInformation() const;
    double position() const{return position_;}
    double duration() const{return duration_;}
    bool seeking() const{return state_==State::Seeking;}
    double playbackRate() const{return playbackRate_;}
    bool canChangePlaybackRate() const{return activeGraph_ && !pendingGraph_ && !rateChanging_ && (state_==State::Playing || state_==State::Paused) && diagnostics_.value("playbackRateSupported").toBool();}
    bool canSeek() const{return canSeek_;}
    bool isLive() const{return plan_ && plan_->live;}
    double seekStart() const{return liveCache_?liveCache_->startTime():0;}
    double seekEnd() const{return liveCache_?liveCache_->endTime():duration_;}
    bool hasMedia() const{return activeGraph_!=nullptr;}
    bool chatAvailable() const{return scriptEnabled() && plan_ && plan_->chat.has_value();}
    QWindow* videoWindow() const{return videoHost_;}
    BrowserSession* browser() const{return browser_.get();}
    QVariantList qualities() const{return qualities_;}
    QVariantList playlist() const{return playlist_;}
    QVariantList subtitles() const{return subtitles_;}
    QString selectedQuality() const{return selectedQuality_;}
    QString selectedAudio() const{return selectedAudio_;}
    QVariantMap settings() const{return settings_->values.toVariantMap();}
    QVariantMap diagnostics() const{return diagnostics_;}
    QVariantMap theme() const;
    bool loggedIn() const{return loggedIn_;}
    bool scriptEnabled() const{return settings_->values["scriptEnabled"].toBool(true);}
    QString debugLog() const{return debugLines_.join('\n');}
    QString selectedMainSubtitle() const{return slots_[0].id;}
    QString selectedSecondarySubtitle() const{return slots_[1].id;}
    bool mainSubtitleVisible() const{return settings_->values["mainSubtitleVisible"].toBool(true);}
    bool secondarySubtitleVisible() const{return settings_->values["secondarySubtitleVisible"].toBool(true);}
    Q_INVOKABLE void clearDebugLog(){debugLines_.clear();emit debugLogChanged();}
    Q_INVOKABLE void openFiles(QVariantList paths);
    Q_INVOKABLE void removePlaylistItem(int index);
    Q_INVOKABLE void clearPlaylist();
    Q_INVOKABLE void selectSubtitle(int slot,QString id);
    Q_INVOKABLE void open(QString url);
    Q_INVOKABLE void openPlaylistItem(int index);
    Q_INVOKABLE qint64 openInNewInstance(QString url);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void returnToLive();
    Q_INVOKABLE void setPlaybackRate(double rate);
    Q_INVOKABLE void adjustPlaybackRate(double step);
    Q_INVOKABLE void focusInputWindow(QWindow* window);
    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE QString nativePath(QString path) const;
    Q_INVOKABLE void openPath(QString path);
    Q_INVOKABLE void selectQuality(QString id);
    Q_INVOKABLE void selectAudio(QString id);
    Q_INVOKABLE void setSetting(QString key,QVariant value);
    Q_INVOKABLE void saveWindowSize(int width,int height);
    Q_INVOKABLE void setSubtitle(int slot,QString id,double offsetSeconds=0,bool danmaku=false);
    Q_INVOKABLE void loadSubtitle(int slot,QUrl path);
    Q_INVOKABLE void adjustSubtitle(int slot,double offsetSeconds,bool danmaku);
    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openScriptConfig();
    Q_INVOKABLE QVariantMap exportDiagnostics();
    Q_INVOKABLE void answerMessage(int id,int response);
signals:
    void playlistAboutToChange(bool locateCurrent);
    void playlistChanged();
    void instanceOpened(qint64 processId,QString url);
    void mediaInformationChanged();
    void debugLogChanged();
    void subtitleSelectionChanged();
    void accountStatusChanged();
    void changed();
    void progressChanged();
    void settingsChanged();
    void themeChanged();
    void askMessage(int id,QString title,QString message,int buttons);
    void scriptMessagesCancelled();
    void videoFullscreenRequested();
    void videoEscapeRequested();
    void videoLayoutRequested();
    void videoLinkRequested();
    void sidebarPageRequested(int page);
    void videoMenuRequested(QPointF position);
    void videoPointerMoved(QPointF position);
    void videoResizeRequested(int edges);
    void windowResizeStarted(int edges);
    void windowResizeFinished();
protected:
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    std::unique_ptr<Settings> settings_;
    bool validationMode_=false;
    HttpService http_;
    std::unique_ptr<BrowserSession> browser_;
    QWindow* videoHost_;
    GraphThread graphThread_;
    QObject* graphDispatcher_=nullptr;
    QThread scriptThread_;
    QObject* scriptWorker_;
    std::unique_ptr<ScriptRuntime> runtime_;
    GraphSession* activeGraph_=nullptr;
    GraphSession* pendingGraph_=nullptr;
    State state_=State::Idle;
    SessionKey key_{};
    Cancel mediaCancel_;
    std::shared_ptr<HostSession> hostSession_;
    std::optional<PlaybackPlan> plan_;
    std::unique_ptr<LiveTimeshift> liveCache_;
    std::string liveVideo_,liveAudio_;
    double liveTarget_=-1;
    QString title_="SimplePlayer",error_,detail_,accountUrl_,configPath_,selectedQuality_,selectedAudio_;
    QVariantList qualities_,playlist_,subtitles_,localPlaylist_;
    QStringList debugLines_;
    QMap<QString,QByteArray> externalSubtitles_;
    bool scriptPlan_=false,nativeResizeActive_=false;
    bool locatePlaylist_=true;
    void notifyPlaylistChanged(bool locateCurrent=false);
    void appendDebug(QString message);
    void saveLocalPlaylist();
    void addLocalFile(QString path);
    QVariantMap diagnostics_;
    double position_=0,duration_=0;
    double playbackRate_=1.0;
    bool rateChanging_=false;
    std::optional<double> seekOrigin_;
    bool canSeek_=false,wasPaused_=false,seekFromEnd_=false;
    bool loggedIn_=false,accountCheckPending_=false;
    qint64 lastAccountCheck_=0;
    Cancel accountCancel_;
    int recoveryAttempts_=0,lastStatsSecond_=-1;
    uint64_t graphOpenSerial_=0;
    uint64_t resolveSerial_=0;
    std::atomic_bool timeQueued_{false};
    std::atomic_int nextMessage_{1};
    QMap<int,std::shared_ptr<std::promise<int>>> messages_;
    struct Slot {QString id;QByteArray content;int64_t offset=0;bool danmaku=false;};
    std::array<Slot,2> slots_;
    FilterPaths filterPaths() const;
    void transition(State state);
    void fail(SessionKey key,QString code,QString message);
    void resolve(SessionKey key,QString url,bool includePlaylist);
    void openMedia(QString url,bool includePlaylist);
    void installPlan(PlaybackPlan plan);
    void updateChoices();
    void buildGraph(QString quality,QString audio,bool switching=false,bool restoring=false,QString switchError={});
    void releaseGraph(GraphSession*& graph);
    void queueStatistics(const char* event);
    void applySlots(int onlySlot=-1);
    HostServices hostServices();
    void applyVolume();
    void refreshAccount();
};
}
