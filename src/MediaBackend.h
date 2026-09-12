#pragma once
#include <QMediaPlayer>
#include "AudioLevels.h"

class DvdPlayer;
class QAudioOutput;
class QVideoSink;

// All playback-library operations live here; the UI uses this small interface.
class MediaBackend : public QObject
{
    Q_OBJECT
public:
    explicit MediaBackend(QObject *parent = nullptr);
    ~MediaBackend() override;
    void setVideoSink(QVideoSink *sink);
    void open(const QString &path);
    void close();
    void togglePlayback();
    void pause();
    void stop();
    void seek(qint64 milliseconds);
    void setVolume(int percent);
    void setMuted(bool muted);
    qint64 position() const;
    qint64 duration() const;
    bool seekable() const;
    bool playing() const;
    bool hasVideo() const;
    bool audioOnly() const;
    bool available() const;
    bool muted() const;
    int volume() const;
    QString filePath() const { return m_path; }
    QString statusText() const;
    bool isDvd() const { return m_isDvd; }
    QStringList dvdTitles() const;
    int dvdTitle() const;
    void selectDvdTitle(int index);
    QMediaPlayer *player() const { return m_player; } // Integration diagnostics.
signals:
    void changed();
    void audioLevels(float energy, float bass);
    void failure(const QString &path, const QString &message);
private:
    DvdPlayer *m_dvd;
    bool m_isDvd = false;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio;
    QVideoSink *m_sink = nullptr;
    QString m_path;
    AudioLevels m_levels;
};
