#include "../src/MediaBackend.h"
#include <QtTest>
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QMediaMetaData>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "../src/MainWindow.h"
#include <QVideoWidget>
#include <QPushButton>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QSlider>
#include <QAction>
#include <QDataStream>

class PlaybackTests : public QObject
{
    Q_OBJECT
    QString root = qEnvironmentVariable("QT_MEDIA_TEST_ROOT");
private slots:
    void dropOnVideoSurface_data()
    {
        QTest::addColumn<bool>("fullscreen");
        QTest::addColumn<bool>("alreadyPlaying");
        QTest::newRow("window-empty") << false << false;
        QTest::newRow("window-playing") << false << true;
        QTest::newRow("fullscreen-playing") << true << true;
    }
    void dropOnVideoSurface()
    {
        QFETCH(bool, fullscreen);
        QFETCH(bool, alreadyPlaying);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QString::fromUtf8("ドロップ 動画.avi"));
        QVERIFY(QFile::copy(QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.avi"), path));
        MainWindow window;
        if (fullscreen) window.showFullScreen(); else window.show();
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(video);
        if (alreadyPlaying) {
            window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
            QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 15000);
        }
        // Qt's embedded video window container consumes drag events before MainWindow.
        const auto children = video->findChildren<QWidget *>();
        QVERIFY(!children.isEmpty());
        QWidget *surface = children.first();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &drop);
        QVERIFY(drop.isAccepted());
        auto *backend = window.findChild<MediaBackend *>();
        QTRY_COMPARE(backend->filePath(), path);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 15000);
        QCOMPARE(drop.dropAction(), Qt::CopyAction);
        QVERIFY(QFileInfo::exists(path));
    }
    void rejectNonFileDrops()
    {
        MainWindow window;
        window.show();
        auto *surface = window.findChild<QVideoWidget *>()->findChildren<QWidget *>().first();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const auto &url : {QUrl("https://example.com/video.mp4"), QUrl::fromLocalFile(dir.path()), QUrl::fromLocalFile(dir.filePath("missing.mp4"))}) {
            QMimeData mime;
            mime.setUrls({url});
            QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(surface, &enter);
            QVERIFY(!enter.isAccepted());
        }
        QVERIFY(window.findChild<MediaBackend *>()->filePath().isEmpty());
    }
    void widgetControls()
    {
        MainWindow window;
        window.show();
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(video);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 15000);
        auto *pause = window.findChild<QPushButton *>("pauseButton");
        auto *play = window.findChild<QPushButton *>("playButton");
        auto *stop = window.findChild<QAction *>("stopAction");
        QVERIFY(pause);
        QVERIFY(play);
        QVERIFY(stop);
        QTest::mouseClick(pause, Qt::LeftButton);
        QVERIFY(play->isEnabled());
        QVERIFY(!pause->isEnabled());
        QTest::qWait(250); // Allow the video widget to present its paused frame.
        const QString screenshot = qEnvironmentVariable("MEDIAPLAYER_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot));
        stop->trigger();
        QCOMPARE(window.findChild<MediaBackend *>()->position(), qint64(0));
        QVERIFY(play->isEnabled());
    }
    void transportButtons()
    {
        // A 30-second PCM fixture makes +/-10 second seeks deterministic.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath("transport.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint32 bytes = 8000 * 2 * 30;
        stream.writeRawData("RIFF", 4); stream << quint32(36 + bytes);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000)
               << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4); stream << bytes;
        file.write(QByteArray(bytes, '\0'));
        file.close();
        MainWindow window;
        window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *play = window.findChild<QPushButton *>("playButton");
        auto *pause = window.findChild<QPushButton *>("pauseButton");
        auto *backward = window.findChild<QPushButton *>("seekBackwardButton");
        auto *forward = window.findChild<QPushButton *>("seekForwardButton");
        auto *mute = window.findChild<QPushButton *>("muteButton");
        auto *volume = window.findChild<QSlider *>("volumeSlider");
        QVERIFY(play && pause && backward && forward && mute && volume);
        for (auto *button : {play, pause, backward, forward}) {
            QVERIFY(!button->isEnabled());
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->accessibleName().isEmpty());
        }
        window.openFile(file.fileName());
        QTRY_VERIFY(backend->playing() && backend->seekable());
        QTest::mouseClick(pause, Qt::LeftButton);
        QTRY_COMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        backend->seek(12000);
        QTest::mouseClick(forward, Qt::LeftButton);
        QTRY_COMPARE(backend->position(), qint64(22000));
        QTest::mouseClick(backward, Qt::LeftButton);
        QTRY_COMPARE(backend->position(), qint64(12000));
        QCOMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        backend->seek(3000);
        QTest::mouseClick(backward, Qt::LeftButton);
        QTRY_COMPARE(backend->position(), qint64(0));
        backend->seek(25000);
        QTest::mouseClick(forward, Qt::LeftButton);
        QTRY_COMPARE(backend->position(), backend->duration());
        backend->seek(5000);
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_VERIFY(backend->playing());
        QTest::mouseClick(forward, Qt::LeftButton);
        QTRY_VERIFY(backend->position() >= 15000 && backend->position() < 16000);
        QVERIFY(backend->playing());
        volume->setValue(37);
        QTest::mouseClick(mute, Qt::LeftButton);
        QVERIFY(backend->muted());
        QVERIFY(mute->isChecked());
        QTest::mouseClick(mute, Qt::LeftButton);
        QVERIFY(!backend->muted());
        QCOMPARE(backend->volume(), 37);
        QCOMPARE(volume->value(), 37);
        window.findChild<QAction *>("stopAction")->trigger();
        QCOMPARE(backend->position(), qint64(0));
        QVERIFY(!backend->playing());
    }
    void decode_data()
    {
        QTest::addColumn<QString>("relative");
        QTest::addColumn<bool>("expectAudio");
        QTest::newRow("WMV") << "qmediaplayerformatsupport/testdata/containers/supported/container.wmv" << false;
        QTest::newRow("MP4") << "qmediaplayerformatsupport/testdata/containers/supported/container.mp4" << false;
        QTest::newRow("AVI") << "qmediaplayerformatsupport/testdata/containers/supported/container.avi" << false;
        QTest::newRow("MP4-audio") << "qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4" << true;
        if (!qEnvironmentVariableIsEmpty("WMV_AUDIO_SAMPLE"))
            QTest::newRow("WMV-audio") << qEnvironmentVariable("WMV_AUDIO_SAMPLE") << true;
        if (!qEnvironmentVariableIsEmpty("WMV9_SAMPLE"))
            QTest::newRow("WMV9-audio") << qEnvironmentVariable("WMV9_SAMPLE") << true;
    }
    void decode()
    {
        QFETCH(QString, relative);
        QFETCH(bool, expectAudio);
        QVERIFY2(!root.isEmpty(), "Set QT_MEDIA_TEST_ROOT to Qt source tests/auto/integration");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QString::fromUtf8("日本語 空白 テスト.") + QFileInfo(relative).suffix());
        QVERIFY(QFile::copy(QDir(root).filePath(relative), path));
        QAudioBufferOutput audio;
        QVideoSink sink;
        MediaBackend backend;
        backend.setVideoSink(&sink);
        QSignalSpy errors(&backend, &MediaBackend::failure);
        int frames = 0;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&frames](const QVideoFrame &frame) { if (frame.isValid()) ++frames; });
        backend.open(path);
        backend.player()->setAudioBufferOutput(&audio);
        int audioBuffers = 0;
        connect(&audio, &QAudioBufferOutput::audioBufferReceived, this, [&audioBuffers](const QAudioBuffer &buffer) { if (buffer.isValid()) ++audioBuffers; });
        QTRY_VERIFY_WITH_TIMEOUT(frames > 0 || !errors.isEmpty(), 15000);
        QVERIFY2(errors.isEmpty(), qPrintable(backend.player()->errorString()));
        QVERIFY(frames > 0);
        QTRY_VERIFY_WITH_TIMEOUT(backend.duration() > 0, 10000);
        const auto metadata = backend.player()->metaData();
        qInfo() << "Video:" << metadata.stringValue(QMediaMetaData::VideoCodec)
                << "Audio:" << metadata.stringValue(QMediaMetaData::AudioCodec)
                << "Duration:" << backend.duration();
        if (expectAudio) {
            QTRY_VERIFY_WITH_TIMEOUT(audioBuffers > 0, 10000);
            backend.pause();
            QTRY_COMPARE(backend.player()->playbackState(), QMediaPlayer::PausedState);
            QTRY_VERIFY(backend.seekable());
            backend.seek(backend.duration() / 2);
            QTRY_VERIFY(qAbs(backend.position() - backend.duration() / 2) < 1000);
            QCOMPARE(backend.player()->playbackState(), QMediaPlayer::PausedState);
            backend.togglePlayback();
            QTRY_VERIFY(backend.playing());
            backend.setVolume(37);
            backend.setMuted(true);
            QVERIFY(backend.muted());
            QCOMPARE(backend.volume(), 37);
            backend.setMuted(false);
            QCOMPARE(backend.volume(), 37);
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(backend.player()->mediaStatus(), QMediaPlayer::EndOfMedia, 10000);
        }
        backend.stop();
        QCOMPARE(backend.position(), qint64(0));
        QCOMPARE(backend.player()->playbackState(), QMediaPlayer::StoppedState);
        QCOMPARE(backend.filePath(), path);
        backend.togglePlayback();
        QTRY_VERIFY(backend.playing());
        backend.stop();
        QVERIFY(errors.isEmpty());
        backend.player()->setAudioBufferOutput(nullptr);
    }
    void invalidAndRapidSwitch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MediaBackend backend;
        QVideoSink sink;
        backend.setVideoSink(&sink);
        QSignalSpy errors(&backend, &MediaBackend::failure);
        backend.open(dir.filePath("missing.mp4"));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!backend.available());
        const QString broken = dir.filePath("broken.wmv");
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid media data");
        file.close();
        backend.open(broken);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 10000);
        QVERIFY(!backend.available());
        errors.clear();
        const QString mp4 = QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.mp4");
        const QString wmv = QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.wmv");
        for (int i = 0; i < 5; ++i) { backend.open(mp4); backend.open(wmv); }
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 15000);
        QCOMPARE(backend.filePath(), QFileInfo(wmv).absoluteFilePath());
        QVERIFY(errors.isEmpty());
        backend.stop();
        backend.setVideoSink(nullptr);
    }
};
QTEST_MAIN(PlaybackTests)
#include "PlaybackTests.moc"
