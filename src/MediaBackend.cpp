#include "MediaBackend.h"
#include "DvdPlayer.h"
#include "FrameStepper.h"
#include "VideoEnhancer.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QMediaMetaData>
#include <QTimer>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

MediaBackend::MediaBackend(QObject *parent) : QObject(parent), m_dvd(new DvdPlayer(this)), m_audio(new QAudioOutput(this))
{
    m_enhancer = new VideoEnhancer(this);
    m_filterSink = new QVideoSink(this);
    connect(m_filterSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_videoEnhancement && !m_isDvd && m_frameTimeUs < 0) presentFrame(frame);
    });
    connect(m_enhancer, &VideoEnhancer::ready, this, [this](const QVideoFrame &original, const QVideoFrame &filtered) {
        if (!m_videoEnhancement || m_isDvd || !m_sink) return;
        m_displayOriginal = original;
        m_sink->setVideoFrame(filtered);
        if (m_stepFilterPending) { m_stepFilterPending = false; m_frameBusy = false; emit changed(); }
    });
    m_stepper = new FrameStepper(this);
    connect(m_stepper, &FrameStepper::ready, this, [this](const QImage &image, qint64 time, const QString &error) {
        m_frameBusy = false;
        if (!error.isEmpty()) { emit changed(); emit failure(m_path, error); return; }
        m_frameTimeUs = time;
        if (m_sink) {
            QVideoFrame frame(image);
            frame.setStartTime(time);
            if (m_videoEnhancement) { m_frameBusy = true; m_stepFilterPending = true; }
            presentFrame(frame);
        }
        emit changed();
    });
    m_audio->setVolume(0.5f);
    connect(m_dvd, &DvdPlayer::changed, this, [this] { if (m_isDvd) emit changed(); });
    connect(m_dvd, &DvdPlayer::failure, this, [this](const QString &error) { if (m_isDvd) emit failure(m_path, error); });
    connect(m_dvd, &DvdPlayer::frame, this, [this](const QImage &image) {
        if (m_isDvd && m_sink) m_sink->setVideoFrame(QVideoFrame(image));
    });
}
MediaBackend::~MediaBackend()
{
    if (m_player) {
        m_player->disconnect(this);
        m_player->stop();
        delete m_player;
    }
}
void MediaBackend::setVideoSink(QVideoSink *sink)
{
    m_sink = sink;
    if (!sink) m_enhancer->cancel();
    if (m_player) m_player->setVideoSink(playbackSink());
}
void MediaBackend::open(const QString &path)
{
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) {
        emit failure(path, tr("ファイルが存在しないか、読み取れません。"));
        return;
    }
    close();
    m_path = file.absoluteFilePath();
    m_isDvd = file.suffix().compare("iso", Qt::CaseInsensitive) == 0;
    if (m_isDvd) {
        m_dvd->open(m_path, volume(), muted());
        emit changed();
        return;
    }
    m_player = new QMediaPlayer(this);
    // Keep Qt at one pass: its infinite mode can prebuffer future repetitions
    // that survive setLoops(Once). Decide whether to repeat at the actual end.
    m_player->setLoops(QMediaPlayer::Once);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoSink(playbackSink());
    auto *buffers = new QAudioBufferOutput(m_player);
    m_player->setAudioBufferOutput(buffers);
    connect(buffers, &QAudioBufferOutput::audioBufferReceived, this, [this](const QAudioBuffer &buffer) {
        if (!audioOnly() || !playing()) return;
        const auto levels = m_levels.process(buffer);
        emit audioLevels(levels.energy, levels.bass);
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::seekableChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::hasAudioChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::tracksChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia || !m_looping || !m_playRequested) return;
        auto *source = m_player;
        QTimer::singleShot(0, source, [this, source] {
            if (m_player != source || !m_looping || !m_playRequested
                || source->mediaStatus() != QMediaPlayer::EndOfMedia) return;
            m_levels.reset();
            source->setPosition(0);
            source->play();
        });
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](auto, const QString &detail) {
        qWarning().noquote() << m_path << detail;
        emit changed();
        emit failure(m_path, tr("再生できません。ファイルの破損、未対応の形式・コーデックなどが考えられます。\n%1").arg(detail));
    });
    m_player->setSource(QUrl::fromLocalFile(m_path));
    m_playRequested = true;
    m_player->play();
    emit changed();
}
void MediaBackend::close()
{
    m_stepFilterPending = false;
    m_enhancer->cancel();
    m_originalFrame = {}; m_displayOriginal = {};
    m_secondMode = false;
    m_stepper->cancel();
    m_frameTimeUs = -1;
    m_frameBusy = false;
    m_playRequested = false;
    // Retire the source before clearing the last frame and notifying the UI.
    m_isDvd = false;
    m_dvd->close();
    if (m_player) {
        m_player->disconnect(this);
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }
    m_path.clear();
    m_levels.reset();
    if (m_sink) m_sink->setVideoFrame(QVideoFrame());
    emit changed();
}
void MediaBackend::togglePlayback()
{
    if (!available()) return;
    m_secondMode = false;
    emit changed();
    if (m_isDvd) { playing() ? m_dvd->pause() : m_dvd->play(); return; }
    if (playing()) pause();
    else {
        leaveFrameMode();
        m_playRequested = true;
        if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(0);
        m_player->play();
    }
}
void MediaBackend::pause() { m_playRequested = false; if (m_isDvd) m_dvd->pause(); else if (available()) m_player->pause(); }
void MediaBackend::stop()
{
    m_enhancer->cancel();
    leaveFrameMode();
    m_playRequested = false;
    if (m_isDvd) { m_dvd->stop(); return; }
    if (m_player) { m_player->stop(); m_player->setPosition(0); }
    emit changed();
}
void MediaBackend::seek(qint64 milliseconds)
{
    m_enhancer->cancel();
    leaveFrameMode();
    m_levels.reset();
    if (m_isDvd) { m_dvd->seek(milliseconds); return; }
    if (seekable()) m_player->setPosition(std::clamp(milliseconds, qint64(0), duration()));
}
void MediaBackend::setVolume(int percent) { m_audio->setVolume(std::clamp(percent, 0, 100) / 100.0f); if (m_isDvd) m_dvd->volume(volume(), muted()); emit changed(); }
void MediaBackend::setLooping(bool enabled)
{
    m_looping = enabled;
    m_dvd->setLooping(enabled);
    emit changed();
}
void MediaBackend::setMuted(bool muted) { m_audio->setMuted(muted); if (m_isDvd) m_dvd->volume(volume(), muted); emit changed(); }
int MediaBackend::volume() const { return qRound(m_audio->volume() * 100); }
bool MediaBackend::muted() const { return m_audio->isMuted(); }
qint64 MediaBackend::position() const { if (m_frameTimeUs >= 0) return m_frameTimeUs / 1000; if (m_isDvd) return m_dvd->state().position; return m_player ? m_player->position() : 0; }
bool MediaBackend::canStepFrame() const { return !m_isDvd && hasVideo() && seekable() && !m_frameBusy; }
void MediaBackend::stepFrame(int direction)
{
    if (!canStepFrame() || !m_sink || direction == 0) return;
    const auto shown = m_sink->videoFrame();
    const qint64 anchor = m_frameTimeUs >= 0 ? m_frameTimeUs : (shown.isValid() && shown.startTime() >= 0 ? shown.startTime() : position() * 1000);
    pause();
    m_enhancer->cancel();
    if (m_videoEnhancement && m_displayOriginal.isValid()) m_originalFrame = m_displayOriginal;
    m_frameTimeUs = anchor;
    m_player->setVideoSink(nullptr);
    if (shown.isValid()) m_sink->setVideoFrame(shown);
    m_frameBusy = true;
    m_stepFilterPending = false;
    m_stepper->request(m_path, anchor, direction);
    emit changed();
}
void MediaBackend::leaveFrameMode()
{
    m_stepFilterPending = false;
    const bool wasSecondMode = m_secondMode;
    m_secondMode = false;
    m_stepper->cancel();
    m_frameBusy = false;
    if (m_frameTimeUs < 0) { if (wasSecondMode) emit changed(); return; }
    const auto time = m_frameTimeUs;
    m_frameTimeUs = -1;
    m_enhancer->cancel();
    if (m_player) { m_player->setVideoSink(playbackSink()); m_player->setPosition((time + 500) / 1000); }
    emit changed();
}
void MediaBackend::stepSecond(int direction)
{
    if (!seekable() || direction == 0) return;
    seek(position() + (direction > 0 ? 1000 : -1000));
    m_secondMode = true;
    emit changed();
}
QVideoSink *MediaBackend::playbackSink() const
{
    return m_sink && m_videoEnhancement && !m_isDvd ? m_filterSink : m_sink;
}
void MediaBackend::presentFrame(const QVideoFrame &frame)
{
    m_originalFrame = frame;
    if (!m_sink) return;
    if (m_videoEnhancement && !m_isDvd && frame.isValid()) m_enhancer->submit(frame);
    else { m_enhancer->cancel(); m_displayOriginal = frame; m_sink->setVideoFrame(frame); }
}
void MediaBackend::setVideoEnhancement(bool enabled)
{
    if (m_videoEnhancement == enabled) return;
    const auto original = m_videoEnhancement ? m_originalFrame : (m_sink ? m_sink->videoFrame() : QVideoFrame());
    m_enhancer->cancel();
    m_videoEnhancement = enabled;
    if (!enabled && m_stepFilterPending) { m_stepFilterPending = false; m_frameBusy = false; }
    if (m_player && m_frameTimeUs < 0) m_player->setVideoSink(playbackSink());
    if (!m_isDvd && original.isValid()) presentFrame(original);
    emit changed();
}
qint64 MediaBackend::duration() const { if (m_isDvd) return m_dvd->state().duration; return m_player ? m_player->duration() : 0; }
bool MediaBackend::seekable() const { if (m_isDvd) return m_dvd->state().seekable; return available() && m_player->isSeekable() && duration() > 0; }
bool MediaBackend::playing() const { if (m_isDvd) return m_dvd->state().playing; return m_player && m_player->playbackState() == QMediaPlayer::PlayingState; }
bool MediaBackend::hasVideo() const { if (m_isDvd) return m_dvd->state().video; return m_player && m_player->hasVideo(); }
bool MediaBackend::audioOnly() const { return !m_isDvd && available() && m_player->hasAudio() && !m_player->hasVideo() && m_player->videoTracks().isEmpty() && m_player->mediaStatus() != QMediaPlayer::LoadingMedia; }
bool MediaBackend::available() const { if (m_isDvd) return m_dvd->state().available; return m_player && m_player->error() == QMediaPlayer::NoError && m_player->mediaStatus() != QMediaPlayer::InvalidMedia; }
QString MediaBackend::statusText() const
{
    if (m_isDvd) return playing() ? tr("DVD再生中") : tr("DVD");
    if (!m_player) return tr("WMV・MP4・AVIファイルを開いてください");
    if (!available()) return tr("再生エラー");
    switch (m_player->mediaStatus()) {
    case QMediaPlayer::LoadingMedia: return tr("読み込み中…");
    case QMediaPlayer::StalledMedia: return tr("バッファ待機中…");
    case QMediaPlayer::EndOfMedia: return tr("再生終了");
    default: break;
    }
    if (playing()) return tr("再生中");
    return m_player->playbackState() == QMediaPlayer::PausedState ? tr("一時停止") : tr("停止");
}

QStringList MediaBackend::dvdTitles() const { return m_isDvd ? m_dvd->state().titles : QStringList(); }
int MediaBackend::dvdTitle() const { return m_isDvd ? m_dvd->state().title : -1; }
void MediaBackend::selectDvdTitle(int index) { if (m_isDvd) { m_secondMode = false; m_dvd->selectTitle(index); emit changed(); } }
