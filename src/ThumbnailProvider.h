#pragma once
#include <QObject>
#include <QImage>
#include <QCache>
#include <QThread>

class ThumbnailWorker;

class ThumbnailProvider : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);
    ~ThumbnailProvider() override;
    void setSource(const QString &path);
    void request(qint64 position);
    void cancel();
signals:
    void ready(qint64 position, const QImage &image);
    void generate(const QString &path, qint64 position, quint64 serial);
    void abort();
private:
    QString m_path;
    quint64 m_serial = 0;
    QCache<qint64, QImage> m_cache{100}; // About 8 MB at 200 x 112 RGBA.
    QThread m_thread;
    ThumbnailWorker *m_worker;
};
