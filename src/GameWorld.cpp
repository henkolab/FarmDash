#include "GameWorld.h"
#include "Pathfinding.h"
#include <QtMath>
#include <algorithm>
#include <QDebug>

GameWorld::GameWorld() {}

TerrainKind GameWorld::tileAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_gridW || y >= m_gridH) return TerrainKind::Fence;
    return m_tiles[y * m_gridW + x];
}

void GameWorld::setTileAt(int x, int y, TerrainKind t) {
    if (x >= 0 && x < m_gridW && y >= 0 && y < m_gridH)
        m_tiles[y * m_gridW + x] = t;
}

void GameWorld::generate(quint32 seed) {
    m_tiles.resize(m_gridW * m_gridH);
    m_rng.seed(seed);
    m_tiles.fill(TerrainKind::Grass, m_gridW * m_gridH);
    m_regions.clear();

    int chunk = m_gridW / 6;
    m_regions = {
        { QRect(0, 0, chunk, m_gridH),                            BiomeType::Grassy },
        { QRect(chunk, 0, chunk, m_gridH),                        BiomeType::Muddy },
        { QRect(chunk * 2, 0, chunk, m_gridH),                    BiomeType::Watery },
        { QRect(chunk * 3, 0, chunk, m_gridH),                    BiomeType::Crops },
        { QRect(chunk * 4, 0, chunk, m_gridH),                    BiomeType::Coop },
        { QRect(chunk * 5, 0, m_gridW - (chunk * 5), m_gridH),   BiomeType::Field }
    };

    for (const auto& reg : m_regions) {
        BiomeManager::fillTiles(*this, reg.area, BiomeManager::getPrimaryTile(reg.type));
    }

    int sx = 2, sy = m_gridH / 2;
    int gx = m_gridW - 6, gy = m_gridH / 2;
    int cx = sx, cy = sy;
    while (cx < gx) {
        setTileAt(cx, cy, TerrainKind::Path);
        if (m_rng() % 10 < 8) cx++;
        else if (cy < gy + 3 && cy < m_gridH - 4) cy++;
        else if (cy > gy - 3 && cy > 3) cy--;
        else cx++;
    }

    for (const auto& reg : m_regions) {
        BiomeManager::generateBiome(*this, reg, m_rng);
    }

    for (int i = 0; i < (int)m_regions.size() - 1; ++i) {
        int fx = m_regions[i].area.right();
        for (int y = 0; y < m_gridH; ++y) {
            if (tileAt(fx, y) == TerrainKind::Path) {
                setTileAt(fx, y, TerrainKind::Gate);
            } else {
                setTileAt(fx, y, TerrainKind::Fence);
            }
        }
    }

    for (int yy = -2; yy <= 2; ++yy) {
        for (int xx = -2; xx <= 2; ++xx) {
            if (std::abs(xx) == 2 || std::abs(yy) == 2)
                setTileAt(gx + xx, gy + yy, TerrainKind::Path);
        }
    }
    for (int yy = -1; yy <= 1; ++yy) {
        for (int xx = -1; xx <= 1; ++xx) {
            if (xx == 0 && yy == 1) setTileAt(gx + xx, gy + yy, TerrainKind::Path);
            else setTileAt(gx + xx, gy + yy, TerrainKind::Barn);
        }
    }

    for (int yy = 1; yy <= 3; ++yy) {
        for (int xx = -1; xx <= 1; ++xx) {
            int tx = gx + xx, ty = gy + yy;
            if (tileAt(tx, ty) == TerrainKind::Barn) continue;
            if (xx == 0) setTileAt(tx, ty, TerrainKind::Path);
            else if (tileAt(tx, ty) != TerrainKind::Gate) setTileAt(tx, ty, TerrainKind::Grass);
        }
    }

    for (int x = 0; x < m_gridW; ++x) { setTileAt(x, 0, TerrainKind::Fence); setTileAt(x, m_gridH - 1, TerrainKind::Fence); }
    for (int y = 0; y < m_gridH; ++y) { setTileAt(0, y, TerrainKind::Fence); setTileAt(m_gridW - 1, y, TerrainKind::Fence); }

    m_start = QPointF(sx * TILE + TILE / 2.0, sy * TILE + TILE / 2.0);
    m_goal = QPointF(gx * TILE + TILE / 2.0, (gy + 1) * TILE + TILE / 2.0);
    m_enemies.clear();
    m_friendlies.clear();
    spawnEnemiesForRegions();
    spawnCrows();
    spawnFriendlies();
    spawnGoldfish();
    m_keysRequired = 1;

    for (auto& p : m_players) {
        p.pos = m_start; p.keysHeld = 0; p.hp = 3.0f; p.deaths = 0; p.finishMs = -1;
    }
}

