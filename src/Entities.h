#pragma once
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtGlobal>

enum class TerrainKind : quint8 {
    Grass=0,
    Mud=1,
    Water=2,
    Path=3,
    Fence=4,
    Barn=5,
    Crops=6,
    Gate=7,
    LongGrass=8,
    Pen=9,
    LeafPile=10
};

enum class EnemyKind : quint8 {
    Chicken=0,
    Goose=1,
    Bull=2,
    Crow=3,
    Fox=4,
    Pig=5,
    Cow=6,
    Sheep=7,
    Rooster=8
};

enum class FriendlyKind : quint8 {
    Sheep = 0,
    Goldfish = 1
};

enum class PickupKind : quint8 {
    Carrot=0, Milk=1, Egg=2, Key=3
};

struct Player {
    quint32 id = 0;
    QString name;
    QPointF pos{0,0};
    QPointF vel{0,0};
    quint8 facing = 0;
    bool alive = true;
    int boostTicks = 0;
    int shieldTicks = 0;
    float hp = 3.0f;
    float maxHp = 3.0f;
    int invulTicks = 0;
    int shootCooldown = 0;
    float distanceMoved = 0.0f;
    int eggs = 5;
    qint64 finishMs = -1;
    int keysHeld = 0;
    int deaths = 0;
    int stunTicks = 0;
};

struct Enemy {
    quint32 id = 0;
    EnemyKind kind = EnemyKind::Chicken;
    QPointF pos{0,0};
    QPointF vel{0,0};
    float hp = 1.0f;
    int knockbackTicks = 0;
    QVector<QPointF> path;
    int repathCooldown = 0;
    int chargeTicks = 0;
    quint32 targetPlayerId = 0;
    int behindTicks = 0;
    int homeRegionIdx = -1;
    int pathIndex = 0;
};

struct Friendly {
    quint32 id = 0;
    FriendlyKind kind = FriendlyKind::Sheep;
    QPointF pos{0,0};
    QPointF vel{0,0};
    QPointF wanderTarget{0,0};
    int wanderCooldown = 0;
};

struct Pickup {
    quint32 id = 0;
    PickupKind kind = PickupKind::Carrot;
    QPointF pos{0,0};
    bool taken = false;
};

struct Projectile {
    quint32 id = 0; // Added ID for network tracking
    QPointF pos{0,0};
    QPointF vel{0,0};
    quint32 ownerId = 0;
    int life = 60;
};

constexpr int TILE = 32;