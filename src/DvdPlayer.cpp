#include "DvdPlayer.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QUrl>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
// The stable libVLC 3 C ABI; loaded dynamically to keep ordinary playback independent.
struct Instance;
struct Player;
struct Media;
struct VideoTrack { unsigned height, width, sarNum, sarDen; };
struct Track {
    uint32_t codec, originalFourcc;
    int id, type, profile, level;
    VideoTrack *video;
    unsigned bitrate;
    char *language, *description;
};
struct Stats {
    int readBytes; float inputBitrate;
    int demuxReadBytes; float demuxBitrate;
    int demuxCorrupted, demuxDiscontinuity, decodedVideo, decodedAudio;
    int displayedPictures, lostPictures, playedAudioBuffers, lostAudioBuffers;
    int sentPackets, sentBytes; float sendBitrate;
};
struct Title { int64_t duration; char *name; unsigned flags; };
using Lock = void *(*)(void *, void **);
using Unlock = void (*)(void *, void *, void *const *);
using Display = void (*)(void *, void *);
using Format = unsigned (*)(void **, char *, unsigned *, unsigned *, unsigned *, unsigned *);
using Cleanup = void (*)(void *);
#define DVD_API(X) \
 X(Instance *, libvlc_new, (int, const char *const *)) \
 X(void, libvlc_release, (Instance *)) \
 X(const char *, libvlc_get_version, ()) \
 X(Media *, libvlc_media_new_location, (Instance *, const char *)) \
 X(void, libvlc_media_add_option, (Media *, const char *)) \
 X(void, libvlc_media_release, (Media *)) \
 X(unsigned, libvlc_media_tracks_get, (Media *, Track ***)) \
 X(void, libvlc_media_tracks_release, (Track **, unsigned)) \
 X(int, libvlc_media_get_stats, (Media *, Stats *)) \
 X(Player *, libvlc_media_player_new_from_media, (Media *)) \
 X(void, libvlc_media_player_release, (Player *)) \
 X(int, libvlc_media_player_play, (Player *)) \
 X(void, libvlc_media_player_set_pause, (Player *, int)) \
 X(void, libvlc_media_player_stop, (Player *)) \
 X(int, libvlc_media_player_get_state, (Player *)) \
 X(int64_t, libvlc_media_player_get_time, (Player *)) \
 X(int64_t, libvlc_media_player_get_length, (Player *)) \
 X(void, libvlc_media_player_set_time, (Player *, int64_t)) \
 X(int, libvlc_media_player_is_seekable, (Player *)) \
 X(int, libvlc_audio_set_volume, (Player *, int)) \
 X(void, libvlc_audio_set_mute, (Player *, int)) \
 X(int, libvlc_media_player_get_title, (Player *)) \
 X(void, libvlc_media_player_set_title, (Player *, int)) \
 X(int, libvlc_media_player_get_full_title_descriptions, (Player *, Title ***)) \
 X(void, libvlc_title_descriptions_release, (Title **, unsigned)) \
 X(void, libvlc_video_set_callbacks, (Player *, Lock, Unlock, Display, void *)) \
 X(void, libvlc_video_set_format_callbacks, (Player *, Format, Cleanup))
struct Api {
    QLibrary core, library;
    bool ready = false;
#define DECLARE(ret, name, args) ret (*name) args = nullptr;
    DVD_API(DECLARE)
#undef DECLARE
    QString load() {
        if (ready) return {};
        QString root = qEnvironmentVariable("ORANGE_VLC_DIR");
        if (root.isEmpty()) root = QCoreApplication::applicationDirPath() + "/vlc";
        core.setFileName(root + "/libvlccore.dll");
        library.setFileName(root + "/libvlc.dll");
        // VLC plugins may retain process-wide TLS callbacks after an instance ends.
        core.setLoadHints(QLibrary::PreventUnloadHint);
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!core.load() || !library.load())
            return QObject::tr("DVD再生ライブラリを読み込めません。配布フォルダのvlcを確認してください。\n%1").arg(library.errorString());
#define RESOLVE(ret, name, args) name = reinterpret_cast<decltype(name)>(library.resolve(#name)); if (!name) return QStringLiteral("Missing libVLC API: " #name);
        DVD_API(RESOLVE)
#undef RESOLVE
        if (!QByteArray(libvlc_get_version()).startsWith("3.")) return QObject::tr("DVD再生にはlibVLC 3.xが必要です。");
        ready = true;
        return {};
    }
};
}

