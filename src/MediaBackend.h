#pragma once
#include <QMediaPlayer>

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
    bool available() const;
    bool muted() const;
    int volume() const;
    QString filePath() const { return m_path; }
    QString statusText() const;
    QMediaPlayer *player() const { return m_player; } // Integration diagnostics.
signals:
    void changed();
    void failure(const QString &path, const QString &message);
private:
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio;
    QVideoSink *m_sink = nullptr;
    QString m_path;
};
