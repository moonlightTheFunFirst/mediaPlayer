#pragma once
#include <QObject>
#include <QThread>
#include <QVideoFrame>
#include <atomic>
#include <functional>

class VideoEnhancer : public QObject
{
    Q_OBJECT
public:
    explicit VideoEnhancer(QObject *parent = nullptr);
    ~VideoEnhancer() override;
    void submit(const QVideoFrame &frame);
    void cancel();
    static QImage sharpen(const QImage &image, const std::function<bool()> &cancelled = {});
signals:
    void ready(const QVideoFrame &original, const QVideoFrame &filtered);
private:
    void startPending();
    QThread m_thread;
    QObject *m_worker;
    std::atomic<quint64> m_epoch{0};
    bool m_busy = false;
    QVideoFrame m_pending;
};