class DvdWorker : public QObject {
    Q_OBJECT
public:
    Api api;
    Instance *instance = nullptr;
    Player *player = nullptr;
    Media *media = nullptr;
    QTimer *timer = nullptr;
    QElapsedTimer opening;
    quint64 serial = 0;
    DvdState state;
    bool failed = false, titlesRead = false, stopped = false;
    int desiredVolume = 50;
    int pendingTitle = -1;
    bool desiredMute = false;
    bool looping = false, paused = false, endedHandled = false, keepTitle = false;
    int loopTitle = -1;
    QMutex mutex;
    struct VideoBuffer {
        DvdWorker *owner;
        QMutex mutex;
        std::vector<unsigned char> storage;
        unsigned char *pixels;
        unsigned width, height, pitch;
    };
    QImage latest;
    bool dirty = false;
    ~DvdWorker() override { reset(); if (instance) api.libvlc_release(instance); }
    void reset() {
        if (timer) timer->stop();
        if (player) { api.libvlc_media_player_stop(player); api.libvlc_media_player_release(player); player = nullptr; }
        if (media) { api.libvlc_media_release(media); media = nullptr; }
        QMutexLocker guard(&mutex);
        latest = {}; dirty = false;
        state = {}; titlesRead = false; failed = false; stopped = false; pendingTitle = -1;
        paused = false; endedHandled = false; loopTitle = -1;
        keepTitle = looping;
    }
    static unsigned format(void **opaque, char *chroma, unsigned *w, unsigned *h, unsigned *pitches, unsigned *lines) {
        if (!*w || !*h || *w > 4096 || *h > 4096) return 0;
        auto *buffer = new VideoBuffer;
        buffer->owner = static_cast<DvdWorker *>(*opaque);
        buffer->width = *w; buffer->height = *h;
        buffer->pitch = (*w * 4 + 31) & ~31u;
        buffer->storage.resize(size_t(buffer->pitch) * *h + 31);
        buffer->pixels = reinterpret_cast<unsigned char *>((reinterpret_cast<uintptr_t>(buffer->storage.data()) + 31) & ~uintptr_t(31));
        std::memcpy(chroma, "RV32", 4);
        *pitches = buffer->pitch; *lines = *h; *opaque = buffer;
        return 1;
    }
    static void cleanup(void *opaque) { delete static_cast<VideoBuffer *>(opaque); }
    static void *lock(void *opaque, void **planes) {
        auto *buffer = static_cast<VideoBuffer *>(opaque);
        buffer->mutex.lock(); *planes = buffer->pixels; return nullptr;
    }
    static void unlock(void *opaque, void *, void *const *) {
        static_cast<VideoBuffer *>(opaque)->mutex.unlock();
    }
    static void display(void *opaque, void *) {
        auto *buffer = static_cast<VideoBuffer *>(opaque);
        QMutexLocker guard(&buffer->mutex);
        const QImage frame = QImage(buffer->pixels, int(buffer->width), int(buffer->height), int(buffer->pitch), QImage::Format_RGB32).copy();
        QMutexLocker ownerGuard(&buffer->owner->mutex);
        buffer->owner->latest = frame;
        buffer->owner->dirty = true;
    }
    void open(const QString &path, quint64 id, int volume, bool muted) {
        reset(); serial = id; desiredVolume = volume; desiredMute = muted;
        opening.start();
        if (QFileInfo(path).size() < 32768) { emit failure(serial, tr("DVD ISOとして小さすぎるファイルです。")); return; }
        const QString error = api.load();
        if (!error.isEmpty()) { emit failure(serial, error); return; }
        if (!instance) {
            const char *options[] = {"--no-video-title-show", "--no-osd", "--no-spu", "--stats", "--no-media-library", "--no-snapshot-preview"};
            instance = api.libvlc_new(6, options);
        }
        if (!instance) { emit failure(serial, tr("DVD再生エンジンの初期化に失敗しました。")); return; }
        QUrl url = QUrl::fromLocalFile(path);
        url.setScheme("dvdnav");
        media = api.libvlc_media_new_location(instance, url.toEncoded().constData());
        if (!media) { emit failure(serial, tr("DVD ISOを開けません。")); return; }
        api.libvlc_media_add_option(media, ":dvdnav-menu=0");
        player = api.libvlc_media_player_new_from_media(media);
        if (!player) { emit failure(serial, tr("DVDプレイヤーを作成できません。")); return; }
        api.libvlc_video_set_callbacks(player, lock, unlock, display, this);
        api.libvlc_video_set_format_callbacks(player, format, cleanup);
        setVolume(volume, muted);
        if (api.libvlc_media_player_play(player) != 0) { emit failure(serial, tr("DVD ISOの再生を開始できません。")); return; }
        if (!timer) { timer = new QTimer(this); timer->setInterval(16); connect(timer, &QTimer::timeout, this, &DvdWorker::poll); }
        timer->start();
    }
    void setVolume(int value, bool muted) {
        desiredVolume = value; desiredMute = muted;
        if (player) { api.libvlc_audio_set_volume(player, value); api.libvlc_audio_set_mute(player, muted); }
    }
    void poll() {
        if (!player) return;
        const int status = api.libvlc_media_player_get_state(player);
        if (status == 7 || (!state.video && opening.elapsed() > 20000 && !stopped)) {
            if (!failed) { failed = true; state.available = false; state.playing = false; state.seekable = false;
                emit updated(serial, state);
                emit failure(serial, tr("DVD-Video ISOを再生できません。DVD形式・破損・暗号化の有無を確認してください。")); }
            timer->stop();
            api.libvlc_media_player_stop(player);
            return;
        }
        // A DVD can either end or navigate to another title/menu at its end.
        // Keep the explicitly selected title, without treating pause/stop as an end.
        const int currentTitle = api.libvlc_media_player_get_title(player);
        if (keepTitle && !looping && !paused && !stopped && pendingTitle < 0
            && loopTitle >= 0 && status == 3 && currentTitle != loopTitle) {
            // After loop is disabled, finish this title rather than following
            // the disc's navigation into another title/menu.
            state.title = state.titleIds.indexOf(loopTitle);
            stop();
            return;
        }
        if (looping && !paused && !stopped && pendingTitle < 0 && loopTitle >= 0
            && ((status == 6 && !endedHandled) || (status == 3 && currentTitle != loopTitle))) {
            if (status == 6) {
                api.libvlc_media_player_stop(player);
                pendingTitle = loopTitle;
                api.libvlc_media_player_play(player);
                setVolume(desiredVolume, desiredMute);
                endedHandled = true;
            } else {
                api.libvlc_media_player_set_title(player, loopTitle);
                api.libvlc_media_player_set_time(player, 0);
            }
            return;
        }
        if (status == 6) endedHandled = true;
        else if (status == 3) endedHandled = false;
        if (pendingTitle >= 0 && (status == 3 || status == 4)) {
            if (pendingTitle != api.libvlc_media_player_get_title(player)) api.libvlc_media_player_set_title(player, pendingTitle);
            api.libvlc_media_player_set_time(player, 0);
            pendingTitle = -1;
        }
        if (!titlesRead && (status == 3 || status == 4)) {
            Title **titles = nullptr;
            const int count = api.libvlc_media_player_get_full_title_descriptions(player, &titles);
            if (count > 0) {
                int longest = -1; qint64 longestTime = -1;
                for (int i = 0; i < count; ++i) {
                    if (titles[i]->flags & 3 || titles[i]->duration <= 0) continue;
                    state.titleIds.append(i);
                    const qint64 seconds = titles[i]->duration / 1000;
                    state.titles.append(tr("タイトル %1 (%2:%3)").arg(state.titleIds.size()).arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')));
                    if (titles[i]->duration > longestTime) { longestTime = titles[i]->duration; longest = i; }
                }
                api.libvlc_title_descriptions_release(titles, unsigned(count));
                titlesRead = true;
                loopTitle = longest;
                if (longest >= 0 && longest != api.libvlc_media_player_get_title(player)) api.libvlc_media_player_set_title(player, longest);
                setVolume(desiredVolume, desiredMute);
            }
        }
        Stats stats{};
        if (api.libvlc_media_get_stats(media, &stats)) {
            state.audioDecoded = stats.decodedAudio > 0;
            state.audioOutput = stats.playedAudioBuffers > 0;
        }
        state.available = !failed;
        state.playing = status == 3 && !stopped;
        state.position = stopped ? 0 : std::max(int64_t(0), api.libvlc_media_player_get_time(player));
        const qint64 length = api.libvlc_media_player_get_length(player);
        if (length > 0) state.duration = length;
        state.seekable = !stopped && api.libvlc_media_player_is_seekable(player) && state.duration > 0;
        if (!stopped && status != 6) state.title = state.titleIds.indexOf(api.libvlc_media_player_get_title(player));
        QImage frame;
        { QMutexLocker guard(&mutex); if (dirty) { frame = latest; dirty = false; state.video = true; } }
        emit updated(serial, state);
        if (!frame.isNull()) {
            // DVD pixels are commonly non-square (4:3 or anamorphic 16:9).
            Track **tracks = nullptr;
            const unsigned count = api.libvlc_media_tracks_get(media, &tracks);
            for (unsigned i = 0; i < count; ++i) {
                const auto *track = tracks[i];
                if (track->type != 1 || !track->video) continue;
                const auto *video = track->video;
                if (!video->sarNum || !video->sarDen || video->width != unsigned(frame.width()) || video->height != unsigned(frame.height())) continue;
                {
                    const qint64 displayWidth = qint64(frame.width()) * video->sarNum / video->sarDen;
                    if (displayWidth > 0 && displayWidth <= 4096 && displayWidth != frame.width())
                        frame = frame.scaled(int(displayWidth), frame.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }
                break;
            }
            api.libvlc_media_tracks_release(tracks, count);
            emit image(serial, frame);
        }
    }
    void play() { if (player) { if ((stopped || endedHandled) && state.title >= 0) pendingTitle = state.titleIds.value(state.title, -1); stopped = false; paused = false; api.libvlc_media_player_play(player); setVolume(desiredVolume, desiredMute); } }
    void pause() { paused = true; if (player) api.libvlc_media_player_set_pause(player, 1); }
    void stop() { if (player) { api.libvlc_media_player_stop(player); stopped = true; poll(); } }
    void seek(qint64 time) { if (player && state.seekable) api.libvlc_media_player_set_time(player, std::clamp(time, qint64(0), state.duration)); }
    void title(int index) { if (player && index >= 0 && index < state.titleIds.size()) { loopTitle = state.titleIds[index]; if (stopped || endedHandled) pendingTitle = loopTitle; else api.libvlc_media_player_set_title(player, loopTitle); stopped = false; paused = false; api.libvlc_media_player_play(player); } }
signals:
    void updated(quint64 serial, DvdState state);
    void image(quint64 serial, QImage image);
    void failure(quint64 serial, QString error);
};

DvdPlayer::DvdPlayer(QObject *parent) : QObject(parent), m_worker(new DvdWorker) {
    qRegisterMetaType<DvdState>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &DvdWorker::updated, this, [this](quint64 id, const DvdState &state) { if (id == m_serial) { m_state = state; emit changed(); } });
    connect(m_worker, &DvdWorker::image, this, [this](quint64 id, const QImage &image) { if (id == m_serial) emit frame(image); });
    connect(m_worker, &DvdWorker::failure, this, [this](quint64 id, const QString &error) { if (id == m_serial) { m_state.available = false; emit changed(); emit failure(error); } });
    m_thread.start();
}
DvdPlayer::~DvdPlayer() { m_thread.quit(); m_thread.wait(); }
void DvdPlayer::open(const QString &path, int value, bool muted) {
    const auto id = ++m_serial; m_state = {};
    QMetaObject::invokeMethod(m_worker, [this, path, id, value, muted] { m_worker->open(path, id, value, muted); });
}
void DvdPlayer::close() { ++m_serial; m_state = {}; QMetaObject::invokeMethod(m_worker, [this] { m_worker->reset(); }); }
#define COMMAND(name, expression) void DvdPlayer::name() { QMetaObject::invokeMethod(m_worker, [this] { m_worker->expression; }); }
COMMAND(play, play())
COMMAND(pause, pause())
COMMAND(stop, stop())
#undef COMMAND
void DvdPlayer::seek(qint64 time) { QMetaObject::invokeMethod(m_worker, [this, time] { m_worker->seek(time); }); }
void DvdPlayer::volume(int value, bool muted) { QMetaObject::invokeMethod(m_worker, [this, value, muted] { m_worker->setVolume(value, muted); }); }
void DvdPlayer::selectTitle(int index) { QMetaObject::invokeMethod(m_worker, [this, index] { m_worker->title(index); }); }
void DvdPlayer::setLooping(bool enabled) {
    QMetaObject::invokeMethod(m_worker, [this, enabled] {
        if (enabled && !m_worker->looping && m_worker->state.title >= 0)
            m_worker->loopTitle = m_worker->pendingTitle >= 0 ? m_worker->pendingTitle : m_worker->state.titleIds.value(m_worker->state.title, -1);
        m_worker->looping = enabled;
        if (enabled) m_worker->keepTitle = true;
    });
}
#include "DvdPlayer.moc"
