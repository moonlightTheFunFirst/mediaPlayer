#include <QApplication>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaPlayer>
#include <QVideoWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QAudioOutput audio;
    QVideoWidget video;
    QMediaPlayer player;
    player.setAudioOutput(&audio);
    player.setVideoOutput(&video);
    qInfo() << "Qt" << qVersion() << "Widgets / Multimedia / MultimediaWidgets initialized";
    return 0;
}
