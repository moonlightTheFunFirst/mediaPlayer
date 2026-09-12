#pragma once
#include <QAudioBuffer>
#include <QVector>
#include <cmath>
#include <algorithm>

// Per-channel filtering preserves bass even for opposite-phase stereo samples.
class AudioLevels
{
public:
    struct Levels { float energy = 0; float bass = 0; };
    void reset() { m_low.clear(); m_rate = 0; }
    Levels process(const QAudioBuffer &buffer) {
        if (!buffer.isValid()) return {};
        const auto format = buffer.format();
        const int channels = format.channelCount();
        if (!format.isValid() || buffer.frameCount() == 0) return {};
        if (m_rate != format.sampleRate() || m_low.size() != channels) {
            m_rate = format.sampleRate(); m_low.fill(0, channels);
        }
        const double alpha = 1.0 - std::exp(-2.0 * 3.141592653589793 * 180.0 / m_rate);
        double energy = 0, bass = 0;
        const char *data = buffer.constData<char>();
        for (int frame = 0; frame < buffer.frameCount(); ++frame) {
            for (int ch = 0; ch < channels; ++ch) {
                double sample = format.normalizedSampleValue(data);
                data += format.bytesPerSample();
                if (!std::isfinite(sample)) sample = 0;
                sample = std::clamp(sample, -1.0, 1.0);
                m_low[ch] += alpha * (sample - m_low[ch]);
                energy += sample * sample;
                bass += m_low[ch] * m_low[ch];
            }
        }
        const double count = double(buffer.frameCount()) * channels;
        return {float(std::sqrt(energy / count)), float(std::sqrt(bass / count))};
    }
private:
    QVector<double> m_low;
    int m_rate = 0;
};
