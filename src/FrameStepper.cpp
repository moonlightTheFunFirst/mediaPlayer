#include "FrameStepper.h"
#include <QLibrary>
#include <QElapsedTimer>
#include <QTransform>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/display.h>
}

namespace {
#define FORMAT_API(X) X(avformat_version) X(avformat_alloc_context) X(avformat_open_input) X(avformat_find_stream_info) X(av_find_best_stream) X(av_seek_frame) X(av_read_frame) X(avformat_close_input)
#define CODEC_API(X) X(avcodec_version) X(avcodec_alloc_context3) X(avcodec_parameters_to_context) X(avcodec_open2) X(avcodec_send_packet) X(avcodec_receive_frame) X(avcodec_flush_buffers) X(avcodec_free_context) X(av_packet_alloc) X(av_packet_free) X(av_packet_unref) X(av_packet_side_data_get)
#define UTIL_API(X) X(avutil_version) X(av_frame_alloc) X(av_frame_free) X(av_frame_unref) X(av_frame_ref) X(av_display_rotation_get) X(av_dict_set) X(av_dict_free)
#define SCALE_API(X) X(swscale_version) X(sws_getContext) X(sws_scale) X(sws_freeContext) X(sws_getCoefficients) X(sws_setColorspaceDetails)
struct Decoder {
    QLibrary formatLib{"avformat-61"}, codecLib{"avcodec-61"}, utilLib{"avutil-59"}, scaleLib{"swscale-8"};
#define DECLARE(name) decltype(&name) name = nullptr;
    FORMAT_API(DECLARE) CODEC_API(DECLARE) UTIL_API(DECLARE) SCALE_API(DECLARE)
#undef DECLARE
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr, *selected = nullptr;
    SwsContext *scale = nullptr;
    bool load() {
#define RESOLVE(name) name = reinterpret_cast<decltype(name)>(library.resolve(#name)); if (!name) return false;
        for (auto *lib : {&utilLib, &codecLib, &formatLib, &scaleLib}) {
            lib->setLoadHints(QLibrary::PreventUnloadHint);
            if (!lib->load()) return false;
        }
        { auto &library = formatLib; FORMAT_API(RESOLVE) }
        { auto &library = codecLib; CODEC_API(RESOLVE) }
        { auto &library = utilLib; UTIL_API(RESOLVE) }
        { auto &library = scaleLib; SCALE_API(RESOLVE) }
#undef RESOLVE
        return (avformat_version() >> 16) == 61 && (avcodec_version() >> 16) == 61
            && (avutil_version() >> 16) == 59 && (swscale_version() >> 16) == 8;
    }
    ~Decoder() {
        if (scale) sws_freeContext(scale);
        if (selected) av_frame_free(&selected);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
    }
};
struct Result { QImage image; qint64 time = 0; QString error; };
Result decode(const QString &path, qint64 anchor, int direction, std::atomic<quint64> &serial, quint64 id)
{
    Decoder d;
    auto fail = [](const QString &message) { return Result{{}, 0, message}; };
    if (!d.load()) return fail(QObject::tr("コマ送り用FFmpegライブラリを読み込めません。"));
    struct Interrupt {
        std::atomic<quint64> &serial; quint64 id; QElapsedTimer timer;
        bool cancelled() const { return serial.load() != id || timer.elapsed() > 15000; }
    } interrupt{serial, id, {}};
    interrupt.timer.start();
    d.format = d.avformat_alloc_context();
    if (!d.format) return fail(QObject::tr("動画を開けません。"));
    d.format->interrupt_callback = {[](void *p) { return static_cast<Interrupt *>(p)->cancelled() ? 1 : 0; }, &interrupt};
    AVDictionary *options = nullptr;
    d.av_dict_set(&options, "protocol_whitelist", "file", 0);
    const int opened = d.avformat_open_input(&d.format, path.toUtf8().constData(), nullptr, &options);
    d.av_dict_free(&options);
    if (opened < 0 || d.avformat_find_stream_info(d.format, nullptr) < 0) return fail(QObject::tr("コマ送り用に動画を読み取れません。"));
    const AVCodec *decoder = nullptr;
    const int index = d.av_find_best_stream(d.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (index < 0) return fail(QObject::tr("映像トラックがありません。"));
    const auto *stream = d.format->streams[index];
    const qint64 origin = d.format->start_time == AV_NOPTS_VALUE ? 0 : d.format->start_time;
    d.codec = d.avcodec_alloc_context3(decoder);
    if (!d.codec || d.avcodec_parameters_to_context(d.codec, stream->codecpar) < 0) return fail(QObject::tr("映像デコーダーを作成できません。"));
    d.codec->thread_count = 2;
    if (d.avcodec_open2(d.codec, decoder, nullptr) < 0) return fail(QObject::tr("この映像のコマ送りに対応していません。"));
    d.packet = d.av_packet_alloc(); d.frame = d.av_frame_alloc(); d.selected = d.av_frame_alloc();
    if (!d.packet || !d.frame || !d.selected) return fail(QObject::tr("コマ送りのメモリを確保できません。"));
    const auto toTicks = [&](qint64 us) { return qint64(std::floor(double(us + origin) * stream->time_base.den / (1000000.0 * stream->time_base.num))); };
    // Seek before the anchor for reverse stepping, then decode in presentation order.
    const qint64 seekUs = direction < 0 ? qMax(qint64(0), anchor - 1000000) : qMax(qint64(0), anchor);
    if (d.av_seek_frame(d.format, index, toTicks(seekUs), AVSEEK_FLAG_BACKWARD) < 0) {
        if (d.av_seek_frame(d.format, index, toTicks(0), AVSEEK_FLAG_BACKWARD) < 0) return fail(QObject::tr("この動画はコマ送り用にシークできません。"));
    }
    d.avcodec_flush_buffers(d.codec);
    bool draining = false, found = false;
    qint64 selectedTime = -1;
    while (!interrupt.cancelled()) {
        const int received = d.avcodec_receive_frame(d.codec, d.frame);
        if (received == 0) {
            const auto pts = d.frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) return fail(QObject::tr("フレーム時刻がないため正確にコマ送りできません。"));
            const qint64 time = qRound64(double(pts) * stream->time_base.num * 1000000.0 / stream->time_base.den) - origin;
            if (time >= 0) {
                const bool take = direction > 0 ? time > anchor : time < anchor;
                if (direction > 0 || take || selectedTime < 0) {
                    d.av_frame_unref(d.selected);
                    if (d.av_frame_ref(d.selected, d.frame) < 0) return fail(QObject::tr("フレームを保持できません。"));
                    selectedTime = time;
                }
                if ((direction > 0 && time > anchor) || (direction < 0 && time >= anchor)) { found = true; break; }
            }
            d.av_frame_unref(d.frame);
        } else if (received == AVERROR_EOF) { found = selectedTime >= 0; break; }
        else if (received == AVERROR(EAGAIN) && !draining) {
            int read;
            do { d.av_packet_unref(d.packet); read = d.av_read_frame(d.format, d.packet); }
            while (read >= 0 && d.packet->stream_index != index && !interrupt.cancelled());
            if (read < 0 && read != AVERROR_EOF) return fail(QObject::tr("フレームの読み取りに失敗しました。"));
            draining = read == AVERROR_EOF;
            if (d.avcodec_send_packet(d.codec, draining ? nullptr : d.packet) < 0) return fail(QObject::tr("フレームをデコードできません。"));
        } else return fail(QObject::tr("フレームをデコードできません。"));
    }
    if (!found || selectedTime < 0) return fail(QObject::tr("コマ送りが時間内に完了しませんでした。"));
    auto *f = d.selected;
    if (f->width <= 0 || f->height <= 0 || f->width > 8192 || f->height > 8192) return fail(QObject::tr("コマ送りで扱える映像サイズを超えています。"));
    QImage image(f->width, f->height, QImage::Format_ARGB32);
    if (image.isNull()) return fail(QObject::tr("画像を確保できません。"));
    d.scale = d.sws_getContext(f->width, f->height, AVPixelFormat(f->format), f->width, f->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!d.scale) return fail(QObject::tr("フレームを表示用に変換できません。"));
    const int *coefficients = d.sws_getCoefficients(f->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT);
    d.sws_setColorspaceDetails(d.scale, coefficients, f->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0, 1 << 16, 1 << 16);
    uint8_t *planes[] = {image.bits(), nullptr, nullptr, nullptr};
    int strides[] = {int(image.bytesPerLine()), 0, 0, 0};
    d.sws_scale(d.scale, f->data, f->linesize, 0, f->height, planes, strides);
    const AVRational sar = f->sample_aspect_ratio.num > 0 ? f->sample_aspect_ratio : stream->sample_aspect_ratio;
    if (sar.num > 0 && sar.den > 0 && sar.num != sar.den) {
        const int width = qRound(double(image.width()) * sar.num / sar.den);
        if (width > 0 && width <= 8192) image = image.scaled(width, image.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    const auto *rotation = d.av_packet_side_data_get(stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (rotation && rotation->size >= 9 * sizeof(int32_t)) {
        const double angle = -d.av_display_rotation_get(reinterpret_cast<const int32_t *>(rotation->data));
        if (std::isfinite(angle)) image = image.transformed(QTransform().rotate(angle));
    }
    return {image, selectedTime, {}};
}
}
FrameStepper::FrameStepper(QObject *parent) : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}
FrameStepper::~FrameStepper() { cancel(); m_thread.quit(); m_thread.wait(); }
void FrameStepper::cancel() { ++m_serial; }
void FrameStepper::request(const QString &path, qint64 anchorUs, int direction)
{
    const auto id = ++m_serial;
    QMetaObject::invokeMethod(m_worker, [this, path, anchorUs, direction, id] {
        if (m_serial != id) return;
        const auto result = decode(path, anchorUs, direction, m_serial, id);
        QMetaObject::invokeMethod(this, [this, result, id] {
            if (m_serial == id) emit ready(result.image, result.time, result.error);
        });
    });
}
