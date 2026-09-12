#include "AudioVisualizer.h"
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

AudioVisualizer::AudioVisualizer(QWidget *parent) : QWidget(parent)
{
    setObjectName("audioVisualizer");
    setAccessibleName(tr("音声ビジュアライザー"));
    setMinimumSize(320, 180);
    m_clock.start();
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &AudioVisualizer::advance);
}
void AudioVisualizer::reset()
{
    m_timer.stop();
    m_running = false;
    m_target = m_level = m_bass = 0;
    m_lastInput = m_lastBeat = -1000;
    m_rings.clear();
    update();
}
void AudioVisualizer::hideEvent(QHideEvent *event)
{
    reset(); QWidget::hideEvent(event);
}
void AudioVisualizer::setRunning(bool running)
{
    m_running = running;
    if (!running) { m_target = 0; m_bass = 0; }
}
void AudioVisualizer::setLevels(float energy, float bass)
{
    if (!m_running || !isVisible()) return;
    m_lastInput = m_clock.elapsed();
    m_target = std::clamp(energy * 2.8f, 0.0f, 1.0f);
    if (bass > 0.055f && bass > m_bass * 1.3f + 0.015f && m_lastInput - m_lastBeat > 250) {
        if (m_rings.size() < 5) m_rings.append(0);
        m_lastBeat = m_lastInput;
    }
    m_bass = bass;
    if (!m_timer.isActive()) { m_lastTick = m_clock.elapsed(); m_timer.start(); }
}
void AudioVisualizer::advance()
{
    const qint64 now = m_clock.elapsed();
    const float elapsed = float(now - m_lastTick) / 33.0f;
    m_lastTick = now;
    if (!m_running || m_clock.elapsed() - m_lastInput > 400) m_target = 0;
    m_level += (m_target - m_level) * (1 - std::pow(m_target > m_level ? 0.65f : 0.88f, elapsed));
    for (auto &ring : m_rings) ring += 0.022f * elapsed;
    m_rings.removeIf([](float ring) { return ring >= 1; });
    if (m_target == 0 && m_level < 0.001f && m_rings.isEmpty()) { m_level = 0; m_timer.stop(); }
    update();
}
void AudioVisualizer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(14, 12, 11));
    const QPointF center(width() / 2.0, height() / 2.0);
    const qreal size = qMin(width(), height());
    const qreal radius = size * (0.35 + 0.16 * m_level);
    QRadialGradient glow(center, radius);
    glow.setColorAt(0, QColor(255, 125, 30, int(22 + 95 * m_level)));
    glow.setColorAt(0.5, QColor(210, 72, 12, int(10 + 32 * m_level)));
    glow.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), glow);
    p.setBrush(Qt::NoBrush);
    for (const float ring : m_rings) {
        p.setPen(QPen(QColor(255, 155, 60, int(90 * (1 - ring))), 1.5));
        const qreal r = size * (0.18 + ring * 0.45);
        p.drawEllipse(center, r, r);
    }
    p.translate(center);
    const qreal scale = std::clamp(size * 0.27, 64.0, 150.0) / 100.0 * (1 + 0.08 * m_level);
    p.scale(scale, scale);
    // Draw a double-beamed note (♬) as paths so it never depends on emoji fonts.
    QPainterPath note;
    note.addEllipse(QRectF(-43, 19, 28, 18));
    note.addEllipse(QRectF(17, 7, 28, 18));
    note.addRect(QRectF(-22, -34, 7, 62));
    note.addRect(QRectF(38, -46, 7, 62));
    note.moveTo(-22, -34); note.lineTo(45, -47); note.lineTo(45, -38); note.lineTo(-22, -25); note.closeSubpath();
    note.moveTo(-22, -18); note.lineTo(45, -31); note.lineTo(45, -22); note.lineTo(-22, -9); note.closeSubpath();
    note.setFillRule(Qt::WindingFill);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 166, 63));
    p.drawPath(note);
}
