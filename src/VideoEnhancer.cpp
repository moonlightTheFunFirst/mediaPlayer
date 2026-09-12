#include "VideoEnhancer.h"
#include <QImage>
#include <QVector>
#include <algorithm>

QImage VideoEnhancer::sharpen(const QImage &image, const std::function<bool()> &cancelled)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    if (source.width() < 3 || source.height() < 3) return source;
    QImage result = source.copy();
    if (result.isNull()) return source;
    const int width = source.width();
    QVector<int> previous(width), current(width), next(width);
    auto luminance = [&](int y, QVector<int> &row) {
        const auto *pixels = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < width; ++x)
            row[x] = (77 * qRed(pixels[x]) + 150 * qGreen(pixels[x]) + 29 * qBlue(pixels[x])) >> 8;
    };
    luminance(0, previous); luminance(1, current);
    for (int y = 1; y < source.height() - 1; ++y) {
        if (cancelled && cancelled()) return {};
        luminance(y + 1, next);
        const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 1; x < width - 1; ++x) {
            // Luminance sharpening at 0.75 strength; cap overshoot to limit halos.
            const int delta = std::clamp(3 * (4 * current[x] - previous[x] - next[x] - current[x - 1] - current[x + 1]) / 16, -24, 24);
            output[x] = qRgba(std::clamp(qRed(input[x]) + delta, 0, 255),
                              std::clamp(qGreen(input[x]) + delta, 0, 255),
                              std::clamp(qBlue(input[x]) + delta, 0, 255), qAlpha(input[x]));
        }
        previous.swap(current); current.swap(next);
    }
    return result;
}
VideoEnhancer::VideoEnhancer(QObject *parent) : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}
VideoEnhancer::~VideoEnhancer() { cancel(); m_thread.quit(); m_thread.wait(); }
void VideoEnhancer::cancel() { ++m_epoch; m_pending = {}; }
void VideoEnhancer::submit(const QVideoFrame &frame) { m_pending = frame; startPending(); }
void VideoEnhancer::startPending()
{
    if (m_busy || !m_pending.isValid()) return;
    const auto original = m_pending;
    m_pending = {};
    m_busy = true;
    const auto epoch = m_epoch.load();
    QMetaObject::invokeMethod(m_worker, [this, original, epoch] {
        QVideoFrame filtered = original;
        const auto transfer = original.surfaceFormat().colorTransfer();
        // Preserve HDR through the native display path rather than clipping to 8-bit SDR.
        if (m_epoch == epoch && transfer != QVideoFrameFormat::ColorTransfer_ST2084
            && transfer != QVideoFrameFormat::ColorTransfer_STD_B67) {
            const auto image = sharpen(original.toImage(), [this, epoch] { return m_epoch != epoch; });
            if (!image.isNull()) {
                filtered = QVideoFrame(image);
                filtered.setStartTime(original.startTime());
                filtered.setEndTime(original.endTime());
                filtered.setRotation(original.rotation());
                filtered.setMirrored(original.mirrored());
            }
        }
        QMetaObject::invokeMethod(this, [this, original, filtered, epoch] {
            m_busy = false;
            if (m_epoch == epoch) emit ready(original, filtered);
            startPending();
        });
    });
}
