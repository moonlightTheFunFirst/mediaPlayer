#include "MediaBackend.h"
#include "DvdPlayer.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioOutput>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

MediaBackend::MediaBackend(QObject *parent) : QObject(parent), m_dvd(new DvdPlayer(this)), m_audio(new QAudioOutput(this))
{
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
    if (m_player) m_player->setVideoSink(sink);
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
    m_player->setAudioOutput(m_audio);
    m_player->setVideoSink(m_sink);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::seekableChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &MediaBackend::changed);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](auto, const QString &detail) {
        qWarning().noquote() << m_path << detail;
        emit changed();
        emit failure(m_path, tr("再生できません。ファイルの破損、未対応の形式・コーデックなどが考えられます。\n%1").arg(detail));
    });
    m_player->setSource(QUrl::fromLocalFile(m_path));
    m_player->play();
    emit changed();
}
void MediaBackend::close()
{
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
    if (m_sink) m_sink->setVideoFrame(QVideoFrame());
    emit changed();
}
void MediaBackend::togglePlayback()
{
    if (!available()) return;
    if (m_isDvd) { playing() ? m_dvd->pause() : m_dvd->play(); return; }
    if (playing()) m_player->pause();
    else {
        if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(0);
        m_player->play();
    }
}
void MediaBackend::pause() { if (m_isDvd) m_dvd->pause(); else if (available()) m_player->pause(); }
void MediaBackend::stop()
{
    if (m_isDvd) { m_dvd->stop(); return; }
    if (m_player) { m_player->stop(); m_player->setPosition(0); }
    emit changed();
}
void MediaBackend::seek(qint64 milliseconds)
{
    if (m_isDvd) { m_dvd->seek(milliseconds); return; }
    if (seekable()) m_player->setPosition(std::clamp(milliseconds, qint64(0), duration()));
}
void MediaBackend::setVolume(int percent) { m_audio->setVolume(std::clamp(percent, 0, 100) / 100.0f); if (m_isDvd) m_dvd->volume(volume(), muted()); emit changed(); }
void MediaBackend::setMuted(bool muted) { m_audio->setMuted(muted); if (m_isDvd) m_dvd->volume(volume(), muted); emit changed(); }
int MediaBackend::volume() const { return qRound(m_audio->volume() * 100); }
bool MediaBackend::muted() const { return m_audio->isMuted(); }
qint64 MediaBackend::position() const { if (m_isDvd) return m_dvd->state().position; return m_player ? m_player->position() : 0; }
qint64 MediaBackend::duration() const { if (m_isDvd) return m_dvd->state().duration; return m_player ? m_player->duration() : 0; }
bool MediaBackend::seekable() const { if (m_isDvd) return m_dvd->state().seekable; return available() && m_player->isSeekable() && duration() > 0; }
bool MediaBackend::playing() const { if (m_isDvd) return m_dvd->state().playing; return m_player && m_player->playbackState() == QMediaPlayer::PlayingState; }
bool MediaBackend::hasVideo() const { if (m_isDvd) return m_dvd->state().video; return m_player && m_player->hasVideo(); }
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
void MediaBackend::selectDvdTitle(int index) { if (m_isDvd) m_dvd->selectTitle(index); }