void GameWorld::spawnCrows() {
    // Crows are global enemies - they roam the entire map
    for (int i = 0; i < 24; ++i) {
        Enemy e;
        e.id = m_nextId++;
        e.kind = EnemyKind::Crow;
        e.hp = 3.0f;
        e.homeRegionIdx = -1;

        for (int tries = 0; tries < 40; ++tries) {
            int tx = 3 + (m_rng() % (m_gridW - 6));
            int ty = 3 + (m_rng() % (m_gridH - 6));
            if (tileAt(tx, ty) != TerrainKind::Barn) {
                e.pos = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                m_enemies.push_back(e);
                break;
            }
        }
    }
}

void GameWorld::spawnGoldfish() {
    // Find the Watery biome region
    int waterRegion = -1;
    for (int i = 0; i < m_regions.size(); ++i) {
        if (m_regions[i].type == BiomeType::Watery) {
            waterRegion = i;
            break;
        }
    }
    if (waterRegion < 0) return;

    const auto& area = m_regions[waterRegion].area;

    int spawned = 0;
    for (int attempts = 0; attempts < 200 && spawned < 8; ++attempts) {
        int tx = area.left() + (m_rng() % area.width());
        int ty = area.top() + (m_rng() % area.height());

        if (tileAt(tx, ty) == TerrainKind::Water) {
            Friendly f;
            f.id = m_nextId++;
            f.kind = FriendlyKind::Goldfish;
            f.pos = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
            f.wanderTarget = f.pos;
            f.wanderCooldown = m_rng() % 120;
            m_friendlies.push_back(f);
            spawned++;
        }
    }
}

void GameWorld::spawnEnemiesForRegions() {
    for (int r = 0; r < m_regions.size(); ++r) {
        const auto& region = m_regions[r];
        QVector<EnemyKind> allowed = BiomeManager::getEnemiesForBiome(region.type);
        if (allowed.isEmpty()) continue;

        for (int i = 0; i < 8; ++i) {
            Enemy e;
            e.id = m_nextId++;
            e.kind = allowed[m_rng() % allowed.size()];
            e.hp = 5.0f;
            e.homeRegionIdx = r; // <--- THIS MUST BE SET HERE

            for (int tries = 0; tries < 30; ++tries) {
                int tx = region.area.left() + (m_rng() % region.area.width());
                int ty = region.area.top() + (m_rng() % region.area.height());
                if (isValidSpawnPos(tx, ty)) {
                    e.pos = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                    m_enemies.push_back(e);
                    break;
                }
            }
        }
    }
}

void GameWorld::respawnEnemyInFront(Enemy& e, const Player& p, const QRect& biomeBounds) {
    float moveAngle = std::atan2(p.vel.y(), p.vel.x());
    if (p.vel.x() == 0 && p.vel.y() == 0) {
        static const float facingAngles[] = { 0, M_PI_2, M_PI, -M_PI_2 };
        moveAngle = facingAngles[p.facing];
    }

    bool spawned = false;
    for (int attempts = 0; attempts < 30; ++attempts) {
        float spawnAngle = moveAngle + ((m_rng() % 160) - 80) * (M_PI / 180.0f);
        float dist = 100.0f + (m_rng() % 300);

        QPointF candidate = p.pos + QPointF(std::cos(spawnAngle) * dist, std::sin(spawnAngle) * dist);
        int tx = int(candidate.x()) / TILE;
        int ty = int(candidate.y()) / TILE;

        // CHECK AGAINST BIOME BOUNDS
        if (tx >= biomeBounds.left() && tx <= biomeBounds.right() &&
            ty >= biomeBounds.top() && ty <= biomeBounds.bottom()) {

            if (isValidSpawnPos(tx, ty)) {
                e.pos = candidate;
                e.hp = 5.0f;
                e.behindTicks = 0;
                e.path.clear();
                e.vel = {0, 0};
                spawned = true;
                break;
            }
        }
    }

    // Fallback if the area in front of the player is outside the biome (e.g., player is at a gate)
    if (!spawned) {
        for (int attempts = 0; attempts < 20; ++attempts) {
            int tx = biomeBounds.left() + (m_rng() % biomeBounds.width());
            int ty = biomeBounds.top() + (m_rng() % biomeBounds.height());
            if (isValidSpawnPos(tx, ty)) {
                e.pos = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                e.behindTicks = 0;
                e.path.clear();
                break;
            }
        }
    }
}

