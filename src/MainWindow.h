#pragma once
#include <QMainWindow>

class MediaBackend;
class QSlider;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    bool handleFileDrop(QEvent *event);
    void chooseFile();
    void refresh();
    MediaBackend *m_backend;
    QSlider *m_seek;
    QLabel *m_time;
    QPushButton *m_play;
    QPushButton *m_pause;
    QPushButton *m_backward;
    QPushButton *m_forward;
    QPushButton *m_mute;
};
