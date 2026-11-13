#pragma once
#include <QByteArray>
#include <QDataStream>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace Proto {
enum MsgType : quint8 {
    Hello = 1, Welcome, StartCountdown, InputUpdate,
    StateSnapshot, Finished, Leaderboard, Chat
};

struct PlayerInput {
    qint8 dx = 0, dy = 0;
    bool jump = false;
    bool shoot = false;
};

// Removed name from snapshot; use ID to look up names received during Hello/Welcome
struct PlayerSnap {
    quint32 id;
    float x, y;
    quint8 facing;
    quint8 alive;
    float hp;
};

struct EnemySnap      { quint32 id; quint8 kind; float x, y; float hp; };
struct FriendlySnap { quint32 id; quint8 kind; float x, y; };
struct PickupSnap     { quint32 id; quint8 kind; float x, y; bool taken; };
struct ProjectileSnap { quint32 id; float x, y; };

QByteArray encodeSnapshot(const QVector<PlayerSnap>& players,
                          const QVector<EnemySnap>& enemies,
                          const QVector<PickupSnap>& pickups,
                          const QVector<FriendlySnap>& friendlies,
                          const QVector<ProjectileSnap>& projectiles,
                          qint64 elapsedMs);

// Rest of your functions...
QByteArray encodeHello(const QString& name);
QByteArray encodeWelcome(quint32 playerId, quint32 courseSeed);
QByteArray encodeStartCountdown(qint32 msUntilStart);
QByteArray encodeInput(const PlayerInput& in);
QByteArray encodeFinished(qint64 timeMs);
QByteArray encodeChat(const QString& s);
QByteArray encodeLeaderboard(const QVector<QPair<QString,qint64>>& rows);
QByteArray frame(const QByteArray& payload);
}