void GameWorld::spawnKeyInBiome(int regionIdx) {
    if (regionIdx < 0 || regionIdx >= m_regions.size()) return;

    const auto& region = m_regions[regionIdx];

    for (int attempts = 0; attempts < 200; ++attempts) {
        int tx = region.area.left() + (m_rng() % region.area.width());
        int ty = region.area.top() + (m_rng() % region.area.height());

        // Validate spot and ensure it's not directly on the main dirt path
        if (isValidSpawnPos(tx, ty) && tileAt(tx, ty) != TerrainKind::Path) {
            Pickup key;
            key.id = m_nextId++;
            key.kind = PickupKind::Key;
            key.taken = false;
            key.pos = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
            m_pickups.push_back(key);
            return;
        }
    }

    int midY = region.area.center().y();
    int midX = region.area.center().x();
    Pickup key;
    key.id = m_nextId++;
    key.kind = PickupKind::Key;
    key.taken = false;
    key.pos = QPointF(midX * TILE + TILE/2.0, midY * TILE + TILE/2.0);
    m_pickups.push_back(key);
}

void GameWorld::initializeSession() {
    m_pickups.clear();
    int numPlayers = qMax(1, (int)m_players.size());

    for (int r = 0; r < 5; ++r) {
        const auto& region = m_regions[r];
        for (int i = 0; i < numPlayers; ++i) {
            spawnKeyInBiome(r);
        }
    }
}

bool GameWorld::isValidSpawnPos(int tx, int ty) const {
    if (tx <= 0 || ty <= 0 || tx >= m_gridW - 1 || ty >= m_gridH - 1) return false;

    TerrainKind t = tileAt(tx, ty);

    return (t != TerrainKind::Water &&
            t != TerrainKind::Barn  &&
            t != TerrainKind::Crops &&
            t != TerrainKind::Fence &&
            t != TerrainKind::Gate  &&
            t != TerrainKind::Pen);
}

bool GameWorld::hasKeyInBiome(int regionIdx) const {
    for (const auto& pk : m_pickups) {
        // We only care about keys currently on the ground
        if (!pk.taken && pk.kind == PickupKind::Key) {
            if (getRegionIndex(pk.pos) == regionIdx) {
                return true; // A key already exists here
            }
        }
    }
    return false;
}

void GameWorld::moveEntity(QPointF& pos, QPointF& vel, float dt, bool flying, Player* p) {
    QPointF np = pos + vel * dt;

    // Check horizontal movement
    if (!blocksMovement(int(np.x()) / TILE, int(pos.y()) / TILE, flying, p)) {
        pos.setX(np.x());
    }

    // Check vertical movement
    if (!blocksMovement(int(pos.x()) / TILE, int(np.y()) / TILE, flying, p)) {
        pos.setY(np.y());
    }
}

void GameWorld::spawnEnemies() {
    std::uniform_int_distribution<int> X(3,m_gridW-4), Y(3,m_gridH-4);
    auto add = [&](EnemyKind k, float hp){
        Enemy e; e.id = m_nextId++; e.kind = k; e.hp = hp; e.knockbackTicks = 0; e.behindTicks = 0;
        for (int tries=0; tries<40; ++tries){
            int x=X(m_rng), y=Y(m_rng);
            if (tileAt(x,y) == TerrainKind::Grass || tileAt(x,y) == TerrainKind::Path) {
                e.pos = QPointF(x*TILE+TILE/2.0, y*TILE+TILE/2.0); break;
            }
        }
        m_enemies.push_back(e);
    };
    for (int i=0; i<4; ++i) add(EnemyKind::Chicken, 3.0f);
    for (int i=0; i<2; ++i) add(EnemyKind::Goose, 5.0f);
    for (int i=0; i<1; ++i) add(EnemyKind::Bull, 10.0f);
}

void GameWorld::spawnFriendlies() {
    std::uniform_int_distribution<int> X(3,m_gridW-4), Y(3,m_gridH-4);
    for (int i=0;i<5;++i){
        Friendly f; f.id = m_nextId++;
        for (int t=0;t<30;++t){
            int x=X(m_rng), y=Y(m_rng);
            if (tileAt(x,y)==TerrainKind::Grass) { f.pos = QPointF(x*TILE+TILE/2.0, y*TILE+TILE/2.0); break; }
        }
        f.wanderTarget = f.pos;
        m_friendlies.push_back(f);
    }
}

quint32 GameWorld::addPlayer(const QString& name) {
    Player p; p.id = m_nextId++; p.name = name; p.pos = m_start;
    m_players.push_back(p);
    return p.id;
}

void GameWorld::removePlayer(quint32 id) {
    m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
                                   [&](const Player& p){ return p.id == id; }), m_players.end());
}

Player* GameWorld::player(quint32 id) {
    for (auto& p : m_players) if (p.id == id) return &p;
    return nullptr;
}

bool GameWorld::blocksMovement(int gx, int gy, bool flying, Player* p) const {
    TerrainKind t = tileAt(gx, gy);

    if (t == TerrainKind::Barn) return true;
    if (flying) return false;

    if (t == TerrainKind::Fence) return true;
    if (t == TerrainKind::Crops) return true;
    if (t == TerrainKind::Pen) return true;
    if (t == TerrainKind::Water) return true;

    if (t == TerrainKind::Gate) {
        if (p != nullptr && p->keysHeld > 0) return false;
        return true;
    }

    return false;
}

