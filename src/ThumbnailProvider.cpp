#include "ThumbnailProvider.h"
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>
#include <algorithm>

class ThumbnailWorker : public QObject
{
    Q_OBJECT
public slots:
    void cancel()
    {
        m_timeout.stop();
        m_armed = false;
        m_finishing = false;
        m_serial = 0;
        delete m_player;
        m_player = nullptr;
        delete m_sink;
        m_sink = nullptr;
    }
    void generate(const QString &path, qint64 position, quint64 serial)
    {
        cancel();
        m_serial = serial;
        m_position = position;
        m_target = position;
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        // No QAudioOutput is attached: preview decoding cannot produce sound.
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this] { finish({}); });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status != QMediaPlayer::LoadedMedia || m_armed) return;
            if (!m_player->hasVideo() || !m_player->isSeekable()) { finish({}); return; }
            m_target = std::clamp(m_position, qint64(0), std::max(qint64(0), m_player->duration() - 100));
            m_player->setActiveAudioTrack(-1);
            m_player->setPosition(m_target);
            m_armed = true;
            m_player->play();
        });
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (!m_armed || !frame.isValid()) return;
            const qint64 time = frame.startTime() / 1000;
            if (frame.startTime() < 0 || time < m_target - 150 || time > m_target + 1500) return;
            const QImage image = frame.toImage();
            if (!image.isNull()) finish(image.scaled(200, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });
        m_timeout.start(5000);
        m_player->setSource(QUrl::fromLocalFile(path));
    }
public:
    ThumbnailWorker()
    {
        m_timeout.setParent(this);
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish({}); });
    }
    ~ThumbnailWorker() override { cancel(); }
signals:
    void completed(quint64 serial, qint64 position, const QImage &image);
private:
    void finish(const QImage &image)
    {
        if (m_finishing) return;
        m_finishing = true;
        m_timeout.stop();
        m_armed = false;
        const auto serial = m_serial;
        const auto position = m_position;
        // Retire the decoder outside its signal stack.
        QTimer::singleShot(0, this, [this, serial, position, image] {
            if (serial != m_serial) return;
            cancel();
            emit completed(serial, position, image);
        });
    }
    QTimer m_timeout;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    qint64 m_position = 0;
    qint64 m_target = 0;
    quint64 m_serial = 0;
    bool m_armed = false;
    bool m_finishing = false;
};

ThumbnailProvider::ThumbnailProvider(QObject *parent) : QObject(parent), m_worker(new ThumbnailWorker)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &ThumbnailProvider::generate, m_worker, &ThumbnailWorker::generate);
    connect(this, &ThumbnailProvider::abort, m_worker, &ThumbnailWorker::cancel);
    connect(m_worker, &ThumbnailWorker::completed, this, [this](quint64 serial, qint64 position, const QImage &image) {
        if (serial != m_serial) return;
        m_cache.insert(position, new QImage(image));
        emit ready(position, image);
    });
    m_thread.start();
}
ThumbnailProvider::~ThumbnailProvider()
{
    m_thread.quit();
    m_thread.wait();
}
void ThumbnailProvider::cancel() { ++m_serial; emit abort(); }
void ThumbnailProvider::setSource(const QString &path)
{
    if (m_path == path) return;
    cancel();
    m_path = path;
    m_cache.clear();
}
void ThumbnailProvider::request(qint64 position)
{
    cancel();
    if (m_path.isEmpty()) return;
    position = std::max(qint64(0), position) / 1000 * 1000;
    if (const auto *image = m_cache.object(position)) { emit ready(position, *image); return; }
    emit generate(m_path, position, m_serial);
}
#include "ThumbnailProvider.moc"
