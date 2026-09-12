#pragma once
#include <QMediaPlayer>
#include <QVideoFrame>
#include "AudioLevels.h"

class DvdPlayer;
class QAudioOutput;
class QVideoSink;
class FrameStepper;
class VideoEnhancer;

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
    void stepFrame(int direction);
    void stepSecond(int direction);
    enum class StepMode { None, Frame, Second };
    StepMode stepMode() const { return m_frameTimeUs >= 0 ? StepMode::Frame : (m_secondMode ? StepMode::Second : StepMode::None); }
    bool canStepFrame() const;
    bool frameStepBusy() const { return m_frameBusy; }
    void setVolume(int percent);
    void setMuted(bool muted);
    void setLooping(bool enabled);
    bool looping() const { return m_looping; }
    void setVideoEnhancement(bool enabled);
    bool videoEnhancement() const { return m_videoEnhancement; }
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
    void presentFrame(const QVideoFrame &frame);
    QVideoSink *playbackSink() const;
    VideoEnhancer *m_enhancer;
    QVideoSink *m_filterSink;
    bool m_videoEnhancement = false;
    bool m_stepFilterPending = false;
    QVideoFrame m_originalFrame, m_displayOriginal;
    void leaveFrameMode();
    FrameStepper *m_stepper;
    qint64 m_frameTimeUs = -1;
    bool m_frameBusy = false;
    bool m_secondMode = false;
    DvdPlayer *m_dvd;
    bool m_isDvd = false;
    bool m_looping = false;
    bool m_playRequested = false;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio;
    QVideoSink *m_sink = nullptr;
    QString m_path;
    AudioLevels m_levels;
};