bool GameWorld::blocksMovementExt(int gx, int gy, bool flying, bool canSwim, Player* p) const {
    TerrainKind t = tileAt(gx, gy);

    if (t == TerrainKind::Barn) return true;
    if (flying) return false;

    if (t == TerrainKind::Fence) return true;
    if (t == TerrainKind::Crops) return true;
    if (t == TerrainKind::Pen) return true;

    if (t == TerrainKind::Water) {
        if (canSwim) return false;
        return true;
    }

    if (t == TerrainKind::Gate) {
        if (p != nullptr && p->keysHeld > 0) return false;
        return true;
    }

    return false;
}

void GameWorld::moveEntityExt(QPointF& pos, QPointF& vel, float dt, bool flying, bool canSwim, Player* p) {
    QPointF np = pos + vel * dt;
    if (!blocksMovementExt(int(np.x())/TILE, int(pos.y())/TILE, flying, canSwim, p)) pos.setX(np.x());
    if (!blocksMovementExt(int(pos.x())/TILE, int(np.y())/TILE, flying, canSwim, p)) pos.setY(np.y());
}

int GameWorld::getRegionIndex(QPointF pos) const {
    int gx = qBound(0, int(pos.x() / TILE), m_gridW - 1);
    int gy = qBound(0, int(pos.y() / TILE), m_gridH - 1);

    for (int i = 0; i < m_regions.size(); ++i) {
        // We use a slight adjustment to the check to ensure
        // players on the fence/gate line are counted in the biome they are in.
        if (gx >= m_regions[i].area.left() && gx <= m_regions[i].area.right()) {
            return i;
        }
    }
    return -1; // Default to first biome
}

