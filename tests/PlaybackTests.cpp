#include "../src/MediaBackend.h"
#include "../src/DvdPlayer.h"
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
#include "../src/ThumbnailProvider.h"
#include <QLabel>
#include <QFrame>
#include <QMenu>

class PlaybackTests : public QObject
{
    Q_OBJECT
    QString root = qEnvironmentVariable("QT_MEDIA_TEST_ROOT");
private slots:
    void dvdIso()
    {
        const QString iso = qEnvironmentVariable("DVD_ISO_SAMPLE");
        if (iso.isEmpty()) QSKIP("Set DVD_ISO_SAMPLE to an unencrypted DVD-Video ISO");
        QVideoSink sink;
        MediaBackend backend;
        backend.setVideoSink(&sink);
        QSignalSpy errors(&backend, &MediaBackend::failure);
        backend.open(iso);
        QVERIFY(backend.isDvd());
        QTRY_VERIFY_WITH_TIMEOUT(backend.hasVideo() || !errors.isEmpty(), 30000);
        if (!errors.isEmpty()) qWarning() << errors.first();
        QVERIFY(errors.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.seekable(), 15000);
        QVERIFY(backend.duration() > 30000);
        qInfo() << "DVD duration/frame:" << backend.duration() << sink.videoFrame().size();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.dvdTitles().isEmpty(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.findChild<DvdPlayer *>()->state().audioOutput, 15000);
        qInfo() << "DVD settled frame/titles:" << sink.videoFrame().size() << backend.dvdTitles();
        backend.pause();
        QTRY_VERIFY(!backend.playing());
        backend.seek(15000);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(backend.position() - 15000) < 2500, 10000);
        QVERIFY(!backend.playing());
        backend.setVolume(37); backend.setMuted(true);
        QCOMPARE(backend.volume(), 37); QVERIFY(backend.muted());
        backend.setMuted(false); QCOMPARE(backend.volume(), 37);
        if (backend.dvdTitles().size() > 1) {
            const int next = backend.dvdTitle() == 0 ? 1 : 0;
            backend.selectDvdTitle(next);
            QTRY_COMPARE_WITH_TIMEOUT(backend.dvdTitle(), next, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(backend.playing(), 10000);
        }
        const int selectedTitle = backend.dvdTitle();
        backend.stop();
        QTRY_VERIFY(!backend.playing());
        QTRY_COMPARE(backend.position(), qint64(0));
        backend.togglePlayback();
        QTRY_VERIFY_WITH_TIMEOUT(backend.playing(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.dvdTitle(), selectedTitle, 10000);
        backend.open(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        QVERIFY(!backend.isDvd());
        QTRY_VERIFY_WITH_TIMEOUT(backend.hasVideo(), 15000);
        QVERIFY(errors.isEmpty());
        backend.setVideoSink(nullptr);
    }
    void dvdWindow()
    {
        const QString iso = qEnvironmentVariable("DVD_ISO_SAMPLE");
        if (iso.isEmpty()) QSKIP("Set DVD_ISO_SAMPLE");
        MainWindow window;
        window.show(); window.activateWindow();
        auto *backend = window.findChild<MediaBackend *>();
        auto *video = window.findChild<QVideoWidget *>();
        auto surfaces = video->findChildren<QWidget *>();
        QWidget *surface = surfaces.isEmpty() ? video : surfaces.first();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(iso)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &drop); QVERIFY(drop.isAccepted());
        QTRY_VERIFY_WITH_TIMEOUT(backend->hasVideo() && backend->seekable(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend->dvdTitles().isEmpty(), 10000);
        auto *menu = window.findChild<QMenu *>("dvdTitlesMenu");
        QVERIFY(menu && menu->isEnabled() && menu->menuAction()->isVisible());
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_VERIFY(!backend->playing());
        const auto position = backend->position();
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(backend->position() >= position + 8000, 10000);
        QTest::keyClick(&window, Qt::Key_F11); QTRY_VERIFY(window.isFullScreen());
        QTest::keyClick(&window, Qt::Key_Escape); QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(window.windowTitle().endsWith(" — Orange"));
    }
    void invalidDvdIso()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile iso(dir.filePath("invalid.iso"));
        QVERIFY(iso.open(QIODevice::WriteOnly)); iso.write("not a DVD"); iso.close();
        MediaBackend backend;
        QSignalSpy errors(&backend, &MediaBackend::failure);
        backend.open(iso.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 30000);
        QVERIFY(!backend.available());
        // A second open must safely retry after a library or media failure.
        backend.open(iso.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 30000);
    }
    void thumbnail_data()
    {
        QTest::addColumn<QString>("sample");
        QTest::newRow("MP4") << QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4");
        QTest::newRow("AVI") << QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.avi");
        QTest::newRow("WMV") << QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.wmv");
        if (!qEnvironmentVariableIsEmpty("WMV9_SAMPLE")) QTest::newRow("WMV9") << qEnvironmentVariable("WMV9_SAMPLE");
    }
    void thumbnail()
    {
        QFETCH(QString, sample);
        ThumbnailProvider provider;
        QSignalSpy ready(&provider, &ThumbnailProvider::ready);
        provider.setSource(sample);
        provider.request(0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        const auto image = qvariant_cast<QImage>(ready.first().at(1));
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= 200 && image.height() <= 112);
        ready.clear();
        provider.request(0);
        QCOMPARE(ready.size(), 1); // Cached response is immediate.
        const qint64 later = sample.contains("nokia_n90") ? 12000 : sample.contains("3colors") ? 2000 : 0;
        if (later > 0) {
            ready.clear();
            provider.request(later);
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
            const auto laterImage = qvariant_cast<QImage>(ready.first().at(1));
            QVERIFY(!laterImage.isNull());
            QVERIFY(laterImage != image);
        }
    }
    void hoverPreview_data()
    {
        QTest::addColumn<bool>("fullscreen");
        QTest::newRow("window") << false;
        QTest::newRow("fullscreen") << true;
    }
    void hoverPreview()
    {
        QFETCH(bool, fullscreen);
        MainWindow window;
        if (fullscreen) window.showFullScreen(); else window.show();
        window.activateWindow();
        const QString mp4 = QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4");
        window.openFile(mp4);
        auto *backend = window.findChild<MediaBackend *>();
        QTRY_VERIFY(backend->playing() && backend->seekable() && backend->hasVideo());
        backend->pause();
        const qint64 originalPosition = backend->position();
        auto *slider = window.findChild<QSlider *>("seekSlider");
        auto *popup = window.findChild<QFrame *>("seekPreviewPopup");
        auto *image = window.findChild<QLabel *>("seekPreviewImage");
        auto *time = window.findChild<QLabel *>("seekPreviewTime");
        QVERIFY(slider && popup && image && time);
        QTRY_VERIFY(window.isActiveWindow());
        QTest::mouseMove(&window, QPoint(30, 30));
        QTest::qWait(50);
        QTest::mouseMove(slider, QPoint(slider->width() / 2, slider->height() / 2));
        QTRY_VERIFY(popup->isVisible());
        QCOMPARE(time->text(), QString("00:01"));
        QTRY_VERIFY_WITH_TIMEOUT(!image->pixmap().isNull(), 10000);
        QCOMPARE(backend->position(), originalPosition);
        QCOMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        if (!qEnvironmentVariableIsEmpty("MEDIAPLAYER_PREVIEW_SCREENSHOT"))
            QVERIFY(popup->grab().save(qEnvironmentVariable("MEDIAPLAYER_PREVIEW_SCREENSHOT")));
        QTest::mouseMove(slider, QPoint(0, slider->height() / 2));
        QTRY_COMPARE(time->text(), QString("00:00"));
        QVERIFY(popup->frameGeometry().left() >= window.frameGeometry().left());
        QTest::mouseMove(slider, QPoint(slider->width() - 1, slider->height() / 2));
        QVERIFY(popup->frameGeometry().right() <= window.frameGeometry().right());
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() / 3, slider->height() / 2));
        QTest::mouseMove(slider, QPoint(slider->width() * 2 / 3, slider->height() / 2));
        QVERIFY(popup->isVisible());
        QCOMPARE(backend->position(), originalPosition);
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() * 2 / 3, slider->height() / 2));
        QTRY_VERIFY(backend->position() > 1800);
        QCOMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        QTest::mouseMove(&window, QPoint(20, 20));
        QTRY_VERIFY(!popup->isVisible());
        // Latest source wins even when a decode from the previous file is pending.
        ThumbnailProvider provider;
        QSignalSpy ready(&provider, &ThumbnailProvider::ready);
        provider.setSource(mp4);
        provider.request(2000);
        provider.setSource(QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.avi"));
        provider.request(0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        QCOMPARE(ready.first().at(0).toLongLong(), qint64(0));
        QVERIFY(!qvariant_cast<QImage>(ready.first().at(1)).isNull());
    }
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
        QCOMPARE(window.windowTitle(), QString("Orange"));
        for (int size : {16, 32, 48, 256})
            QVERIFY(!window.windowIcon().pixmap(size, size).isNull());
        window.show();
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        QTRY_COMPARE(window.windowTitle(), QString("3colors_with_sound_1s.mp4 — Orange"));
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
        auto *seek = window.findChild<QSlider *>("seekSlider");
        QEnterEvent audioHover(QPointF(20, 10), QPointF(20, 10), QPointF(seek->mapToGlobal(QPoint(20, 10))));
        QApplication::sendEvent(seek, &audioHover);
        QVERIFY(window.findChild<QFrame *>("seekPreviewPopup")->isVisible());
        QVERIFY(!window.findChild<QLabel *>("seekPreviewImage")->isVisible());
        QEvent audioLeave(QEvent::Leave);
        QApplication::sendEvent(seek, &audioLeave);
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
        // Keyboard shortcuts must work even after clicking the volume slider.
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        volume->setFocus();
        QTRY_VERIFY(volume->hasFocus());
        QTest::keyClick(volume, Qt::Key_Space);
        QTRY_COMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        backend->seek(12000);
        QTest::keyClick(volume, Qt::Key_Right);
        QTRY_COMPARE(backend->position(), qint64(22000));
        QTest::keyClick(volume, Qt::Key_Left);
        QTRY_COMPARE(backend->position(), qint64(12000));
        QCOMPARE(volume->value(), 37);
        QCOMPARE(backend->player()->playbackState(), QMediaPlayer::PausedState);
        QTest::keyClick(volume, Qt::Key_Space);
        QTRY_VERIFY(backend->playing());
        QTest::keyClick(volume, Qt::Key_Right);
        QTRY_VERIFY(backend->position() >= 22000 && backend->position() < 23000);
        QVERIFY(backend->playing());
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
