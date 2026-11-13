#pragma once
#include "Entities.h"
#include "Protocol.h"
#include "Biomes.h"
#include <QVector>
#include <QHash>
#include <QElapsedTimer>
#include <random>

class GameWorld {
public:
    GameWorld();
    void generate(quint32 seed);
    void initializeSession();
    void tick(float dt, const QHash<quint32, Proto::PlayerInput>& inputs);
    quint32 addPlayer(const QString& name);
    void removePlayer(quint32 id);
    Player* player(quint32 id);
    const QVector<Player>& players() const { return m_players; }
    const QVector<Enemy>& enemies() const { return m_enemies; }
    const QVector<Friendly>& friendlies() const { return m_friendlies; }
    const QVector<Pickup>& pickups() const { return m_pickups; }
    const QVector<Projectile>& projectiles() const { return m_projectiles; }

    int gridW() const { return m_gridW; }
    int gridH() const { return m_gridH; }
    TerrainKind tileAt(int x, int y) const;
    QPointF startPoint() const { return m_start; }
    QPointF goalPoint() const { return m_goal; }
    int keysRequired() const { return m_keysRequired; }
    qint64 elapsedMs() const { return m_elapsed.isValid() ? m_elapsed.elapsed() : 0; }
    void startTimer() { m_elapsed.start(); }

    void snapshot(QVector<Proto::PlayerSnap>& ps,
                  QVector<Proto::EnemySnap>& es,
                  QVector<Proto::PickupSnap>& pk,
                  QVector<Proto::FriendlySnap>& fs,
                  QVector<Proto::ProjectileSnap>& prs) const;

    void applySnapshot(const QVector<Proto::PlayerSnap>& ps,
                       const QVector<Proto::EnemySnap>& es,
                       const QVector<Proto::PickupSnap>& pk,
                       const QVector<Proto::FriendlySnap>& fs,
                       const QVector<Proto::ProjectileSnap>& prs);
    void setTileAt(int x, int y, TerrainKind t);
    int getRegionIndex(QPointF pos) const;
    void moveEntity(QPointF& pos, QPointF& vel, float dt, bool flying, Player* p = nullptr);
    bool blocksMovement(int gx, int gy, bool flying, Player* p = nullptr) const;
    bool blocksMovementExt(int gx, int gy, bool flying, bool canSwim, Player* p = nullptr) const;
    void moveEntityExt(QPointF& pos, QPointF& vel, float dt, bool flying, bool canSwim, Player* p = nullptr);

private:
    QVector<TerrainKind> m_tiles;
    int m_gridW = 300;
    int m_gridH = 30;
    QPointF m_start{0,0}, m_goal{0,0};
    int m_keysRequired = 0;
    QVector<Player> m_players;
    QVector<Enemy> m_enemies;
    QVector<Friendly> m_friendlies;
    QVector<Pickup> m_pickups;
    QVector<Projectile> m_projectiles;
    quint32 m_nextId = 1;
    QElapsedTimer m_elapsed;
    std::mt19937 m_rng;

    void spawnEnemies();
    void spawnFriendlies();
    void spawnPickups();
    void respawnEnemyInFront(Enemy& e, const Player& p, const QRect& biomeBounds);
    void updateEnemy(Enemy& e, float dt);
    void resolvePlayerCollisions();
    void resolveProjectiles(float dt);
    void resolvePickups();
    void spawnDynamicPickup(QPointF playerPos);
    bool isValidSpawnPos(int tx, int ty) const;
    QVector<BiomeRegion> m_regions;
    void spawnEnemiesForRegions();
    void spawnKeyInBiome(int regionIdx);
    bool hasKeyInBiome(int regionIdx) const;
    void spawnCrows();
    void spawnGoldfish();
};