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
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QAction>
#include <QDataStream>
#include "../src/ThumbnailProvider.h"
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QContextMenuEvent>
#include "../src/AudioVisualizer.h"
#include "../src/FrameStepper.h"
#include "../src/VideoEnhancer.h"

class PlaybackTests : public QObject
{
    Q_OBJECT
    QString root = qEnvironmentVariable("QT_MEDIA_TEST_ROOT");
private slots:
    void sharpening()
    {
        QImage source(16, 16, QImage::Format_ARGB32); source.fill(qRgba(100, 100, 100, 200));
        QCOMPARE(VideoEnhancer::sharpen(source), source);
        source.setPixel(8, 8, qRgba(180, 180, 180, 200));
        const auto filtered = VideoEnhancer::sharpen(source);
        QCOMPARE(qRed(filtered.pixel(8, 8)), 204);
        QVERIFY(qRed(filtered.pixel(7, 8)) < 100);
        QCOMPARE(qAlpha(filtered.pixel(8, 8)), 200);
        QCOMPARE(source.pixel(8, 8), qRgba(180, 180, 180, 200));
        QCOMPARE(filtered.pixel(0, 0), source.pixel(0, 0));
        QVERIFY(VideoEnhancer::sharpen(source, [] { return true; }).isNull());
        VideoEnhancer enhancer; QSignalSpy frames(&enhancer, &VideoEnhancer::ready);
        QVideoFrame frame(source); frame.setStartTime(40000); frame.setEndTime(80000);
        frame.setRotation(QtVideo::Rotation::Clockwise90); frame.setMirrored(true);
        enhancer.submit(frame); enhancer.cancel();
        QTest::qWait(100); QCOMPARE(frames.size(), 0);
        enhancer.submit(frame);
        QTRY_COMPARE(frames.size(), 1);
        const auto result = qvariant_cast<QVideoFrame>(frames[0][1]);
        QCOMPARE(result.startTime(), qint64(40000)); QCOMPARE(result.endTime(), qint64(80000));
        QCOMPARE(result.rotation(), frame.rotation()); QCOMPARE(result.mirrored(), true);
    }
    void videoEnhancement_data() { frameFormats_data(); QTest::newRow("MP4") << QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"); }
    void videoEnhancement()
    {
        QFETCH(QString, path);
        MainWindow window; window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *action = window.findChild<QAction *>("videoEnhancementAction");
        auto *sink = window.findChild<QVideoWidget *>()->videoSink();
        QVERIFY(action && !action->isChecked());
        window.openFile(path); backend->setMuted(true);
        QTRY_VERIFY(backend->canStepFrame() && sink->videoFrame().isValid());
        backend->pause(); QTest::qWait(100);
        const auto original = sink->videoFrame().toImage().convertToFormat(QImage::Format_ARGB32);
        const auto position = backend->position();
        for (int i = 0; i < 2; ++i) {
            action->trigger(); QVERIFY(backend->videoEnhancement());
            QTRY_COMPARE(sink->videoFrame().toImage().convertToFormat(QImage::Format_ARGB32), VideoEnhancer::sharpen(original));
            if (i == 0) {
                const auto shown = sink->videoFrame().toImage().convertToFormat(QImage::Format_ARGB32);
                qint64 changedPixels = 0, totalDifference = 0; int maximum = 0;
                for (int y = 0; y < shown.height(); ++y) {
                    for (int x = 0; x < shown.width(); ++x) {
                        const auto a = original.pixel(x, y), b = shown.pixel(x, y);
                        const int difference = qMax(qAbs(qRed(a) - qRed(b)), qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b))));
                        changedPixels += difference != 0; totalDifference += difference;
                        maximum = qMax(maximum, difference);
                    }
                }
                qInfo() << "Displayed enhancement: changed pixels" << changedPixels
                        << "of" << shown.width() * shown.height() << "max channel delta" << maximum
                        << "mean changed-pixel delta" << (changedPixels ? double(totalDifference) / changedPixels : 0);
                if (path.endsWith("nokia_n90.wmv")) QVERIFY(changedPixels > 100);
            }
            QCOMPARE(backend->position(), position); QVERIFY(!backend->playing());
            action->trigger(); QVERIFY(!backend->videoEnhancement());
            QTRY_COMPARE(sink->videoFrame().toImage().convertToFormat(QImage::Format_ARGB32), original);
        }
        action->trigger(); QTest::qWait(100);
        const auto anchor = sink->videoFrame().startTime();
        backend->stepFrame(1); QTRY_VERIFY_WITH_TIMEOUT(!backend->frameStepBusy(), 16000);
        QVERIFY(sink->videoFrame().startTime() > anchor);
        const auto enhanced = sink->videoFrame().toImage().convertToFormat(QImage::Format_ARGB32);
        action->trigger();
        QCOMPARE(VideoEnhancer::sharpen(sink->videoFrame().toImage()), enhanced);
        action->trigger(); backend->seek(0); backend->togglePlayback();
        QTRY_VERIFY(backend->playing() && backend->position() > 100);
        backend->close(); QTest::qWait(200);
        QVERIFY(!sink->videoFrame().isValid());
        action->trigger(); QVERIFY(!sink->videoFrame().isValid());
    }
    void customSkip()
    {
        MainWindow window; window.show(); window.activateWindow();
        auto *backend = window.findChild<MediaBackend *>();
        auto *seconds = window.findChild<QDoubleSpinBox *>("skipSecondsSpinBox");
        auto *editor = seconds->findChild<QLineEdit *>();
        QCOMPARE(seconds->value(), 10.0);
        const auto path = QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4");
        window.openFile(path);
        QTRY_VERIFY(backend->seekable()); backend->pause(); backend->seek(0);
        seconds->setFocus(); seconds->selectAll();
        QTest::keyClicks(editor, "1.5");
        QCOMPARE(seconds->value(), 10.0); // Commit only on Enter/focus loss.
        QTest::keyClick(editor, Qt::Key_Left);
        QCOMPARE(backend->position(), qint64(0));
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(seconds->value(), 1.5);
        QVERIFY(!seconds->hasFocus());
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_COMPARE(backend->position(), qint64(1500));
        QVERIFY(!backend->playing());
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_COMPARE(backend->position(), qint64(0));
        auto *forward = window.findChild<QPushButton *>("seekForwardButton");
        QVERIFY(forward->toolTip().contains("1.5"));
        QTest::mouseClick(forward, Qt::LeftButton);
        QTRY_COMPARE(backend->position(), qint64(1500));
        backend->seek(0);
        QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
        QTRY_COMPARE(backend->position(), qint64(1000));
        seconds->setFocus(); seconds->selectAll(); QTest::keyClicks(editor, "0.1");
        window.setFocus(); QCOMPARE(seconds->value(), 0.1);
        backend->seek(0); QTest::keyClick(&window, Qt::Key_Right);
        QTRY_COMPARE(backend->position(), qint64(100));
        window.openFile(path); QCOMPARE(seconds->value(), 0.1);
        backend->close(); QCOMPARE(seconds->value(), 0.1);
        MainWindow fresh;
        QCOMPARE(fresh.findChild<QDoubleSpinBox *>("skipSecondsSpinBox")->value(), 10.0);
    }
    void variableFrameTimes()
    {
        // Four independently encoded GIF frames with unequal display durations.
        QTemporaryDir temp;
        QFile file(temp.filePath("variable.gif")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("47494638396101000100810000ff000000ff000000ffffff00"));
        const int delays[] = {4, 10, 7, 12};
        for (int i = 0; i < 4; ++i) {
            QByteArray control = QByteArray::fromHex("21f9040000000000"); control[4] = char(delays[i]);
            file.write(control);
            file.write(QByteArray::fromHex("2c0000000001000100000202"));
            file.putChar(char(0x44 + i * 8)); file.putChar(1); file.putChar(0);
        }
        file.putChar(';'); file.close();
        FrameStepper stepper; QSignalSpy frames(&stepper, &FrameStepper::ready);
        const qint64 times[] = {0, 40000, 140000, 210000};
        const QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
        for (int direction : {1, -1}) {
            for (int j = 0; j < 3; ++j) {
                const int index = direction > 0 ? j : 3 - j;
                frames.clear(); stepper.request(file.fileName(), times[index], direction);
                QVERIFY(frames.wait(16000));
                QVERIFY2(frames[0][2].toString().isEmpty(), qPrintable(frames[0][2].toString()));
                QCOMPARE(frames[0][1].toLongLong(), times[index + direction]);
                QCOMPARE(qvariant_cast<QImage>(frames[0][0]).pixelColor(0, 0), colors[index + direction]);
            }
        }
    }
    void frameFormats_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("AVI") << QDir(root).filePath("qmediaplayerformatsupport/testdata/containers/supported/container.avi");
        const auto wmv = qEnvironmentVariable("WMV9_SAMPLE");
        if (!wmv.isEmpty()) QTest::newRow("WMV9") << wmv;
    }
    void frameFormats()
    {
        QFETCH(QString, path);
        FrameStepper stepper; QSignalSpy frames(&stepper, &FrameStepper::ready);
        stepper.request(path, 400000, 1); QVERIFY(frames.wait(16000));
        QVERIFY2(frames[0][2].toString().isEmpty(), qPrintable(frames[0][2].toString()));
        const auto time = frames[0][1].toLongLong();
        if (path.endsWith(".avi")) QCOMPARE(time, qint64(600000)); // Fixture is 5 fps.
        else QVERIFY(time > 400000 && time < 500000);
        frames.clear(); stepper.request(path, time, -1); QVERIFY(frames.wait(16000));
        QVERIFY2(frames[0][2].toString().isEmpty(), qPrintable(frames[0][2].toString()));
        if (path.endsWith(".avi")) QCOMPARE(frames[0][1].toLongLong(), qint64(400000));
        else QVERIFY(frames[0][1].toLongLong() < time);
        QVideoSink sink;
        MediaBackend backend;
        backend.setVideoSink(&sink); backend.open(path); backend.setMuted(true);
        QTRY_VERIFY(backend.canStepFrame() && sink.videoFrame().isValid());
        backend.pause();
        const auto shown = sink.videoFrame().startTime();
        backend.stepFrame(1);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.frameStepBusy(), 16000);
        QCOMPARE(sink.videoFrame().startTime(), shown + (path.endsWith(".avi") ? 200000 : 40000));
        backend.stepFrame(-1);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.frameStepBusy(), 16000);
        QCOMPARE(sink.videoFrame().startTime(), shown);
        backend.setVideoSink(nullptr);
    }
    void frameDecode()
    {
        FrameStepper stepper;
        QSignalSpy frames(&stepper, &FrameStepper::ready);
        const auto path = QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4");
        auto step = [&](qint64 anchor, int direction) {
            frames.clear(); stepper.request(path, anchor, direction);
            return frames.wait(16000);
        };
        QVERIFY(step(0, 1));
        QVERIFY2(frames[0][2].toString().isEmpty(), qPrintable(frames[0][2].toString()));
        QCOMPARE(frames[0][1].toLongLong(), qint64(40000));
        QVERIFY(!qvariant_cast<QImage>(frames[0][0]).isNull());
        QVERIFY(step(40000, 1)); QCOMPARE(frames[0][1].toLongLong(), qint64(80000));
        QVERIFY(step(80000, -1)); QCOMPARE(frames[0][1].toLongLong(), qint64(40000));
        QVERIFY(step(40000, -1)); QCOMPARE(frames[0][1].toLongLong(), qint64(0));
        QVERIFY(step(0, -1)); QCOMPARE(frames[0][1].toLongLong(), qint64(0));
        QVERIFY(step(10000000, 1));
        QVERIFY2(frames[0][2].toString().isEmpty(), qPrintable(frames[0][2].toString()));
        const auto last = frames[0][1].toLongLong();
        QVERIFY(last >= 80000);
        QVERIFY(step(last, 1)); QCOMPARE(frames[0][1].toLongLong(), last);
    }
    void frameControls()
    {
        MainWindow window; window.show(); window.activateWindow();
        auto *backend = window.findChild<MediaBackend *>();
        auto *video = window.findChild<QVideoWidget *>();
        auto *modeLabel = window.findChild<QLabel *>("stepModeLabel");
        QVERIFY(modeLabel && !modeLabel->isVisible());
        QSignalSpy errors(backend, &MediaBackend::failure);
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        QTRY_VERIFY(backend->canStepFrame() && video->videoSink()->videoFrame().isValid());
        backend->pause();
        const auto anchor = video->videoSink()->videoFrame().startTime();
        auto *volume = window.findChild<QSlider *>("volumeSlider");
        volume->setFocus();
        QTest::keyClick(volume, Qt::Key_Right, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!backend->frameStepBusy(), 16000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors[0][1].toString()));
        QCOMPARE(video->videoSink()->videoFrame().startTime(), anchor + 40000);
        QVERIFY(modeLabel->isVisible()); QCOMPARE(modeLabel->text(), QStringLiteral("コマ送り"));
        QVERIFY(!backend->playing());
        const auto forward = video->videoSink()->videoFrame().startTime();
        QTest::qWait(200); QCOMPARE(video->videoSink()->videoFrame().startTime(), forward);
        QTest::keyClick(volume, Qt::Key_Left, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!backend->frameStepBusy(), 16000);
        QCOMPARE(video->videoSink()->videoFrame().startTime(), anchor);
        QTest::keyClick(volume, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(modeLabel->isVisible()); QCOMPARE(modeLabel->text(), QStringLiteral("1秒再生"));
        QVERIFY(!backend->playing());
        QTRY_VERIFY(backend->position() >= 1000);
        backend->togglePlayback(); QTRY_VERIFY(backend->playing());
        QVERIFY(!modeLabel->isVisible());
        backend->stepFrame(-1); backend->close();
        QTest::qWait(300);
        QVERIFY(backend->filePath().isEmpty()); QVERIFY(!video->videoSink()->videoFrame().isValid());
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/test.wav"));
        QTRY_VERIFY(backend->audioOnly()); QVERIFY(!backend->canStepFrame());
    }
    void visualizerColors()
    {
        AudioVisualizer visual;
        visual.resize(400, 400); visual.show();
        // A point inside the note's left stem, without audio-driven scaling.
        auto color = [&] { return visual.grab().toImage().pixelColor(180, 200); };
        const QColor orange = color();
        QVERIFY(orange.red() > 250 && orange.green() > 150 && orange.green() < 180);
        visual.setRunning(true);
        QColor previous = color();
        bool changed = false;
        for (int i = 0; i < 52; ++i) {
            QTest::qWait(250);
            const auto current = color();
            changed |= current != orange;
            // Cover at least one random target boundary; no sudden color jump.
            QVERIFY(qAbs(current.red() - previous.red()) < 40);
            QVERIFY(qAbs(current.green() - previous.green()) < 40);
            QVERIFY(qAbs(current.blue() - previous.blue()) < 40);
            previous = current;
            if (i == 20 && qEnvironmentVariableIsSet("ORANGE_COLOR_PREVIEW"))
                visual.grab().save(qEnvironmentVariable("ORANGE_COLOR_PREVIEW"));
        }
        QVERIFY(changed);
        visual.setRunning(false);
        const auto paused = color();
        QTest::qWait(1000);
        QCOMPARE(color(), paused);
        visual.setRunning(true);
        QTest::qWait(1800);
        QVERIFY(color() != paused);
        visual.reset();
        QCOMPARE(color(), orange);
    }
    void disableLoopWithoutSeek_data()
    {
        QTest::addColumn<QString>("relative");
        QTest::newRow("MP4") << "qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4";
        QTest::newRow("WAV") << "qmediaplayerbackend/testdata/test.wav";
    }
    void disableLoopWithoutSeek()
    {
        QFETCH(QString, relative);
        int wraps = 0;
        qint64 previous = 0;
        MainWindow window; window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *button = window.findChild<QPushButton *>("loopButton");
        QTest::mouseClick(button, Qt::LeftButton);
        window.openFile(QDir(root).filePath(relative));
        backend->setMuted(true);
        QTRY_VERIFY(backend->playing() && backend->duration() > 0);
        connect(backend->player(), &QMediaPlayer::positionChanged, &window, [&](qint64 position) {
            if (previous - position > backend->duration() / 2) ++wraps;
            previous = position;
        });
        QTRY_VERIFY_WITH_TIMEOUT(wraps >= 2, 12000);
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(!backend->looping());
        const int count = wraps;
        // Do not seek: that would flush Qt's prebuffered future loop iterations.
        QTest::qWait(int(backend->duration()) + 700);
        QCOMPARE(wraps, count);
        QVERIFY(!backend->playing());
        QCOMPARE(backend->player()->mediaStatus(), QMediaPlayer::EndOfMedia);
    }
    void loopPlayback_data()
    {
        QTest::addColumn<QString>("relative");
        QTest::newRow("MP4") << "qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4";
        QTest::newRow("WAV") << "qmediaplayerbackend/testdata/test.wav";
        QTest::newRow("AVI") << "qmediaplayerformatsupport/testdata/containers/supported/container.avi";
        if (!qEnvironmentVariableIsEmpty("WMV9_SAMPLE")) QTest::newRow("WMV9") << qEnvironmentVariable("WMV9_SAMPLE");
    }
    void loopPlayback()
    {
        QFETCH(QString, relative);
        MainWindow window; window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *button = window.findChild<QPushButton *>("loopButton");
        QVERIFY(button && !button->isChecked());
        const auto off = button->icon().pixmap(22, 22).toImage();
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(backend->looping() && button->isChecked());
        QVERIFY(button->icon().pixmap(22, 22).toImage() != off);
        window.openFile(QDir(root).filePath(relative));
        backend->setMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(backend->seekable() && backend->playing(), 10000);
        const auto duration = backend->duration();
        QVERIFY(duration >= 900);
        for (int repeat = 0; repeat < 2; ++repeat) {
            backend->seek(duration - 300);
            QTRY_VERIFY(backend->position() >= duration - 350);
            QTRY_VERIFY_WITH_TIMEOUT(backend->position() < duration / 2 && backend->playing(), 8000);
        }
        backend->pause(); QTRY_VERIFY(!backend->playing());
        QTest::qWait(800); QVERIFY(!backend->playing());
        backend->togglePlayback(); QTRY_VERIFY(backend->playing());
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(!backend->looping() && !button->isChecked());
        backend->seek(duration - 600);
        QTRY_COMPARE_WITH_TIMEOUT(backend->player()->mediaStatus(), QMediaPlayer::EndOfMedia, 8000);
        QVERIFY(!backend->playing());
        QTest::mouseClick(button, Qt::LeftButton);
        QTest::qWait(200); QVERIFY(!backend->playing());
        backend->togglePlayback(); QTRY_VERIFY(backend->playing());
        backend->stop(); QTest::qWait(800); QVERIFY(!backend->playing());
        backend->close(); QTest::qWait(200);
        QVERIFY(backend->looping()); QVERIFY(backend->filePath().isEmpty());
        window.openFile(QDir(root).filePath(relative));
        QTRY_VERIFY(backend->playing());
        QVERIFY(backend->looping());
    }
    void dvdLoop()
    {
        const auto iso = qEnvironmentVariable("DVD_ISO_SAMPLE");
        if (iso.isEmpty()) QSKIP("Set DVD_ISO_SAMPLE");
        MediaBackend backend;
        backend.setMuted(true); backend.setLooping(true); backend.open(iso);
        QTRY_VERIFY_WITH_TIMEOUT(backend.seekable() && !backend.dvdTitles().isEmpty(), 30000);
        const int selected = backend.dvdTitles().size() > 1 ? 1 : 0;
        backend.selectDvdTitle(selected);
        QTRY_COMPARE(backend.dvdTitle(), selected);
        QTest::qWait(500);
        const auto duration = backend.duration();
        for (int repeat = 0; repeat < 2; ++repeat) {
            backend.seek(duration - 2000);
            QTRY_VERIFY_WITH_TIMEOUT(backend.position() >= duration - 4000, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(backend.position() < 5000 && backend.playing(), 15000);
            QCOMPARE(backend.dvdTitle(), selected);
        }
        backend.pause(); QTRY_VERIFY(!backend.playing());
        QTest::qWait(500); QVERIFY(!backend.playing());
        backend.togglePlayback(); QTRY_VERIFY(backend.playing());
        backend.setLooping(false);
        backend.seek(duration - 2000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.position() >= duration - 4000, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.playing(), 15000);
        backend.setLooping(true);
        QTest::qWait(300); QVERIFY(!backend.playing());
        backend.togglePlayback(); QTRY_VERIFY(backend.playing());
        backend.stop(); QTRY_VERIFY(!backend.playing());
        QTest::qWait(500); QVERIFY(!backend.playing());
        backend.close(); QTest::qWait(300);
        QVERIFY(backend.filePath().isEmpty()); QVERIFY(!backend.playing());
    }
    void audioAnalysis()
    {
        QAudioFormat format;
        format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        auto tone = [&](double hz, float amplitude) {
            QAudioBuffer buffer(4800, format);
            float *samples = buffer.data<float>();
            for (int i = 0; i < 4800; ++i) {
                samples[i * 2] = amplitude * float(std::sin(2 * 3.141592653589793 * hz * i / 48000));
                samples[i * 2 + 1] = -samples[i * 2];
            }
            return buffer;
        };
        AudioLevels analyzer;
        const auto low = analyzer.process(tone(80, 0.5f));
        QVERIFY(low.energy > 0.34f && low.energy < 0.36f);
        QVERIFY(low.bass > 0.28f);
        analyzer.reset();
        const auto high = analyzer.process(tone(4000, 0.5f));
        QVERIFY(high.bass < low.bass * 0.1f);
        analyzer.reset();
        const auto quiet = analyzer.process(tone(80, 0.05f));
        QVERIFY(quiet.energy < low.energy * 0.11f);
        analyzer.reset();
        QCOMPARE(analyzer.process(tone(80, 0)).energy, 0.0f);
    }
    void audioVisualization()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath(QStringLiteral("音声 sample.wav"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        const quint32 bytes = 48000 * 8 * 2;
        stream.writeRawData("RIFF", 4); stream << quint32(36 + bytes);
        stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(1)
            << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4); stream << bytes;
        for (int i = 0; i < 48000 * 8; ++i) {
            const double amplitude = (i % 24000 < 12000) ? 0.65 : 0.08;
            stream << qint16(32767 * amplitude * std::sin(2 * 3.141592653589793 * 80 * i / 48000));
        }
        file.close();
        MainWindow window; window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *visual = window.findChild<AudioVisualizer *>();
        auto *video = window.findChild<QVideoWidget *>();
        QSignalSpy levels(backend, &MediaBackend::audioLevels);
        QVERIFY(!visual->isVisible());
        window.openFile(path);
        backend->setMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(backend->audioOnly() && visual->isVisible(), 10000);
        QTRY_VERIFY(!levels.isEmpty());
        QVERIFY(!video->isVisible());
        const auto first = visual->grab().toImage();
        QTest::qWait(400);
        QVERIFY(first != visual->grab().toImage());
        if (qEnvironmentVariableIsSet("ORANGE_VISUAL_PREVIEW")) window.grab().save(qEnvironmentVariable("ORANGE_VISUAL_PREVIEW"));
        backend->pause();
        QTRY_VERIFY(!backend->playing());
        QTest::qWait(1800);
        const auto paused = visual->grab().toImage();
        QTest::qWait(150);
        QCOMPARE(visual->grab().toImage(), paused);
        QVERIFY(visual->isVisible());
        backend->seek(2000); backend->togglePlayback();
        QTRY_VERIFY(backend->playing());
        QTRY_VERIFY(visual->grab().toImage() != paused);
        backend->stop();
        QVERIFY(visual->isVisible());
        backend->togglePlayback();
        QTRY_VERIFY(backend->playing());
        window.openFile(QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4"));
        QTRY_VERIFY(backend->hasVideo());
        QVERIFY(!visual->isVisible()); QVERIFY(video->isVisible());
        window.openFile(path);
        QTRY_VERIFY(visual->isVisible());
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(visual, &enter); QVERIFY(enter.isAccepted());
        QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(10, 10), visual->mapToGlobal(QPoint(10, 10)));
        QApplication::sendEvent(visual, &context);
        auto *menu = window.findChild<QMenu *>("mediaContextMenu");
        QTRY_VERIFY(menu->isVisible());
        auto *close = window.findChild<QAction *>("closeMediaAction");
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(close).center());
        QVERIFY(!visual->isVisible()); QVERIFY(!backend->audioOnly());
        QCOMPARE(window.windowTitle(), QStringLiteral("Orange"));
    }
    void closeMedia_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("mp4") << QDir(root).filePath("qmediaplayerbackend/testdata/3colors_with_sound_1s.mp4");
        const auto iso = qEnvironmentVariable("DVD_ISO_SAMPLE");
        if (!iso.isEmpty()) QTest::newRow("dvd") << iso;
    }
    void closeMedia()
    {
        QFETCH(QString, path);
        MainWindow window;
        window.show();
        auto *backend = window.findChild<MediaBackend *>();
        auto *video = window.findChild<QVideoWidget *>();
        auto *action = window.findChild<QAction *>("closeMediaAction");
        auto *menu = window.findChild<QMenu *>("mediaContextMenu");
        QVERIFY(action && menu);
        QVERIFY(!action->isEnabled());
        for (int attempt = 0; attempt < 2; ++attempt) {
            window.openFile(path);
            QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 30000);
            QVERIFY(action->isEnabled());
            QVERIFY(window.windowTitle().contains(QFileInfo(path).fileName()));
            if (attempt == 0) {
                const auto surfaces = video->findChildren<QWidget *>();
                QWidget *surface = surfaces.isEmpty() ? video : surfaces.first();
                QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(10, 10), surface->mapToGlobal(QPoint(10, 10)));
                QApplication::sendEvent(surface, &context);
                QTRY_VERIFY(menu->isVisible());
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
            } else {
                // The File menu uses the same action.
                action->trigger();
            }
            QVERIFY(backend->filePath().isEmpty());
            QVERIFY(!backend->available());
            QVERIFY(!backend->playing());
            QVERIFY(!backend->isDvd());
            QCOMPARE(backend->position(), qint64(0));
            QCOMPARE(backend->duration(), qint64(0));
            QCOMPARE(window.windowTitle(), QStringLiteral("Orange"));
            QVERIFY(!action->isEnabled());
            QVERIFY(!video->videoSink()->videoFrame().isValid());
            QTest::qWait(250); // Queued DVD frames must not revive a closed source.
            QVERIFY(!video->videoSink()->videoFrame().isValid());
            backend->togglePlayback();
            QVERIFY(!backend->playing());
        }
    }
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
