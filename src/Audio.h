#pragma once
#include <QObject>
#include <QSoundEffect>
#include <QTemporaryFile>
#include <QHash>

// Synthesizes simple WAV blips at runtime so the game ships with no audio assets.
class Audio : public QObject {
    Q_OBJECT
public:
    explicit Audio(QObject* parent = nullptr);
    enum Sfx { Cluck, Moo, Coin, Shoot, Splash, Win, Countdown, Caw };
    void play(Sfx s, float volume = 0.6f);
private:
    QHash<int, QSoundEffect*> m_effects;
    QHash<int, QTemporaryFile*> m_files;
    void synth(Sfx s, double freq, double durSec, bool noisy);
};
