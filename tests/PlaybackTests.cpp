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

class PlaybackTests : public QObject
{
    Q_OBJECT
    QString root = qEnvironmentVariable("QT_MEDIA_TEST_ROOT");
private slots:
    void widgetControls()
    {
        MainWindow window;
        window.show();
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(video);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 15000);
        QPushButton *pause = nullptr;
        QPushButton *stop = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (button->text() == QString::fromUtf8("一時停止")) pause = button;
            if (button->text() == QString::fromUtf8("停止")) stop = button;
        }
        QVERIFY(pause);
        QVERIFY(stop);
        QTest::mouseClick(pause, Qt::LeftButton);
        QCOMPARE(pause->text(), QString::fromUtf8("再生"));
        QTest::qWait(250); // Allow the video widget to present its paused frame.
        const QString screenshot = qEnvironmentVariable("MEDIAPLAYER_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot));
        QTest::mouseClick(stop, Qt::LeftButton);
        QCOMPARE(pause->text(), QString::fromUtf8("再生"));
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