void GameWorld::updateEnemy(Enemy& e, float dt) {
    bool canSwim = (e.kind == EnemyKind::Goose);
    bool flying = (e.kind == EnemyKind::Crow);

    // 1. Handle Knockback
    if (e.knockbackTicks > 0) {
        e.knockbackTicks--;
        moveEntityExt(e.pos, e.vel, dt, flying, canSwim, nullptr);
        e.vel *= 0.9f;
        return;
    }

    // 2. CROW SPECIAL CASE: Global roaming enemy with map-wide vision
    if (e.kind == EnemyKind::Crow) {
        Player* target = nullptr;
        float bestDist = 1e9f;

        for (auto& p : m_players) {
            if (!p.alive) continue;

            // Crows can ONLY be evaded by hiding in long grass
            TerrainKind pTile = tileAt(int(p.pos.x()/TILE), int(p.pos.y()/TILE));
            if (pTile == TerrainKind::LongGrass || pTile == TerrainKind::LeafPile) continue;

            // No distance limit - crows see across the entire map
            float d2 = QPointF::dotProduct(p.pos - e.pos, p.pos - e.pos);
            if (d2 < bestDist) {
                bestDist = d2;
                target = &p;
            }
        }

        bool isAggro = (target != nullptr);  // Always aggro if player is visible

        if (isAggro) {
            if (e.repathCooldown > 0) --e.repathCooldown;
            if (e.path.isEmpty() || e.repathCooldown <= 0) {
                // flying=true lets pathfinder cross Crops, Fences, Gates, Water
                e.path = Pathfinder::findPath(*this, e.pos, target->pos, true, false);
                e.repathCooldown = 15 + m_rng() % 15;
            }
        } else {
            // All players hidden in long grass - wander freely
            if (e.repathCooldown > 0) {
                --e.repathCooldown;
                e.vel = {0, 0};
                return;
            }

            if (e.path.isEmpty()) {
                for (int i = 0; i < 10; ++i) {
                    int tx = 2 + (m_rng() % (m_gridW - 4));
                    int ty = 2 + (m_rng() % (m_gridH - 4));
                    if (tileAt(tx, ty) != TerrainKind::Barn) {
                        e.path = Pathfinder::findPath(*this, e.pos,
                                                      QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0), true, false);
                        if (!e.path.isEmpty()) break;
                    }
                }
                if (e.path.isEmpty()) {
                    e.repathCooldown = 60 + m_rng() % 60;
                    return;
                }
            }
        }

        // Move crow - NO biome boundary restriction
        if (!e.path.isEmpty()) {
            QPointF wp = e.path.first();
            QPointF d = wp - e.pos;
            float len = std::hypot(d.x(), d.y());

            if (len < 8.f) {
                e.path.removeFirst();
                if (!isAggro && e.path.isEmpty()) {
                    e.repathCooldown = 120 + m_rng() % 180;
                    e.vel = {0, 0};
                }
            } else {
                float speed = isAggro ? 130.f : 70.f;
                e.vel = (d / len) * speed;
                // flying=true bypasses all terrain collision except Barn
                moveEntityExt(e.pos, e.vel, dt, true, false, nullptr);
            }
        }
        return;
    }

    // 3. BIOME-BOUND ENEMIES (everything except Crow)
    if (e.homeRegionIdx < 0 || e.homeRegionIdx >= m_regions.size()) {
        e.homeRegionIdx = getRegionIndex(e.pos);
        if (e.homeRegionIdx == -1) return;
    }
    QRect myBounds = m_regions[e.homeRegionIdx].area;

    Player* localTarget = nullptr;
    float bestDistSq = 1e9f;
    bool anyPlayerInBiome = false;

    for (auto& p : m_players) {
        if (!p.alive) continue;

        int pIdx = getRegionIndex(p.pos);

        if (pIdx == e.homeRegionIdx) {
            anyPlayerInBiome = true;

            TerrainKind pTile = tileAt(int(p.pos.x()/TILE), int(p.pos.y()/TILE));

            if (pTile != TerrainKind::LongGrass && pTile != TerrainKind::LeafPile) {
                float d2 = QPointF::dotProduct(p.pos - e.pos, p.pos - e.pos);
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    localTarget = &p;
                }
            }
        }
    }

    if (anyPlayerInBiome && localTarget) {
        if (localTarget->pos.x() - e.pos.x() > 550.0f) {
            e.behindTicks++;
        } else {
            e.behindTicks = 0;
        }

        if (e.behindTicks > 180) {
            respawnEnemyInFront(e, *localTarget, myBounds);
            return;
        }
    } else {
        e.behindTicks = 0;
    }

    bool isAggro = false;

    if (localTarget) {
        isAggro = true;

        if (e.repathCooldown > 0) --e.repathCooldown;
        if (e.path.isEmpty() || e.repathCooldown <= 0) {
            e.path = Pathfinder::findPath(*this, e.pos, localTarget->pos, flying, canSwim);
            e.repathCooldown = 20 + m_rng() % 20;
        }
    } else {
        if (e.repathCooldown > 0) {
            --e.repathCooldown;
            e.vel = {0, 0};
            return;
        }

        if (e.path.isEmpty()) {
            QPointF wanderTarget;
            bool found = false;

            for (int i = 0; i < 10; ++i) {
                int tx = myBounds.left() + (m_rng() % myBounds.width());
                int ty = myBounds.top() + (m_rng() % myBounds.height());
                if (isValidSpawnPos(tx, ty)) {
                    wanderTarget = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                    found = true;
                    break;
                }
            }

            if (!found) {
                e.repathCooldown = 60 + m_rng() % 60;
                return;
            }

            e.path = Pathfinder::findPath(*this, e.pos, wanderTarget, flying, canSwim);

            if (e.path.isEmpty()) {
                e.repathCooldown = 60 + m_rng() % 60;
                return;
            }
        }
    }

    if (!e.path.isEmpty()) {
        QPointF wp = e.path.first();
        QPointF d = wp - e.pos;
        float len = std::hypot(d.x(), d.y());

        if (len < 8.f) {
            e.path.removeFirst();
            if (!isAggro && e.path.isEmpty()) {
                e.repathCooldown = 90 + m_rng() % 120;
                e.vel = {0, 0};
            }
        } else {
            float speed = isAggro ? 115.f : 50.f;

            if (isAggro) {
                switch(e.kind) {
                case EnemyKind::Fox:     speed = 145.f; break;
                case EnemyKind::Cow:     speed = 60.f;  break;
                case EnemyKind::Sheep:   speed = 120.f; break;
                case EnemyKind::Chicken: speed = 100.f; break;
                case EnemyKind::Rooster: speed = 155.f; break;
                default: break;
                }
            }

            if (tileAt(int(e.pos.x()/TILE), int(e.pos.y()/TILE)) == TerrainKind::Mud
                && e.kind != EnemyKind::Pig) {
                speed *= 0.6f;
            }

            e.vel = (d / len) * speed;

            QPointF nextPos = e.pos + e.vel * dt;
            int ntx = int(nextPos.x() / TILE);
            int nty = int(nextPos.y() / TILE);

            if (ntx >= myBounds.left() && ntx <= myBounds.right() &&
                nty >= myBounds.top() && nty <= myBounds.bottom()) {
                moveEntityExt(e.pos, e.vel, dt, flying, canSwim, nullptr);
            } else {
                e.vel = {0, 0};
                e.path.clear();
            }
        }
    }
}

