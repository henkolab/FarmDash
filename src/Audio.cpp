#include "Audio.h"
#include <QFile>
#include <QUrl>
#include <QtEndian>
#include <QRandomGenerator>
#include <cmath>

static QByteArray makeWav(double freq, double durSec, bool noisy) {
    const int sampleRate = 22050;
    const int n = int(durSec * sampleRate);
    QByteArray data;
    data.reserve(n * 2 + 44);
    auto u32 = [&](quint32 v){ char b[4]; qToLittleEndian(v, b); data.append(b, 4); };
    auto u16 = [&](quint16 v){ char b[2]; qToLittleEndian(v, b); data.append(b, 2); };
    data.append("RIFF"); u32(36 + n*2);
    data.append("WAVE"); data.append("fmt "); u32(16); u16(1); u16(1);
    u32(sampleRate); u32(sampleRate*2); u16(2); u16(16);
    data.append("data"); u32(n*2);
    for (int i=0; i<n; ++i) {
        double t = double(i)/sampleRate;
        double env = std::min(1.0, t/0.005) * std::exp(-3.0*t/durSec);
        double s = std::sin(2*M_PI*freq*t);
        if (noisy) s = 0.5*s + 0.5*(QRandomGenerator::global()->bounded(2000)/1000.0 - 1.0);
        qint16 v = qint16(s * env * 22000.0);
        char b[2]; qToLittleEndian(v, b); data.append(b, 2);
    }
    return data;
}

Audio::Audio(QObject* p) : QObject(p) {
    synth(Cluck,    900,  0.10, true);
    synth(Moo,      180,  0.50, false);
    synth(Coin,    1400,  0.18, false);
    synth(Shoot,    600,  0.08, true);
    synth(Splash,   300,  0.25, true);
    synth(Win,      660,  0.60, false);
    synth(Countdown,800,  0.12, false);
    synth(Caw,     1200,  0.20, true);
}

void Audio::synth(Sfx s, double freq, double dur, bool noisy) {
    auto* tf = new QTemporaryFile(this);
    tf->setAutoRemove(true);
    if (!tf->open()) return;
    tf->write(makeWav(freq, dur, noisy));
    tf->flush();
    auto* fx = new QSoundEffect(this);
    fx->setSource(QUrl::fromLocalFile(tf->fileName()));
    m_files.insert(s, tf);
    m_effects.insert(s, fx);
}

void Audio::play(Sfx s, float volume) {
    if (auto* fx = m_effects.value(s, nullptr)) {
        fx->setVolume(volume);
        fx->play();
    }
}
