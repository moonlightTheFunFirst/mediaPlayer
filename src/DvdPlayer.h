#pragma once
#include <QObject>
#include <QThread>
#include <QImage>
#include <QStringList>

struct DvdState {
    qint64 position = 0, duration = 0;
    bool available = false, playing = false, seekable = false, video = false;
    bool audioDecoded = false, audioOutput = false; // Integration diagnostics.
    int title = -1;
    QStringList titles;
    QList<int> titleIds;
};
Q_DECLARE_METATYPE(DvdState)
class DvdWorker;
// Serial numbers discard notifications from a DVD after the user opens another file.
class DvdPlayer : public QObject {
    Q_OBJECT
public:
    explicit DvdPlayer(QObject *parent = nullptr);
    ~DvdPlayer() override;
    void open(const QString &path, int volume, bool muted);
    void close();
    void play();
    void pause();
    void stop();
    void seek(qint64 time);
    void volume(int value, bool muted);
    void selectTitle(int index);
    const DvdState &state() const { return m_state; }
signals:
    void changed();
    void frame(const QImage &image);
    void failure(const QString &message);
private:
    QThread m_thread;
    DvdWorker *m_worker;
    quint64 m_serial = 0;
    DvdState m_state;
};
