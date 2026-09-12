#include "MainWindow.h"
#include "MediaBackend.h"
#include "SeekPreview.h"
#include "AudioVisualizer.h"
#include <QStackedWidget>
#include <QVideoWidget>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QLabel>
#include <QPushButton>
#include <QBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>
#include <QTimer>
#include <QAction>
#include <QPainter>

namespace {
QIcon loopIcon(QWidget *widget, bool enabled)
{
    const qreal scale = widget->devicePixelRatioF();
    QPixmap pixmap(qRound(22 * scale), qRound(22 * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(enabled ? QColor(255, 166, 63) : QColor(155, 155, 155), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(4, 7), QPointF(18, 7));
    painter.drawLine(QPointF(18, 7), QPointF(15, 4));
    painter.drawLine(QPointF(18, 7), QPointF(15, 10));
    painter.drawLine(QPointF(18, 15), QPointF(4, 15));
    painter.drawLine(QPointF(4, 15), QPointF(7, 12));
    painter.drawLine(QPointF(4, 15), QPointF(7, 18));
    painter.drawLine(QPointF(4, 7), QPointF(4, 10));
    painter.drawLine(QPointF(18, 12), QPointF(18, 15));
    if (enabled) {
        painter.setPen(QPen(QColor(255, 166, 63), 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(10, 10), QPointF(11, 9));
        painter.drawLine(QPointF(11, 9), QPointF(11, 13));
    }
    painter.end();
    return QIcon(pixmap);
}
QIcon fullscreenIcon(QWidget *widget)
{
    const qreal scale = widget->devicePixelRatioF();
    QPixmap pixmap(qRound(22 * scale), qRound(22 * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(widget->palette().color(QPalette::ButtonText), 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        // Four open corners suggest expanding the picture to fill the screen.
        for (const auto &corner : {QPointF(3, 3), QPointF(19, 3), QPointF(3, 19), QPointF(19, 19)}) {
            const qreal dx = corner.x() < 11 ? 5 : -5;
            const qreal dy = corner.y() < 11 ? 5 : -5;
            painter.drawLine(corner, corner + QPointF(dx, 0));
            painter.drawLine(corner, corner + QPointF(0, dy));
        }
    }
    return QIcon(pixmap);
}
QIcon mediaIcon(QWidget *widget, QStyle::StandardPixmap symbol)
{
    QIcon result;
    const qreal scale = widget->devicePixelRatioF();
    const int pixels = qRound(22 * scale);
    const QIcon source = widget->style()->standardIcon(symbol);
    for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
        QPixmap pixmap = source.pixmap(pixels, pixels);
        {
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), widget->palette().color(
                mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText));
        }
        pixmap.setDevicePixelRatio(scale);
        result.addPixmap(pixmap, mode);
    }
    return result;
}
QString timestamp(qint64 ms)
{
    const qint64 seconds = ms / 1000;
    const QString tail = QString("%1:%2").arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
    return seconds >= 3600 ? QString("%1:").arg(seconds / 3600, 2, 10, QChar('0')) + tail : tail;
}
// Click and drag preview the target; only release commits the seek.
class SeekSlider : public QSlider
{
public:
    SeekSlider() : QSlider(Qt::Horizontal) { setRange(0, 100000); setFocusPolicy(Qt::NoFocus); }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) { event->ignore(); return; }
        setSliderDown(true); moveTo(event); event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (isSliderDown()) moveTo(event);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && isSliderDown()) { moveTo(event); setSliderDown(false); }
        event->accept();
    }
private:
    void moveTo(QMouseEvent *event) {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const int handle = style()->pixelMetric(QStyle::PM_SliderLength, &option, this);
        setValue(QStyle::sliderValueFromPosition(minimum(), maximum(),
            qRound(event->position().x()) - handle / 2, qMax(1, width() - handle), option.upsideDown));
    }
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_backend(new MediaBackend(this))
{
    setWindowTitle(QStringLiteral("Orange"));
    setWindowIcon(QIcon(QStringLiteral(":/orange/orange.ico")));
    resize(960, 640);
    setMinimumSize(560, 360);
    setAcceptDrops(true);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    // Let the video reach the menu bar and both window edges.
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *video = new QVideoWidget;
    // QVideoWidget embeds a window container which accepts DragEnter but does
    // not implement file drops. Intercept its events before it consumes them.
    video->setAcceptDrops(true);
    video->installEventFilter(this);
    for (auto *surface : video->findChildren<QWidget *>()) {
        surface->setAcceptDrops(true);
        surface->installEventFilter(this);
    }
    video->setAspectRatioMode(Qt::KeepAspectRatio);
    video->setMinimumSize(320, 180);
    video->setStyleSheet("background-color: black;");
    auto palette = video->palette();
    palette.setColor(QPalette::Window, Qt::black);
    video->setPalette(palette);
    video->setAutoFillBackground(true);
    m_display = new QStackedWidget;
    m_display->addWidget(video);
    m_visualizer = new AudioVisualizer;
    m_visualizer->setAcceptDrops(true);
    m_visualizer->installEventFilter(this);
    m_display->addWidget(m_visualizer);
    layout->addWidget(m_display, 1);
    m_backend->setVideoSink(video->videoSink());
    auto *timeline = new QHBoxLayout;
    timeline->setContentsMargins(6, 0, 6, 0);
    m_seek = new SeekSlider;
    m_seek->setObjectName("seekSlider");
    m_time = new QLabel;
    timeline->addWidget(m_seek, 1);
    timeline->addWidget(m_time);
    layout->addLayout(timeline);
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(6, 0, 6, 6);
    auto iconButton = [](const QIcon &icon, const QString &name, const QString &label) {
        auto *button = new QPushButton;
        button->setObjectName(name);
        button->setIcon(icon);
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(40, 32);
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    m_backward = iconButton(mediaIcon(this, QStyle::SP_MediaSeekBackward), "seekBackwardButton", tr("10秒戻る (←)"));
    m_play = iconButton(mediaIcon(this, QStyle::SP_MediaPlay), "playButton", tr("再生"));
    m_pause = iconButton(mediaIcon(this, QStyle::SP_MediaPause), "pauseButton", tr("一時停止"));
    m_forward = iconButton(mediaIcon(this, QStyle::SP_MediaSeekForward), "seekForwardButton", tr("10秒進む (→)"));
    m_mute = iconButton(mediaIcon(this, QStyle::SP_MediaVolume), "muteButton", tr("ミュート (M)"));
    m_mute->setCheckable(true);
    m_loop = iconButton(loopIcon(this, false), "loopButton", tr("ループ再生：OFF"));
    m_loop->setCheckable(true);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setObjectName("volumeSlider");
    volume->installEventFilter(this);
    volume->setAccessibleName(tr("音量"));
    volume->setRange(0, 100);
    volume->setValue(m_backend->volume());
    volume->setMaximumWidth(140);
    volume->setToolTip(tr("音量 0～100%"));
    auto *volumeText = new QLabel(tr("50%"));
    volumeText->setMinimumWidth(36);
    auto *fullscreen = iconButton(fullscreenIcon(this), "fullscreenButton", tr("全画面切り替え (F11)、解除 (Esc)"));
    for (auto *button : {m_backward, m_play, m_pause, m_forward, fullscreen}) {
        button->setFocusPolicy(Qt::NoFocus);
        controls->addWidget(button);
    }
    m_stepModeLabel = new QLabel;
    m_stepModeLabel->setObjectName("stepModeLabel");
    controls->addWidget(m_stepModeLabel);
    controls->addStretch();
    controls->addWidget(m_loop);
    controls->addWidget(m_mute);
    controls->addWidget(volume);
    controls->addWidget(volumeText);
    layout->addLayout(controls);
    setCentralWidget(central);
    m_preview = new SeekPreview(m_seek);
    // Keep the height font-driven so menu text also fits at higher DPI.
    menuBar()->setStyleSheet("QMenuBar { padding: 0px; margin: 0px; }"
                            "QMenuBar::item { padding: 2px 8px; margin: 0px; }");
    auto *fileMenu = menuBar()->addMenu(tr("ファイル(&F)"));
    fileMenu->addAction(tr("開く…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    m_closeAction = fileMenu->addAction(tr("閉じる"), m_backend, &MediaBackend::close);
    m_closeAction->setObjectName("closeMediaAction");
    m_contextMenu = new QMenu(this);
    m_contextMenu->setObjectName("mediaContextMenu");
    m_contextMenu->addAction(m_closeAction);
    fileMenu->addAction(tr("終了"), QKeySequence::Quit, this, &QWidget::close);
    auto *playMenu = menuBar()->addMenu(tr("再生(&P)"));
    auto *toggleAction = playMenu->addAction(tr("再生／一時停止"), QKeySequence(Qt::Key_Space), m_backend, &MediaBackend::togglePlayback);
    toggleAction->setAutoRepeat(false);
    playMenu->addAction(tr("10秒戻る"), QKeySequence(Qt::Key_Left), this, [this] { m_backend->seek(m_backend->position() - 10000); });
    playMenu->addAction(tr("10秒進む"), QKeySequence(Qt::Key_Right), this, [this] { m_backend->seek(m_backend->position() + 10000); });
    auto *stopAction = playMenu->addAction(tr("停止（先頭に戻す）"), m_backend, &MediaBackend::stop);
    playMenu->addAction(tr("1秒戻る"), QKeySequence(Qt::CTRL | Qt::Key_Left), this, [this] { m_backend->stepSecond(-1); });
    playMenu->addAction(tr("1秒進む"), QKeySequence(Qt::CTRL | Qt::Key_Right), this, [this] { m_backend->stepSecond(1); });
    m_previousFrame = playMenu->addAction(tr("1コマ戻る"), QKeySequence(Qt::ALT | Qt::Key_Left), this, [this] { m_backend->stepFrame(-1); });
    m_nextFrame = playMenu->addAction(tr("1コマ進む"), QKeySequence(Qt::ALT | Qt::Key_Right), this, [this] { m_backend->stepFrame(1); });
    m_previousFrame->setObjectName("previousFrameAction");
    m_nextFrame->setObjectName("nextFrameAction");
    m_previousFrame->setAutoRepeat(false);
    m_nextFrame->setAutoRepeat(false);
    stopAction->setObjectName("stopAction");
    playMenu->addAction(tr("ミュート"), QKeySequence(Qt::Key_M), this, [this] { m_backend->setMuted(!m_backend->muted()); });
    m_dvdMenu = playMenu->addMenu(tr("DVDタイトル"));
    m_dvdMenu->setObjectName("dvdTitlesMenu");
    connect(m_dvdMenu, &QMenu::aboutToShow, this, [this] {
        m_dvdMenu->clear();
        const auto titles = m_backend->dvdTitles();
        for (int i = 0; i < titles.size(); ++i) {
            auto *action = m_dvdMenu->addAction(titles[i], this, [this, i] { m_backend->selectDvdTitle(i); });
            action->setCheckable(true);
            action->setChecked(i == m_backend->dvdTitle());
        }
    });
    auto toggleFullscreen = [this] { isFullScreen() ? showNormal() : showFullScreen(); };
    auto *viewMenu = menuBar()->addMenu(tr("表示(&V)"));
    viewMenu->addAction(tr("全画面"), QKeySequence(Qt::Key_F11), this, toggleFullscreen);
    viewMenu->addAction(tr("全画面解除"), QKeySequence(Qt::Key_Escape), this, [this] { if (isFullScreen()) showNormal(); });
    connect(fullscreen, &QPushButton::clicked, this, toggleFullscreen);
    connect(m_play, &QPushButton::clicked, this, [this] {
        if (!m_backend->playing()) m_backend->togglePlayback();
    });
    connect(m_pause, &QPushButton::clicked, m_backend, &MediaBackend::pause);
    connect(m_backward, &QPushButton::clicked, this, [this] { m_backend->seek(m_backend->position() - 10000); });
    connect(m_forward, &QPushButton::clicked, this, [this] { m_backend->seek(m_backend->position() + 10000); });
    connect(m_mute, &QPushButton::clicked, m_backend, &MediaBackend::setMuted);
    connect(m_loop, &QPushButton::clicked, m_backend, &MediaBackend::setLooping);
    connect(volume, &QSlider::valueChanged, this, [this, volumeText](int value) {
        m_backend->setVolume(value); volumeText->setText(tr("%1%").arg(value));
    });
    connect(m_seek, &QSlider::valueChanged, this, [this] {
        if (m_seek->isSliderDown()) m_time->setText(timestamp(m_backend->duration() * m_seek->value() / m_seek->maximum()) + " / " + timestamp(m_backend->duration()));
    });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_backend->seek(m_backend->duration() * m_seek->value() / m_seek->maximum()); refresh();
    });
    connect(m_backend, &MediaBackend::changed, this, &MainWindow::refresh);
    connect(m_backend, &MediaBackend::audioLevels, m_visualizer, &AudioVisualizer::setLevels);
    connect(m_backend, &MediaBackend::failure, this, [this](const QString &path, const QString &message) {
        auto *dialog = new QMessageBox(QMessageBox::Warning, tr("再生エラー"), path + "\n\n" + message, QMessageBox::Ok, this);
        dialog->setTextFormat(Qt::PlainText);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    refresh();
}
void MainWindow::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("動画・音声ファイルを開く"),
        m_backend->filePath(), tr("動画・音声ファイル (*.wmv *.mp4 *.avi *.m4v *.asf *.iso *.wav *.mp3 *.flac *.m4a *.aac *.ogg *.opus *.wma);;音声ファイル (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.opus *.wma);;動画ファイル (*.wmv *.mp4 *.avi *.m4v *.asf *.iso);;すべてのファイル (*)"));
    if (!path.isEmpty()) openFile(path);
}
void MainWindow::openFile(const QString &path) { m_backend->open(path); }
void MainWindow::refresh()
{
    const auto mode = m_backend->stepMode();
    m_stepModeLabel->setText(mode == MediaBackend::StepMode::Frame ? tr("コマ送り") : tr("1秒再生"));
    m_stepModeLabel->setVisible(mode != MediaBackend::StepMode::None);
    m_previousFrame->setEnabled(m_backend->canStepFrame());
    m_nextFrame->setEnabled(m_backend->canStepFrame());
    if (m_displayPath != m_backend->filePath()) {
        m_displayPath = m_backend->filePath();
        m_visualizer->reset();
    }
    m_display->setCurrentIndex(m_backend->audioOnly() ? 1 : 0);
    m_visualizer->setRunning(m_backend->audioOnly() && m_backend->playing());
    m_closeAction->setEnabled(!m_backend->filePath().isEmpty());
    m_play->setEnabled(m_backend->available() && !m_backend->playing());
    m_pause->setEnabled(m_backend->available() && m_backend->playing());
    m_backward->setEnabled(m_backend->seekable());
    m_forward->setEnabled(m_backend->seekable());
    m_seek->setEnabled(m_backend->seekable());
    m_preview->setMedia(m_backend->filePath(), m_backend->duration(), m_backend->seekable(), m_backend->hasVideo() && !m_backend->isDvd());
    m_dvdMenu->menuAction()->setVisible(m_backend->isDvd());
    m_dvdMenu->setEnabled(!m_backend->dvdTitles().isEmpty());
    m_mute->setChecked(m_backend->muted());
    m_loop->setChecked(m_backend->looping());
    m_loop->setIcon(loopIcon(this, m_backend->looping()));
    m_loop->setToolTip(m_backend->looping() ? tr("ループ再生：ON（現在のファイル／DVDタイトルを繰り返す）") : tr("ループ再生：OFF"));
    m_loop->setAccessibleName(m_loop->toolTip());
    m_mute->setIcon(mediaIcon(this, m_backend->muted() ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
    m_mute->setToolTip(m_backend->muted() ? tr("ミュート解除 (M)") : tr("ミュート (M)"));
    m_mute->setAccessibleName(m_mute->toolTip());
    if (!m_seek->isSliderDown()) {
        const QSignalBlocker blocker(m_seek);
        m_seek->setValue(m_backend->duration() > 0 ? int(m_backend->position() * m_seek->maximum() / m_backend->duration()) : 0);
        m_time->setText(timestamp(m_backend->position()) + " / " + timestamp(m_backend->duration()));
    }
    if (!m_backend->filePath().isEmpty()) {
        const QString name = QFileInfo(m_backend->filePath()).fileName();
        setWindowTitle(name + QStringLiteral(" — Orange"));
    } else {
        setWindowTitle(QStringLiteral("Orange"));
    }
}
void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    m_contextMenu->popup(event->globalPos());
    event->accept();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    handleFileDrop(event);
}
void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    handleFileDrop(event);
}
void MainWindow::dropEvent(QDropEvent *event)
{
    handleFileDrop(event);
}
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ContextMenu) {
        contextMenuEvent(static_cast<QContextMenuEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride && qobject_cast<QSlider *>(watched)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ControlModifier || key->modifiers() == Qt::AltModifier)
            && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
            // Let the window's seek shortcuts win over QSlider's arrow handling.
            event->ignore();
            return true;
        }
    }
    if (handleFileDrop(event)) return true;
    return QMainWindow::eventFilter(watched, event);
}
bool MainWindow::handleFileDrop(QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        break;
    case QEvent::DragLeave:
        event->accept();
        return true;
    default:
        return false;
    }
    auto *drop = static_cast<QDropEvent *>(event);
    const auto urls = drop->mimeData()->urls();
    // Opening a file must never request that the drag source move/delete it.
    if (urls.isEmpty() || !urls.first().isLocalFile()
        || !QFileInfo(urls.first().toLocalFile()).isFile()
        || !drop->possibleActions().testFlag(Qt::CopyAction)) {
        drop->ignore();
        return true;
    }
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    if (event->type() == QEvent::Drop) {
        const QString path = urls.first().toLocalFile();
        // Finish the OS drag transaction before opening media and creating outputs.
        QTimer::singleShot(0, this, [this, path] { openFile(path); });
    }
    return true;
}