void GameWorld::resolvePlayerCollisions() {
    for (auto& p : m_players) {
        if (!p.alive || p.invulTicks > 0) continue;

        // 1. ENEMY COLLISION CHECK
        for (auto& e : m_enemies) {
            float dx = p.pos.x() - e.pos.x();
            float dy = p.pos.y() - e.pos.y();

            if (dx*dx + dy*dy < 25*25) {
                p.hp -= 1.0f;
                p.invulTicks = 80;

                if (p.hp <= 0) {
                    // 1. Increment deaths
                    p.deaths++;

                    // 2. Identify how far the player got before resetting position
                    int deathBiomeIdx = getRegionIndex(p.pos);

                    // 3. Reset player status and position
                    p.keysHeld = 0;
                    p.pos = m_start;
                    p.hp = p.maxHp;
                    p.invulTicks = 120;

                    // 4. Ensure exactly one key exists in every biome up to the death point
                    for (int i = 0; i <= deathBiomeIdx; ++i) {
                        // Only spawn a new key if there isn't one already on the ground in this biome
                        if (!hasKeyInBiome(i)) {
                            spawnKeyInBiome(i);
                        }
                    }
                }
                break; // Stop checking other enemies for this player this frame
            }
        }

        // 2. GOAL CHECK
        // Note: m_keysRequired is usually 0 or 1 for the final barn door
        // because keys are consumed at biome gates.
        QPointF distToGoal = p.pos - m_goal;
        if (distToGoal.x()*distToGoal.x() + distToGoal.y()*distToGoal.y() < 25*25 && p.finishMs < 0) {
            p.finishMs = elapsedMs();
        }

        // 3. FRIENDLY ENTITY PUSH (Gentle separation from farm animals)
        for (auto& f : m_friendlies) {
            float dx = float(p.pos.x() - f.pos.x());
            float dy = float(p.pos.y() - f.pos.y());
            float d2 = dx*dx + dy*dy;
            if (d2 < 18.f*18.f && d2 > 1.f) {
                float l = std::sqrt(d2);
                p.pos += QPointF(dx/l, dy/l) * 2.0f;
            }
        }
    }

    // 4. PLAYER-PLAYER SEPARATION
    // Prevents players from overlapping perfectly
    for (int i = 0; i < m_players.size(); ++i) {
        for (int j = i + 1; j < m_players.size(); ++j) {
            auto& a = m_players[i];
            auto& b = m_players[j];
            QPointF d = a.pos - b.pos;
            float l2 = float(d.x()*d.x() + d.y()*d.y());
            if (l2 < 18*18 && l2 > 1) {
                float l = std::sqrt(l2);
                QPointF n(d.x()/l, d.y()/l);
                a.pos += n * 1.5f;
                b.pos -= n * 1.5f;
            }
        }
    }
}

void GameWorld::resolveProjectiles(float dt) {
    for (auto& pr : m_projectiles) {
        pr.pos += pr.vel * dt;
        pr.life--;
        for (auto& e : m_enemies) {
            float dx = e.pos.x() - pr.pos.x(); float dy = e.pos.y() - pr.pos.y();
            if (dx*dx + dy*dy < 22*22) {
                e.hp -= 1.0f; pr.life = 0;
                QPointF pushDir(dx, dy); float len = std::hypot(dx, dy);
                if (len > 0) { e.vel = (pushDir/len) * 600.0f; e.knockbackTicks = 15; }
            }
        }
        for (auto& p : m_players) {
            if (p.id == pr.ownerId) continue;  // Can't hit yourself
            if (!p.alive || p.invulTicks > 0) continue;

            float dx = p.pos.x() - pr.pos.x();
            float dy = p.pos.y() - pr.pos.y();
            if (dx*dx + dy*dy < 18*18) {
                pr.life = 0;
                p.stunTicks = 40;  // ~0.65 second stun/slow
                break;
            }
        }
    }
    m_projectiles.erase(std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                                       [](const Projectile& pr){ return pr.life <= 0; }), m_projectiles.end());
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const Enemy& e){ return e.hp <= 0; }), m_enemies.end());
}

void GameWorld::resolvePickups() {
    for (auto& pk : m_pickups) {
        if (pk.taken) continue;
        for (auto& p : m_players) {
            if (!p.alive) continue;

            QPointF d = pk.pos - p.pos;
            if (d.x()*d.x() + d.y()*d.y() < 18*18) {
                // 1. KEY LOGIC (Already exists)
                if (pk.kind == PickupKind::Key) {
                    if (p.keysHeld >= 1) continue; // Can't carry two keys
                    p.keysHeld = 1;
                }

                // 2. CARROT (Speed Boost)
                else if (pk.kind == PickupKind::Carrot) {
                    p.boostTicks = 300; // ~5 seconds of 1.7x speed (logic is in tick())
                }

                // 3. MILK (Heal)
                else if (pk.kind == PickupKind::Milk) {
                    if (p.hp >= p.maxHp) continue; // Don't consume if health is full
                    p.hp = qMin(p.maxHp, p.hp + 1.0f);
                }

                // 4. EGG (Ammo)
                else if (pk.kind == PickupKind::Egg) {
                    p.eggs += 2; // Refill projectile ammo
                }

                pk.taken = true;
                break; // Pickup consumed by this player
            }
        }
    }
    // Remove taken pickups
    m_pickups.erase(std::remove_if(m_pickups.begin(), m_pickups.end(),
                                   [](const Pickup& pk){ return pk.taken; }), m_pickups.end());
}

