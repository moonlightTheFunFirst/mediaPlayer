#pragma once
#include <QMainWindow>

class MediaBackend;
class QSlider;
class QLabel;
class QPushButton;
class SeekPreview;
class QMenu;
class QAction;
class AudioVisualizer;
class QStackedWidget;
class QDoubleSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);
protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    bool handleFileDrop(QEvent *event);
    void chooseFile();
    void refresh();
    void skip(int direction);
    void refreshSkipLabels();
    QDoubleSpinBox *m_skipSeconds;
    QAction *m_skipBackwardAction;
    QAction *m_skipForwardAction;
    QMenu *m_dvdMenu;
    QMenu *m_contextMenu;
    QAction *m_closeAction;
    QAction *m_previousFrame;
    QAction *m_nextFrame;
    MediaBackend *m_backend;
    QStackedWidget *m_display;
    AudioVisualizer *m_visualizer;
    QString m_displayPath;
    QSlider *m_seek;
    SeekPreview *m_preview;
    QLabel *m_time;
    QLabel *m_stepModeLabel;
    QPushButton *m_play;
    QPushButton *m_pause;
    QPushButton *m_backward;
    QPushButton *m_forward;
    QPushButton *m_mute;
    QPushButton *m_loop;
};
