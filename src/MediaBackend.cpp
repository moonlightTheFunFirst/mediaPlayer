#include "MediaBackend.h"
#include <QAudioOutput>
#include <QFileInfo>
#include <QVideoSink>
#include <QDebug>
#include <algorithm>

MediaBackend::MediaBackend(QObject *parent) : QObject(parent), m_audio(new QAudioOutput(this))
{
    m_audio->setVolume(0.5f);
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
    // Retire the old source and its signal connections before loading another.
    if (m_player) {
        m_player->disconnect(this);
        m_player->stop();
        delete m_player;
    }
    m_path = file.absoluteFilePath();
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
void MediaBackend::togglePlayback()
{
    if (!available()) return;
    if (playing()) m_player->pause();
    else {
        if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(0);
        m_player->play();
    }
}
void MediaBackend::pause() { if (available()) m_player->pause(); }
void MediaBackend::stop()
{
    if (m_player) { m_player->stop(); m_player->setPosition(0); }
    emit changed();
}
void MediaBackend::seek(qint64 milliseconds)
{
    if (seekable()) m_player->setPosition(std::clamp(milliseconds, qint64(0), duration()));
}
void MediaBackend::setVolume(int percent) { m_audio->setVolume(std::clamp(percent, 0, 100) / 100.0f); emit changed(); }
void MediaBackend::setMuted(bool muted) { m_audio->setMuted(muted); emit changed(); }
int MediaBackend::volume() const { return qRound(m_audio->volume() * 100); }
bool MediaBackend::muted() const { return m_audio->isMuted(); }
qint64 MediaBackend::position() const { return m_player ? m_player->position() : 0; }
qint64 MediaBackend::duration() const { return m_player ? m_player->duration() : 0; }
bool MediaBackend::seekable() const { return available() && m_player->isSeekable() && duration() > 0; }
bool MediaBackend::playing() const { return m_player && m_player->playbackState() == QMediaPlayer::PlayingState; }
bool MediaBackend::hasVideo() const { return m_player && m_player->hasVideo(); }
bool MediaBackend::available() const { return m_player && m_player->error() == QMediaPlayer::NoError && m_player->mediaStatus() != QMediaPlayer::InvalidMedia; }
QString MediaBackend::statusText() const
{
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