void GameWorld::tick(float dt, const QHash<quint32, Proto::PlayerInput>& inputs) {
    for (auto& p : m_players) {
        if (!p.alive) continue;
        if (p.shootCooldown > 0) p.shootCooldown--;
        if (p.invulTicks > 0) p.invulTicks--;
        if (p.boostTicks > 0) p.boostTicks--;

        QPointF prevPos = p.pos;
        Proto::PlayerInput in = inputs.value(p.id);
        float speed = 140.f;
        if (tileAt(int(p.pos.x())/TILE, int(p.pos.y())/TILE) == TerrainKind::Mud) speed *= 0.7f;
        if (p.boostTicks > 0) speed *= 1.7f;
        if (p.stunTicks > 0) speed *= 0.3f;

        QPointF v(in.dx, in.dy); float l = std::hypot(v.x(), v.y());
        if (l > 0.01f) {
            p.vel = (v/l) * speed;
            if (std::abs(v.x()) > std::abs(v.y())) p.facing = (v.x() > 0 ? 0 : 2);
            else p.facing = (v.y() > 0 ? 1 : 3);
        } else p.vel = {0,0};

        moveEntity(p.pos, p.vel, dt, false, &p);

        int oldGridX = int(prevPos.x() / TILE);
        int newGridX = int(p.pos.x() / TILE);

        if (newGridX > oldGridX) {
            if (tileAt(oldGridX, int(p.pos.y()/TILE)) == TerrainKind::Gate) {
                if (p.keysHeld > 0) {
                    p.keysHeld--;
                }
            }
        }

        p.distanceMoved += std::hypot(p.pos.x() - prevPos.x(), p.pos.y() - prevPos.y());

        if (p.distanceMoved >= 100.0f) {
            p.distanceMoved = 0;

            int nearbyCount = 0;
            float checkRadiusSq = 800.0f * 800.0f;

            for (const auto& pk : m_pickups) {
                if (pk.taken) continue;
                float dx = pk.pos.x() - p.pos.x();
                float dy = pk.pos.y() - p.pos.y();
                if (dx*dx + dy*dy < checkRadiusSq) {
                    nearbyCount++;
                }
            }

            if (nearbyCount < 8) {
                spawnDynamicPickup(p.pos);
            }
        }

        if (in.shoot && p.eggs > 0 && p.shootCooldown <= 0) {
            p.eggs--; p.shootCooldown = 25;
            Projectile pr; pr.id = m_nextId++; pr.ownerId = p.id; pr.pos = p.pos; pr.life = 60;
            static const QPointF dirs[4] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
            pr.vel = dirs[p.facing] * 450.0f;
            m_projectiles.push_back(pr);
        }

        if (in.shoot && p.eggs > 0 && p.shootCooldown <= 0 && p.stunTicks <= 0) {
            p.eggs--; p.shootCooldown = 25;
            Projectile pr; pr.id = m_nextId++; pr.ownerId = p.id; pr.pos = p.pos; pr.life = 60;
            static const QPointF dirs[4] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
            pr.vel = dirs[p.facing] * 450.0f;
            m_projectiles.push_back(pr);
        }
    }

    // Update enemies
    for (auto& e : m_enemies) updateEnemy(e, dt);

    // Update friendlies
    for (auto& f : m_friendlies) {
        if (f.kind == FriendlyKind::Goldfish) {
            // Goldfish swim around in water
            if (f.wanderCooldown > 0) {
                f.wanderCooldown--;
                f.vel *= 0.95f;  // Slow drift while waiting
                f.pos += f.vel * dt;
                continue;
            }

            QPointF d = f.wanderTarget - f.pos;
            float dist = std::hypot(d.x(), d.y());

            if (dist < 10.f) {
                // Reached target, pick new water tile
                f.wanderCooldown = 30 + m_rng() % 90;
                f.vel = {0, 0};

                // Find a nearby water tile to swim to
                for (int tries = 0; tries < 20; ++tries) {
                    int tx = int(f.pos.x()/TILE) + (m_rng() % 7) - 3;
                    int ty = int(f.pos.y()/TILE) + (m_rng() % 7) - 3;
                    if (tx > 0 && ty > 0 && tx < m_gridW - 1 && ty < m_gridH - 1
                        && tileAt(tx, ty) == TerrainKind::Water) {
                        f.wanderTarget = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                        break;
                    }
                }
            } else {
                float speed = 35.f + (m_rng() % 20);
                f.vel = (d / dist) * speed;
                f.pos += f.vel * dt;
            }
        } else {
            // Original friendly wandering (farm animals)
            if (f.wanderCooldown > 0) {
                f.wanderCooldown--;
                f.vel = {0, 0};
                continue;
            }

            QPointF d = f.wanderTarget - f.pos;
            float dist = std::hypot(d.x(), d.y());

            if (dist < 10.f) {
                f.wanderCooldown = 60 + m_rng() % 120;
                f.vel = {0, 0};

                for (int tries = 0; tries < 15; ++tries) {
                    int tx = int(f.pos.x()/TILE) + (m_rng() % 9) - 4;
                    int ty = int(f.pos.y()/TILE) + (m_rng() % 9) - 4;
                    if (isValidSpawnPos(tx, ty)) {
                        f.wanderTarget = QPointF(tx * TILE + TILE/2.0, ty * TILE + TILE/2.0);
                        break;
                    }
                }
            } else {
                float speed = 30.f;
                f.vel = (d / dist) * speed;
                moveEntity(f.pos, f.vel, dt, false);
            }
        }
    }

    resolveProjectiles(dt);
    resolvePickups();
    resolvePlayerCollisions();
}

