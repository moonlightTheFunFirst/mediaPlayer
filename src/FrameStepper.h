#pragma once
#include <QObject>
#include <QThread>
#include <QImage>
#include <atomic>

class FrameStepper : public QObject
{
    Q_OBJECT
public:
    explicit FrameStepper(QObject *parent = nullptr);
    ~FrameStepper() override;
    void request(const QString &path, qint64 anchorUs, int direction);
    void cancel();
signals:
    void ready(const QImage &image, qint64 timeUs, const QString &error);
private:
    QThread m_thread;
    QObject *m_worker;
    std::atomic<quint64> m_serial{0};
};
