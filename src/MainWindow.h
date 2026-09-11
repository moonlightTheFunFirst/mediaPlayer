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
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    void chooseFile();
    void refresh();
    MediaBackend *m_backend;
    QSlider *m_seek;
    QLabel *m_time;
    QLabel *m_filename;
    QPushButton *m_play;
    QPushButton *m_stop;
    QPushButton *m_mute;
};