void GameWorld::snapshot(QVector<Proto::PlayerSnap>& ps,
                         QVector<Proto::EnemySnap>& es,
                         QVector<Proto::PickupSnap>& pk,
                         QVector<Proto::FriendlySnap>& fs,
                         QVector<Proto::ProjectileSnap>& prs) const {
    ps.clear(); es.clear(); pk.clear(); fs.clear(); prs.clear();
    for (auto& p : m_players) ps.push_back({p.id, (float)p.pos.x(), (float)p.pos.y(), p.facing, (quint8)(p.alive?1:0), p.hp});
    for (auto& e : m_enemies) es.push_back({e.id, (quint8)e.kind, (float)e.pos.x(), (float)e.pos.y(), e.hp});
    for (auto& p : m_pickups) pk.push_back({p.id, (quint8)p.kind, (float)p.pos.x(), (float)p.pos.y(), p.taken});
    for (auto& f : m_friendlies) fs.push_back({f.id, (quint8)f.kind, (float)f.pos.x(), (float)f.pos.y()});
    for (auto& pr : m_projectiles) prs.push_back({pr.id, (float)pr.pos.x(), (float)pr.pos.y()});
}

void GameWorld::applySnapshot(const QVector<Proto::PlayerSnap>& ps,
                              const QVector<Proto::EnemySnap>& es,
                              const QVector<Proto::PickupSnap>& pk,
                              const QVector<Proto::FriendlySnap>& fs,
                              const QVector<Proto::ProjectileSnap>& prs) {
    QHash<quint32, Player> existing;
    for (auto& p : m_players) existing.insert(p.id, p);
    m_players.clear();
    for (auto& s : ps) {
        Player p = existing.value(s.id);
        p.id = s.id; p.pos = QPointF(s.x, s.y); p.facing = s.facing; p.alive = s.alive; p.hp = s.hp;
        m_players.push_back(p);
    }
    m_enemies.clear();
    for (auto& s : es) {
        Enemy e; e.id = s.id; e.kind = EnemyKind(s.kind); e.pos = QPointF(s.x, s.y); e.hp = s.hp;
        m_enemies.push_back(e);
    }
    m_pickups.clear();
    for (auto& s : pk) {
        Pickup p; p.id = s.id; p.kind = PickupKind(s.kind); p.pos = QPointF(s.x, s.y); p.taken = s.taken;
        m_pickups.push_back(p);
    }
    m_friendlies.clear();
    for (auto& s : fs) {
        Friendly f;
        f.id = s.id;
        f.kind = FriendlyKind(s.kind);
        f.pos = QPointF(s.x, s.y);
        m_friendlies.push_back(f);
    }
    m_projectiles.clear();
    for (auto& s : prs) {
        Projectile pr; pr.id = s.id; pr.pos = QPointF(s.x, s.y); m_projectiles.push_back(pr);
    }
}

void GameWorld::spawnDynamicPickup(QPointF playerPos) {
    for (int attempts = 0; attempts < 30; ++attempts) {
        float angle = ((m_rng() % 140) - 70) * M_PI / 180.0f;
        float dist = 150.0f + (m_rng() % 400);

        QPointF candidate = playerPos + QPointF(std::cos(angle) * dist, std::sin(angle) * dist);
        int tx = int(candidate.x()) / TILE;
        int ty = int(candidate.y()) / TILE;

        if (isValidSpawnPos(tx, ty)) {
            Pickup pk;
            pk.id = m_nextId++;
            pk.taken = false;
            pk.pos = candidate;

            int r = m_rng() % 3;
            PickupKind kinds[] = {PickupKind::Carrot, PickupKind::Milk, PickupKind::Egg};
            pk.kind = kinds[r];

            m_pickups.push_back(pk);
            break;
        }
    }
}