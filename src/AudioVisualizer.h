#pragma once
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>

class AudioVisualizer : public QWidget
{
    Q_OBJECT
public:
    explicit AudioVisualizer(QWidget *parent = nullptr);
    void setRunning(bool running);
    void setLevels(float energy, float bass);
    void reset();
protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *event) override;
private:
    void advance();
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastInput = -1000, m_lastBeat = -1000;
    qint64 m_lastTick = 0;
    bool m_running = false;
    float m_target = 0, m_level = 0, m_bass = 0;
    QVector<float> m_rings;
};
