#include "MainWindow.h"
#include "MediaBackend.h"
#include <QVideoWidget>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>
#include <QLabel>
#include <QPushButton>
#include <QBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>
#include <QTimer>

namespace {
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
    setWindowTitle(tr("Media Player"));
    resize(960, 640);
    setMinimumSize(560, 360);
    setAcceptDrops(true);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
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
    layout->addWidget(video, 1);
    m_backend->setVideoSink(video->videoSink());
    auto *timeline = new QHBoxLayout;
    m_seek = new SeekSlider;
    m_seek->setToolTip(tr("クリック・ドラッグで再生位置を移動"));
    m_time = new QLabel;
    timeline->addWidget(m_seek, 1);
    timeline->addWidget(m_time);
    layout->addLayout(timeline);
    auto *controls = new QHBoxLayout;
    auto *open = new QPushButton(tr("開く"));
    m_play = new QPushButton;
    m_stop = new QPushButton(tr("停止"));
    m_mute = new QPushButton(tr("ミュート"));
    m_mute->setCheckable(true);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(m_backend->volume());
    volume->setMaximumWidth(140);
    volume->setToolTip(tr("音量 0～100%"));
    auto *volumeText = new QLabel(tr("50%"));
    volumeText->setMinimumWidth(36);
    auto *fullscreen = new QPushButton(tr("全画面"));
    for (auto *button : {open, m_play, m_stop, m_mute, fullscreen}) {
        button->setFocusPolicy(Qt::NoFocus);
        controls->addWidget(button);
    }
    open->setToolTip(tr("ファイルを開く (Ctrl+O)"));
    m_play->setToolTip(tr("再生／一時停止 (Space)"));
    m_stop->setToolTip(tr("停止して先頭に戻す"));
    m_mute->setToolTip(tr("ミュート切り替え (M)"));
    fullscreen->setToolTip(tr("全画面切り替え (F11)、解除 (Esc)"));
    controls->addStretch();
    controls->addWidget(new QLabel(tr("音量")));
    controls->addWidget(volume);
    controls->addWidget(volumeText);
    layout->addLayout(controls);
    setCentralWidget(central);
    auto *fileMenu = menuBar()->addMenu(tr("ファイル(&F)"));
    fileMenu->addAction(tr("開く…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    fileMenu->addAction(tr("終了"), QKeySequence::Quit, this, &QWidget::close);
    auto *playMenu = menuBar()->addMenu(tr("再生(&P)"));
    playMenu->addAction(tr("再生／一時停止"), QKeySequence(Qt::Key_Space), m_backend, &MediaBackend::togglePlayback);
    playMenu->addAction(tr("停止"), m_backend, &MediaBackend::stop);
    playMenu->addAction(tr("ミュート"), QKeySequence(Qt::Key_M), this, [this] { m_backend->setMuted(!m_backend->muted()); });
    auto toggleFullscreen = [this] { isFullScreen() ? showNormal() : showFullScreen(); };
    auto *viewMenu = menuBar()->addMenu(tr("表示(&V)"));
    viewMenu->addAction(tr("全画面"), QKeySequence(Qt::Key_F11), this, toggleFullscreen);
    viewMenu->addAction(tr("全画面解除"), QKeySequence(Qt::Key_Escape), this, [this] { if (isFullScreen()) showNormal(); });
    connect(fullscreen, &QPushButton::clicked, this, toggleFullscreen);
    connect(open, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(m_play, &QPushButton::clicked, m_backend, &MediaBackend::togglePlayback);
    connect(m_stop, &QPushButton::clicked, m_backend, &MediaBackend::stop);
    connect(m_mute, &QPushButton::clicked, m_backend, &MediaBackend::setMuted);
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
        m_backend->filePath(), tr("動画ファイル (*.wmv *.mp4 *.avi *.m4v *.asf);;すべてのファイル (*)"));
    if (!path.isEmpty()) openFile(path);
}
void MainWindow::openFile(const QString &path) { m_backend->open(path); }
void MainWindow::refresh()
{
    m_play->setText(m_backend->playing() ? tr("一時停止") : tr("再生"));
    m_play->setEnabled(m_backend->available());
    m_stop->setEnabled(m_backend->available());
    m_seek->setEnabled(m_backend->seekable());
    m_mute->setChecked(m_backend->muted());
    if (!m_seek->isSliderDown()) {
        const QSignalBlocker blocker(m_seek);
        m_seek->setValue(m_backend->duration() > 0 ? int(m_backend->position() * m_seek->maximum() / m_backend->duration()) : 0);
        m_time->setText(timestamp(m_backend->position()) + " / " + timestamp(m_backend->duration()));
    }
    if (!m_backend->filePath().isEmpty()) {
        const QString name = QFileInfo(m_backend->filePath()).fileName();
        setWindowTitle(name + tr(" — Media Player"));
    }
    statusBar()->showMessage(m_backend->statusText());
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
