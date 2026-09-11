#pragma once
#include <QObject>
#include <QTimer>
#include <QPoint>
class QSlider;
class QFrame;
class QLabel;
class ThumbnailProvider;

class SeekPreview : public QObject
{
    Q_OBJECT
public:
    explicit SeekPreview(QSlider *slider);
    ~SeekPreview() override;
    void setMedia(const QString &path, qint64 duration, bool seekable, bool hasVideo);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void showAt(const QPoint &point);
    void placePopup();
    void hide();
    QSlider *m_slider;
    QFrame *m_popup;
    QLabel *m_image;
    QLabel *m_time;
    ThumbnailProvider *m_provider;
    QTimer m_delay;
    QString m_path;
    qint64 m_duration = 0;
    qint64 m_position = -1;
    bool m_seekable = false;
    bool m_hasVideo = false;
    QPoint m_point;
};
