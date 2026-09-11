#include "SeekPreview.h"
#include "ThumbnailProvider.h"
#include <QSlider>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>

SeekPreview::SeekPreview(QSlider *slider) : QObject(slider), m_slider(slider),
    m_popup(new QFrame(slider->window(), Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput)),
    m_image(new QLabel(m_popup)), m_time(new QLabel(m_popup)), m_provider(new ThumbnailProvider(this))
{
    m_popup->setObjectName("seekPreviewPopup");
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_popup->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_popup->setStyleSheet("QFrame#seekPreviewPopup {background:#202020; border:1px solid #666; border-radius:5px;} QLabel {color:white; border:none;}");
    auto *layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(5);
    m_image->setObjectName("seekPreviewImage");
    m_image->setFixedSize(200, 112);
    m_image->setAlignment(Qt::AlignCenter);
    m_time->setObjectName("seekPreviewTime");
    m_time->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_image);
    layout->addWidget(m_time);
    m_slider->setMouseTracking(true);
    m_slider->installEventFilter(this);
    m_slider->window()->installEventFilter(this);
    m_delay.setSingleShot(true);
    m_delay.setInterval(200);
    connect(&m_delay, &QTimer::timeout, this, [this] { m_provider->request(m_position); });
    connect(m_provider, &ThumbnailProvider::ready, this, [this](qint64 position, const QImage &image) {
        if (!m_popup->isVisible() || position != m_position / 1000 * 1000) return;
        if (image.isNull()) m_image->hide();
        else { m_image->setPixmap(QPixmap::fromImage(image)); m_image->show(); }
        m_popup->adjustSize();
        placePopup();
    });
}
SeekPreview::~SeekPreview() { delete m_popup; }
void SeekPreview::setMedia(const QString &path, qint64 duration, bool seekable, bool hasVideo)
{
    if (m_path != path || m_hasVideo != hasVideo) hide();
    m_path = path;
    m_duration = duration;
    m_seekable = seekable && duration > 0;
    m_hasVideo = hasVideo;
    m_provider->setSource(path);
    if (!m_seekable) hide();
}
void SeekPreview::hide()
{
    m_popup->hide();
    m_delay.stop();
    m_provider->cancel();
    m_position = -1;
}
void SeekPreview::placePopup()
{
    const QPoint global = m_slider->mapToGlobal(m_point);
    QRect bounds = m_slider->window()->frameGeometry();
    if (auto *screen = QGuiApplication::screenAt(global)) bounds = bounds.intersected(screen->availableGeometry());
    const int x = std::clamp(global.x() - m_popup->width() / 2, bounds.left(), std::max(bounds.left(), bounds.right() - m_popup->width() + 1));
    const int y = std::max(bounds.top(), m_slider->mapToGlobal(QPoint(0, 0)).y() - m_popup->height() - 8);
    m_popup->move(x, y);
}
void SeekPreview::showAt(const QPoint &point)
{
    if (!m_seekable || !m_slider->isEnabled()) { hide(); return; }
    m_point = point;
    QStyleOptionSlider option;
    option.initFrom(m_slider);
    option.orientation = Qt::Horizontal;
    const int handle = m_slider->style()->pixelMetric(QStyle::PM_SliderLength, &option, m_slider);
    const int value = QStyle::sliderValueFromPosition(0, 100000, point.x() - handle / 2,
        std::max(1, m_slider->width() - handle), m_slider->invertedAppearance() != (m_slider->layoutDirection() == Qt::RightToLeft));
    const qint64 position = m_duration * value / 100000;
    const bool changed = m_position < 0 || position / 1000 != m_position / 1000;
    m_position = position;
    const qint64 seconds = position / 1000;
    QString time = QString("%1:%2").arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
    if (seconds >= 3600) time.prepend(QString("%1:").arg(seconds / 3600, 2, 10, QChar('0')));
    m_time->setText(time);
    if (changed) {
        m_provider->cancel();
        m_delay.stop();
        m_image->clear();
        m_image->setVisible(m_hasVideo);
        if (m_hasVideo) { m_image->setText(tr("読み込み中…")); m_delay.start(); }
    }
    m_popup->adjustSize();
    placePopup();
    m_popup->show();
}
bool SeekPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_slider) {
        if (event->type() == QEvent::Enter)
            showAt(static_cast<QEnterEvent *>(event)->position().toPoint());
        else if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)
            showAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) hide();
    } else if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Move || event->type() == QEvent::Resize) hide();
    return QObject::eventFilter(watched, event);
}
