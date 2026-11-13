#include "GameWidget.h"
#include "Leaderboard.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDataStream>
#include <QDateTime>
#include <QtMath>
#include <QPainterPath>

GameWidget::GameWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(800, 600);
    m_audio = new Audio(this);
    m_minimap = new Minimap(this);
    m_effects = new NaturalEffects(this);
    connect(&m_tickTimer, &QTimer::timeout, this, &GameWidget::onTick);
}

GameWidget::~GameWidget() = default;

void GameWidget::startSinglePlayer(const QString& name) {
    m_role = Role::SinglePlayer;
    m_playerName = name;
    m_world.generate(quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    updateTerrainCache();
    generateBackground();
    m_minimap->generate(m_world);
    m_effects->reset();

    if (m_world.players().isEmpty()) {
        m_localPlayerId = m_world.addPlayer(name);
    }

    m_world.initializeSession();
    m_world.startTimer();
    m_dtClock.start();
    m_finished = false;
    m_finishMs = -1;
    m_currentBiome = -1;
    m_biomeFlashTimer = 0;
    m_particles.clear();
    m_tickTimer.start(16);
    setFocus();
}

void GameWidget::startAsHost(NetworkManager* net, const QString& name) {
    m_role = Role::Host;
    m_net = net;
    m_playerName = name;
    m_world.generate(quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    updateTerrainCache();
    generateBackground();
    m_minimap->generate(m_world);
    m_effects->reset();

    if (m_localPlayerId == 0 || m_world.player(m_localPlayerId) == nullptr) {
        m_localPlayerId = m_world.addPlayer(name);
    }

    m_world.initializeSession();
    connect(net, &NetworkManager::messageFromClient, this, &GameWidget::onMessageFromClient);
    connect(net, &NetworkManager::clientConnected, this, &GameWidget::onClientConnected);
    m_countdownEndsAt = QDateTime::currentMSecsSinceEpoch() + 5000;
    m_net->broadcastToClients(Proto::encodeStartCountdown(5000));
    m_world.startTimer();
    m_dtClock.start();
    m_currentBiome = -1;
    m_biomeFlashTimer = 0;
    m_particles.clear();
    m_tickTimer.start(16);
    setFocus();
}

void GameWidget::startAsClient(NetworkManager* net, const QString& name) {
    m_role = Role::Client;
    m_net = net;
    m_playerName = name;
    connect(net, &NetworkManager::messageFromServer, this, &GameWidget::onMessageFromServer);
    m_dtClock.start();
    m_tickTimer.start(16);
}

void GameWidget::onClientConnected(quint32 cid) {
    quint32 pid = m_world.addPlayer(QString("Player %1").arg(cid));
    m_clientToPlayer.insert(cid, pid);
    m_net->broadcastToClients(Proto::encodeWelcome(pid, 0)); // Seed 0 means "use current"
    qint32 ms = qint32(qMax<qint64>(0, m_countdownEndsAt - QDateTime::currentMSecsSinceEpoch()));
    m_net->broadcastToClients(Proto::encodeStartCountdown(ms));
}

void GameWidget::onMessageFromClient(quint32 cid, QByteArray payload) {
    QDataStream d(payload); d.setVersion(QDataStream::Qt_6_2);
    quint8 t; d >> t;
    quint32 pid = m_clientToPlayer.value(cid, 0);
    if (!pid) return;

    if (t == Proto::Hello) {
        QString name; d >> name;
        if (auto* p = m_world.player(pid)) p->name = name;
    } else if (t == Proto::InputUpdate) {
        Proto::PlayerInput in;
        d >> in.dx >> in.dy >> in.jump >> in.shoot;
        m_collectedInputs.insert(pid, in);
    } else if (t == Proto::Finished) {
        if (auto* p = m_world.player(pid)) Leaderboard::add(p->name, p->finishMs);
    }
}

void GameWidget::onMessageFromServer(QByteArray payload) {
    QDataStream d(payload); d.setVersion(QDataStream::Qt_6_2);
    quint8 t; d >> t;
    if (t == Proto::Welcome) {
        updateTerrainCache();
        generateBackground();
        quint32 pid, seed; d >> pid >> seed;
        m_localPlayerId = pid;
        if (seed != 0) m_world.generate(seed);
    } else if (t == Proto::StartCountdown) {
        qint32 ms; d >> ms;
        m_countdownEndsAt = QDateTime::currentMSecsSinceEpoch() + ms;
    } else if (t == Proto::StateSnapshot) {
        qint64 elapsed; d >> elapsed;

        // Match the updated Protocol (No names, contains Projectiles/Friendlies)
        QVector<Proto::PlayerSnap> ps;
        QVector<Proto::EnemySnap> es;
        QVector<Proto::PickupSnap> pk;
        QVector<Proto::FriendlySnap> fs;
        QVector<Proto::ProjectileSnap> prs;

        quint32 n;
        d >> n; ps.resize(n);
        for(auto& s : ps) d >> s.id >> s.x >> s.y >> s.facing >> s.alive >> s.hp; // Fixed: Removed name

        d >> n; es.resize(n);
        for(auto& s : es) d >> s.id >> s.kind >> s.x >> s.y >> s.hp;

        d >> n; pk.resize(n);
        for(auto& s : pk) d >> s.id >> s.kind >> s.x >> s.y >> s.taken;

        d >> n; fs.resize(n);
        for(auto& s : fs) d >> s.id >> s.kind >> s.x >> s.y;

        d >> n; prs.resize(n);
        for(auto& s : prs) d >> s.id >> s.x >> s.y;

        m_world.applySnapshot(ps, es, pk, fs, prs);
    }
}

Proto::PlayerInput GameWidget::readLocalInput() const {
    Proto::PlayerInput in;
    if (m_keys.contains(Qt::Key_A) || m_keys.contains(Qt::Key_Left))  in.dx = -1;
    if (m_keys.contains(Qt::Key_D) || m_keys.contains(Qt::Key_Right)) in.dx =  1;
    if (m_keys.contains(Qt::Key_W) || m_keys.contains(Qt::Key_Up))    in.dy = -1;
    if (m_keys.contains(Qt::Key_S) || m_keys.contains(Qt::Key_Down))  in.dy =  1;

    // Check J key OR Mouse Click
    if (m_keys.contains(Qt::Key_J) || m_mousePendingShoot) {
        in.shoot = true;
        // Reset the latch
        const_cast<GameWidget*>(this)->m_mousePendingShoot = false;
    }
    return in;
}

void GameWidget::onTick() {
    float dt = m_dtClock.restart() / 1000.f;
    if (dt > 0.1f) dt = 0.05f;

    if (m_role != Role::Client) {
        QHash<quint32, Proto::PlayerInput> inputs;
        inputs.insert(m_localPlayerId, readLocalInput());

        if (m_role == Role::Host) {
            for (auto it = m_collectedInputs.begin(); it != m_collectedInputs.end(); ++it)
                inputs.insert(it.key(), it.value());
        }

        // World keeps ticking even if local host is finished
        m_world.tick(dt, inputs);

        if (m_role == Role::Host && m_net) {
            QVector<Proto::PlayerSnap> ps; QVector<Proto::EnemySnap> es;
            QVector<Proto::PickupSnap> pk; QVector<Proto::FriendlySnap> fs;
            QVector<Proto::ProjectileSnap> prs;
            m_world.snapshot(ps, es, pk, fs, prs);
            m_net->broadcastToClients(Proto::encodeSnapshot(ps, es, pk, fs, prs, m_world.elapsedMs()));
        }
    } else {
        m_net->sendToServer(Proto::encodeInput(readLocalInput()));
    }

    // Snapshot enemy state BEFORE tick for kill detection
    QVector<QPointF> prevEnemyPos;
    QVector<QColor> prevEnemyColors;
    for (const auto& e : m_world.enemies()) {
        prevEnemyPos.push_back(e.pos);
        QColor ec;
        switch (e.kind) {
        case EnemyKind::Fox:     ec = QColor(220, 120, 30);  break;
        case EnemyKind::Pig:     ec = QColor(255, 180, 180); break;
        case EnemyKind::Cow:     ec = Qt::white;             break;
        case EnemyKind::Sheep:   ec = QColor(240, 240, 235); break;
        case EnemyKind::Crow:    ec = QColor(50, 50, 50);    break;
        case EnemyKind::Goose:   ec = Qt::white;             break;
        case EnemyKind::Bull:    ec = QColor(100, 50, 20);   break;
        case EnemyKind::Rooster: ec = QColor(160, 80, 30);   break;
        default:                 ec = Qt::white;             break;
        }
        prevEnemyColors.push_back(ec);
    }
    int prevEnemyCount = prevEnemyPos.size();

    // Snapshot player alive states BEFORE tick
    QHash<quint32, bool> prevAlive;
    QHash<quint32, QPointF> prevPlayerPos;
    for (const auto& p : m_world.players()) {
        prevAlive.insert(p.id, p.alive);
        prevPlayerPos.insert(p.id, p.pos);
    }

    if (m_role != Role::Client) {
        QHash<quint32, Proto::PlayerInput> inputs;
        inputs.insert(m_localPlayerId, readLocalInput());

        if (m_role == Role::Host) {
            for (auto it = m_collectedInputs.begin(); it != m_collectedInputs.end(); ++it)
                inputs.insert(it.key(), it.value());
        }

        m_world.tick(dt, inputs);

        if (m_role == Role::Host && m_net) {
            QVector<Proto::PlayerSnap> ps; QVector<Proto::EnemySnap> es;
            QVector<Proto::PickupSnap> pk; QVector<Proto::FriendlySnap> fs;
            QVector<Proto::ProjectileSnap> prs;
            m_world.snapshot(ps, es, pk, fs, prs);
            m_net->broadcastToClients(Proto::encodeSnapshot(ps, es, pk, fs, prs, m_world.elapsedMs()));
        }
    } else {
        m_net->sendToServer(Proto::encodeInput(readLocalInput()));
    }

    // Detect enemy kills (enemy count decreased)
    int newEnemyCount = m_world.enemies().size();
    if (newEnemyCount < prevEnemyCount) {
        // Find which enemies disappeared
        QSet<quint32> currentIds;
        for (const auto& e : m_world.enemies()) {
            currentIds.insert(e.id);
        }

        // spawn poof for each killed enemy
        int killed = prevEnemyCount - newEnemyCount;
        for (int i = 0; i < prevEnemyPos.size() && killed > 0; ++i) {
            bool stillExists = false;
            for (const auto& e : m_world.enemies()) {
                float dx = e.pos.x() - prevEnemyPos[i].x();
                float dy = e.pos.y() - prevEnemyPos[i].y();
                if (dx*dx + dy*dy < 5*5) {
                    stillExists = true;
                    break;
                }
            }
            if (!stillExists) {
                spawnEnemyPoof(prevEnemyPos[i], prevEnemyColors[i]);
                killed--;
            }
        }
    }

    // Detect player deaths (position reset to start)
    for (const auto& p : m_world.players()) {
        QPointF prev = prevPlayerPos.value(p.id, p.pos);
        float distMoved = std::hypot(p.pos.x() - prev.x(), p.pos.y() - prev.y());

        // If player teleported far (death respawn), spawn burst at old position
        if (distMoved > 200.0f) {
            QColor playerColor = (p.id == m_localPlayerId) ? QColor(60, 100, 220) : QColor(200, 60, 200);
            spawnDeathBurst(prev, playerColor, 25);
        }
    }


    // Individual finish logic
    if (!m_finished) {
        if (auto* p = m_world.player(m_localPlayerId)) {
            if (p->finishMs >= 0) {
                m_finished = true;
                m_finishMs = p->finishMs;
                m_showResultsMenu = true;
                if (m_role == Role::Client) m_net->sendToServer(Proto::encodeFinished(m_finishMs));
            }
        }
    }

    m_effects->update(0.016f, m_world);

    update();
}

void GameWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.4f, 0.6f, 0.3f, 1.f);
}

void GameWidget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

QPointF GameWidget::cameraOffset() const {
    const Player* p = const_cast<GameWorld&>(m_world).player(m_localPlayerId);
    if (p) return QPointF(width()/2.0 - p->pos.x(), height()/2.0 - p->pos.y());
    return {0,0};
}

void GameWidget::generateBackground() {
    m_clouds.clear();
    m_bgBushes.clear();
    m_bgFlowers.clear();

    std::mt19937 bgRng(99);

    // Generate clouds across a wide area
    int mapPixelW = m_world.gridW() * TILE;
    int mapPixelH = m_world.gridH() * TILE;

    for (int i = 0; i < 25; ++i) {
        Cloud c;
        c.x = -200.f + (bgRng() % (mapPixelW + 400));
        c.y = -300.f + (bgRng() % 250);
        c.width = 60.f + (bgRng() % 120);
        c.height = 20.f + (bgRng() % 30);
        c.speed = 3.f + (bgRng() % 15);
        c.alpha = 40 + (bgRng() % 60);
        m_clouds.push_back(c);
    }

    // Bushes scattered in the border area
    for (int i = 0; i < 80; ++i) {
        BgBush b;
        // Pick a random edge
        int edge = bgRng() % 4;
        switch (edge) {
        case 0: // top
            b.x = -100.f + (bgRng() % (mapPixelW + 200));
            b.y = -10.f - (bgRng() % 80);
            break;
        case 1: // bottom
            b.x = -100.f + (bgRng() % (mapPixelW + 200));
            b.y = mapPixelH + 10.f + (bgRng() % 80);
            break;
        case 2: // left
            b.x = -10.f - (bgRng() % 80);
            b.y = -50.f + (bgRng() % (mapPixelH + 100));
            break;
        case 3: // right
            b.x = mapPixelW + 10.f + (bgRng() % 80);
            b.y = -50.f + (bgRng() % (mapPixelH + 100));
            break;
        }
        b.radius = 6.f + (bgRng() % 10);
        int g = 80 + (bgRng() % 60);
        b.color = QColor(30 + bgRng()%20, g, 25 + bgRng()%15);
        m_bgBushes.push_back(b);
    }

    // Flowers in the border grass
    for (int i = 0; i < 120; ++i) {
        BgFlower f;
        int edge = bgRng() % 4;
        switch (edge) {
        case 0:
            f.x = -80.f + (bgRng() % (mapPixelW + 160));
            f.y = -5.f - (bgRng() % 60);
            break;
        case 1:
            f.x = -80.f + (bgRng() % (mapPixelW + 160));
            f.y = mapPixelH + 5.f + (bgRng() % 60);
            break;
        case 2:
            f.x = -5.f - (bgRng() % 60);
            f.y = -40.f + (bgRng() % (mapPixelH + 80));
            break;
        case 3:
            f.x = mapPixelW + 5.f + (bgRng() % 60);
            f.y = -40.f + (bgRng() % (mapPixelH + 80));
            break;
        }

        QColor colors[] = {
            QColor(255, 100, 100), QColor(255, 255, 100),
            QColor(200, 100, 255), QColor(255, 180, 200),
            QColor(100, 200, 255), QColor(255, 200, 100)
        };
        f.color = colors[bgRng() % 6];
        m_bgFlowers.push_back(f);
    }
}

void GameWidget::drawParallaxBackground(QPainter& painter, QPointF offset) {
    painter.fillRect(rect(), QColor(65, 110, 50));

    // Clouds (0.1x parallax)
    painter.save();
    painter.translate(offset * 0.1f);

    int mapW = m_world.gridW() * TILE + 400;

    for (const auto& c : m_clouds) {
        float drift = c.x + m_animTime * c.speed;
        float wrappedX = std::fmod(drift, (float)mapW);
        if (wrappedX < -200) wrappedX += mapW;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, c.alpha));
        painter.drawEllipse(QPointF(wrappedX, c.y), c.width * 0.5f, c.height);
        painter.drawEllipse(QPointF(wrappedX - c.width * 0.25f, c.y + 3),
                            c.width * 0.35f, c.height * 0.8f);
        painter.drawEllipse(QPointF(wrappedX + c.width * 0.3f, c.y + 2),
                            c.width * 0.4f, c.height * 0.9f);
    }
    painter.restore();
}

void GameWidget::drawFenceExterior(QPainter& painter, QPointF offset) {
    int mapPixelW = m_world.gridW() * TILE;
    int mapPixelH = m_world.gridH() * TILE;
    int borderSize = 300;

    painter.save();
    painter.translate(offset);

    QRectF visibleWorld(-offset.x() - borderSize, -offset.y() - borderSize,
                        width() + borderSize * 2, height() + borderSize * 2);

    // Dark grass tiles around the map edge
    int tileStartX = int((-offset.x() - borderSize) / TILE) - 1;
    int tileStartY = int((-offset.y() - borderSize) / TILE) - 1;
    int tileEndX = int((-offset.x() + width() + borderSize) / TILE) + 1;
    int tileEndY = int((-offset.y() + height() + borderSize) / TILE) + 1;

    std::mt19937 groundRng(77);

    for (int ty = tileStartY; ty <= tileEndY; ++ty) {
        for (int tx = tileStartX; tx <= tileEndX; ++tx) {
            if (tx >= 0 && tx < m_world.gridW() && ty >= 0 && ty < m_world.gridH()) continue;

            groundRng.seed(tx * 7919 + ty * 104729);

            int v = groundRng() % 12 - 6;
            QColor base(70 + v, 115 + v/2, 50 + v);
            QRect r(tx * TILE, ty * TILE, TILE, TILE);

            painter.setPen(Qt::NoPen);
            painter.fillRect(r, base);

            // Dark grass dots
            painter.setPen(QColor(55 + v, 95 + v, 40 + v));
            int dots = groundRng() % 4;
            for (int d = 0; d < dots; ++d) {
                int dx = groundRng() % (TILE - 4) + 2;
                int dy = groundRng() % (TILE - 4) + 2;
                painter.drawPoint(r.x() + dx, r.y() + dy);
            }

            // Occasional dark patch
            if (groundRng() % 8 == 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(55 + v, 90 + v/2, 38 + v, 60));
                int px = groundRng() % (TILE - 8);
                int py = groundRng() % (TILE - 8);
                painter.drawEllipse(r.x() + px, r.y() + py, 8, 6);
            }
        }
    }

    // Flowers
    for (const auto& f : m_bgFlowers) {
        QPointF fp(f.x, f.y);
        if (!visibleWorld.contains(fp)) continue;

        float bob = std::sin(m_animTime * 2.0f + f.x * 0.05f) * 1.5f;
        QPointF drawPos = fp + QPointF(0, bob);

        painter.setPen(QPen(QColor(50, 100, 35), 1));
        painter.drawLine(drawPos + QPointF(0, 2), drawPos + QPointF(0, 6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(f.color);
        painter.drawEllipse(drawPos, 3, 3);
        painter.setBrush(QColor(255, 230, 50));
        painter.drawEllipse(drawPos, 1.5, 1.5);
    }

    // Bushes
    for (const auto& b : m_bgBushes) {
        QPointF bp(b.x, b.y);
        if (!visibleWorld.contains(bp)) continue;

        painter.setPen(Qt::NoPen);

        painter.setBrush(QColor(0, 0, 0, 20));
        painter.drawEllipse(bp + QPointF(1, 2), b.radius, b.radius * 0.5f);

        std::mt19937 bushRng(int(b.x * 31 + b.y * 97));
        for (int i = 0; i < 6; ++i) {
            float ox = (bushRng() % int(b.radius * 1.4f)) - b.radius * 0.7f;
            float oy = (bushRng() % int(b.radius * 1.0f)) - b.radius * 0.5f;
            float sr = b.radius * 0.4f + (bushRng() % int(b.radius * 0.3f + 1));

            int gv = bushRng() % 20 - 10;
            QColor blobColor(
                qBound(0, b.color.red() + gv, 255),
                qBound(0, b.color.green() + gv, 255),
                qBound(0, b.color.blue() + gv/2, 255)
                );

            painter.setBrush(blobColor);
            painter.drawEllipse(bp + QPointF(ox, oy), sr, sr * 0.8f);
        }

        painter.setPen(QColor(
            qBound(0, b.color.red() - 30, 255),
            qBound(0, b.color.green() - 30, 255),
            qBound(0, b.color.blue() - 15, 255)
            ));
        for (int i = 0; i < 4; ++i) {
            float dx = (bushRng() % int(b.radius * 1.6f)) - b.radius * 0.8f;
            float dy = (bushRng() % int(b.radius * 1.2f)) - b.radius * 0.6f;
            painter.drawPoint(bp + QPointF(dx, dy));
        }
    }

    painter.restore();
}

void GameWidget::updateTerrainCache() {
    int gw = m_world.gridW();
    int gh = m_world.gridH();

    m_terrainCache = QPixmap(gw * TILE, gh * TILE);
    QPainter p(&m_terrainCache);
    p.setRenderHint(QPainter::Antialiasing, false);

    std::mt19937 tileRng(42);

    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            TerrainKind t = m_world.tileAt(x, y);
            QRect r(x * TILE, y * TILE, TILE, TILE);
            p.setPen(Qt::NoPen);

            int v = tileRng() % 16 - 8;

            // Determine which biome this tile belongs to
            int biomeIdx = m_world.getRegionIndex(QPointF(x * TILE + TILE/2, y * TILE + TILE/2));
            BiomeType biome = BiomeType::Grassy;
            if (biomeIdx >= 0 && biomeIdx < 6) {
                BiomeType types[] = {
                    BiomeType::Grassy, BiomeType::Muddy, BiomeType::Watery,
                    BiomeType::Crops, BiomeType::Coop, BiomeType::Field
                };
                biome = types[biomeIdx];
            }

            switch (t) {
            case TerrainKind::Grass: {
                QColor base(110 + v, 180 + v/2, 90 + v);
                p.fillRect(r, base);

                p.setPen(QColor(90 + v, 160 + v, 75 + v));
                int dots = tileRng() % 4;
                for (int d = 0; d < dots; ++d) {
                    int dx = tileRng() % (TILE - 4) + 2;
                    int dy = tileRng() % (TILE - 4) + 2;
                    p.drawPoint(r.x() + dx, r.y() + dy);
                }
                break;
            }

            case TerrainKind::Mud: {
                QColor base(120 + v, 80 + v/2, 50 + v/3);
                p.fillRect(r, base);

                p.setPen(QColor(100 + v, 65 + v/2, 40 + v/3));
                int specks = tileRng() % 3;
                for (int d = 0; d < specks; ++d) {
                    int dx = tileRng() % (TILE - 2) + 1;
                    int dy = tileRng() % (TILE - 2) + 1;
                    p.drawPoint(r.x() + dx, r.y() + dy);
                    p.drawPoint(r.x() + dx + 1, r.y() + dy);
                }
                break;
            }

            case TerrainKind::Water: {
                QColor base(60 + v/2, 120 + v, 200 + v/2);
                p.fillRect(r, base);

                p.setPen(QPen(QColor(80, 150, 220, 80), 1));
                int waveY = 8 + (tileRng() % 16);
                p.drawLine(r.x() + 2, r.y() + waveY,
                           r.x() + TILE - 2, r.y() + waveY - 2);
                break;
            }

            case TerrainKind::Path: {
                // Biome-themed path color
                QColor pathBase = BiomeManager::getPathColor(biome);
                QColor base(pathBase.red() + v/2, pathBase.green() + v/2, pathBase.blue() + v);
                p.fillRect(r, base);

                // Biome-specific path detail
                switch (biome) {
                case BiomeType::Grassy: {
                    // Simple dirt specks
                    p.setPen(QColor(base.red() - 20, base.green() - 20, base.blue() - 15));
                    int specks = 2 + tileRng() % 3;
                    for (int d = 0; d < specks; ++d) {
                        int dx = tileRng() % (TILE - 2) + 1;
                        int dy = tileRng() % (TILE - 2) + 1;
                        p.drawPoint(r.x() + dx, r.y() + dy);
                    }
                    break;
                }
                case BiomeType::Muddy: {
                    // Muddy rut lines
                    p.setPen(QPen(QColor(base.red() - 15, base.green() - 15, base.blue() - 10), 1));
                    int rutY = 6 + tileRng() % (TILE - 12);
                    p.drawLine(r.x() + 3, r.y() + rutY, r.x() + TILE - 3, r.y() + rutY + 1);

                    // Wet patches
                    if (tileRng() % 4 == 0) {
                        p.setPen(Qt::NoPen);
                        p.setBrush(QColor(base.red() - 20, base.green() - 15, base.blue() - 10, 70));
                        int px = tileRng() % (TILE - 8);
                        int py = tileRng() % (TILE - 8);
                        p.drawEllipse(r.x() + px, r.y() + py, 8, 5);
                    }
                    break;
                }
                case BiomeType::Watery: {
                    // Stone block grid
                    p.setPen(QPen(QColor(base.red() - 20, base.green() - 20, base.blue() - 18), 1));
                    p.drawLine(r.x() + TILE/2, r.y(), r.x() + TILE/2, r.y() + TILE);
                    p.drawLine(r.x(), r.y() + TILE/2, r.x() + TILE, r.y() + TILE/2);

                    // Subtle crack
                    if (tileRng() % 5 == 0) {
                        p.setPen(QColor(base.red() - 25, base.green() - 25, base.blue() - 20));
                        int cx = tileRng() % (TILE - 6) + 3;
                        int cy = tileRng() % (TILE - 6) + 3;
                        p.drawLine(r.x() + cx, r.y() + cy, r.x() + cx + 3, r.y() + cy + 2);
                    }
                    break;
                }
                case BiomeType::Crops: {
                    // Gravel dots
                    for (int d = 0; d < 5; ++d) {
                        int dx = tileRng() % (TILE - 2) + 1;
                        int dy = tileRng() % (TILE - 2) + 1;
                        int shade = tileRng() % 25;
                        p.setPen(QColor(base.red() - 15 + shade, base.green() - 15 + shade, base.blue() - 10 + shade));
                        p.drawPoint(r.x() + dx, r.y() + dy);
                    }

                    // Straw bits
                    if (tileRng() % 4 == 0) {
                        p.setPen(QColor(200, 180, 100));
                        int sx = tileRng() % (TILE - 8) + 4;
                        int sy = tileRng() % (TILE - 4) + 2;
                        p.drawLine(r.x() + sx, r.y() + sy, r.x() + sx + 4, r.y() + sy + 1);
                    }
                    break;
                }
                case BiomeType::Coop: {
                    // Wood plank lines
                    p.setPen(QPen(QColor(base.red() - 18, base.green() - 18, base.blue() - 12), 1));
                    for (int plank = 5; plank < TILE; plank += 8) {
                        p.drawLine(r.x(), r.y() + plank, r.x() + TILE, r.y() + plank);
                    }

                    // Wood grain
                    if (tileRng() % 4 == 0) {
                        p.setPen(QColor(base.red() - 10, base.green() - 10, base.blue() - 8));
                        int gx = tileRng() % (TILE - 6) + 3;
                        p.drawLine(r.x() + gx, r.y() + 2, r.x() + gx + 1, r.y() + TILE - 2);
                    }

                    // Straw scatter
                    if (tileRng() % 3 == 0) {
                        p.setPen(QColor(190, 170, 90));
                        int sx = tileRng() % (TILE - 6) + 3;
                        int sy = tileRng() % (TILE - 4) + 2;
                        p.drawLine(r.x() + sx, r.y() + sy, r.x() + sx + 3, r.y() + sy - 1);
                    }
                    break;
                }
                case BiomeType::Field: {
                    // Dusty specks
                    p.setPen(QColor(base.red() - 15, base.green() - 15, base.blue() - 12));
                    int specks = 2 + tileRng() % 3;
                    for (int d = 0; d < specks; ++d) {
                        int dx = tileRng() % (TILE - 2) + 1;
                        int dy = tileRng() % (TILE - 2) + 1;
                        p.drawPoint(r.x() + dx, r.y() + dy);
                    }

                    // Trampled grass bits
                    if (tileRng() % 5 == 0) {
                        p.setPen(QColor(130, 160, 90, 80));
                        int gx = tileRng() % (TILE - 6) + 3;
                        int gy = tileRng() % (TILE - 4) + 2;
                        p.drawLine(r.x() + gx, r.y() + gy, r.x() + gx + 2, r.y() + gy - 2);
                    }
                    break;
                }
                }
                break;
            }

            case TerrainKind::Crops: {
                p.fillRect(r, QColor(160 + v/2, 130 + v/2, 60 + v/3));

                p.setPen(QPen(QColor(80, 140, 40), 2));
                for (int row = 4; row < TILE; row += 8) {
                    p.drawLine(r.x() + 2, r.y() + row, r.x() + TILE - 2, r.y() + row);
                }

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(60 + v, 130 + v, 30));
                for (int row = 2; row < TILE; row += 8) {
                    for (int col = 4; col < TILE; col += 10) {
                        p.drawEllipse(r.x() + col, r.y() + row, 5, 4);
                    }
                }
                break;
            }

            case TerrainKind::Fence: {
                // Check if this is a biome border fence or outer wall
                bool isBiomeBorder = false;
                int leftBiome = m_world.getRegionIndex(QPointF((x-1) * TILE + TILE/2, y * TILE + TILE/2));
                int rightBiome = m_world.getRegionIndex(QPointF((x+1) * TILE + TILE/2, y * TILE + TILE/2));
                if (leftBiome != rightBiome && leftBiome >= 0 && rightBiome >= 0) {
                    isBiomeBorder = true;
                }

                if (isBiomeBorder) {
                    // Themed checkpoint barrier
                    QColor postColor = BiomeManager::getGatePostColor(biome);

                    // Ground base matches surrounding biome
                    QColor groundBase = BiomeManager::getPrimaryTile(biome) == TerrainKind::Mud
                                            ? QColor(120 + v, 80 + v/2, 50 + v/3)
                                            : QColor(100 + v, 165 + v/2, 80 + v);
                    p.fillRect(r, groundBase);

                    // Checkpoint post
                    p.setBrush(postColor);
                    p.setPen(QPen(postColor.darker(130), 1));
                    p.drawRect(r.x() + TILE/2 - 3, r.y(), 6, TILE);

                    // Cross rails
                    p.setPen(QPen(postColor.darker(115), 2));
                    p.drawLine(r.x() + 2, r.y() + TILE/3, r.x() + TILE - 2, r.y() + TILE/3);
                    p.drawLine(r.x() + 2, r.y() + 2*TILE/3, r.x() + TILE - 2, r.y() + 2*TILE/3);

                    // Warning stripe at top
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(220, 180, 50, 100));
                    p.drawRect(r.x(), r.y(), TILE, 3);
                    p.drawRect(r.x(), r.y() + TILE - 3, TILE, 3);
                } else {
                    // Outer wall fence (original style but improved)
                    p.fillRect(r, QColor(90 + v, 155 + v/2, 70 + v));

                    QColor wood(130, 90, 45);
                    QColor woodDark(95, 65, 30);

                    p.fillRect(r.x() + 2, r.y(), 4, TILE, wood);
                    p.fillRect(r.x() + TILE - 6, r.y(), 4, TILE, wood);

                    p.fillRect(r.x(), r.y() + 6, TILE, 3, woodDark);
                    p.fillRect(r.x(), r.y() + TILE - 10, TILE, 3, woodDark);

                    p.setPen(QColor(70, 70, 70));
                    p.drawPoint(r.x() + 4, r.y() + 7);
                    p.drawPoint(r.x() + TILE - 4, r.y() + 7);
                    p.drawPoint(r.x() + 4, r.y() + TILE - 9);
                    p.drawPoint(r.x() + TILE - 4, r.y() + TILE - 9);
                }
                break;
            }

            case TerrainKind::Barn: {
                p.fillRect(r, QColor(160 + v/2, 35, 35));

                p.setPen(QPen(QColor(130, 25, 25), 1));
                for (int plank = 0; plank < TILE; plank += 8) {
                    p.drawLine(r.x() + plank, r.y(), r.x() + plank, r.y() + TILE);
                }

                p.setPen(QPen(QColor(100, 70, 40), 2));
                p.drawLine(r.topLeft(), r.bottomRight());
                p.drawLine(r.topRight(), r.bottomLeft());
                break;
            }

            case TerrainKind::Gate: {
                // Themed gate based on biome
                QColor pathBase = BiomeManager::getPathColor(biome);
                QColor postColor = BiomeManager::getGatePostColor(biome);

                p.fillRect(r, QColor(pathBase.red() + v/2, pathBase.green() + v/2, pathBase.blue() + v));

                // Gate posts
                p.setBrush(postColor);
                p.setPen(QPen(postColor.darker(130), 1));
                p.drawRect(r.x(), r.y(), 5, TILE);
                p.drawRect(r.x() + TILE - 5, r.y(), 5, TILE);

                // Gate bars
                p.setPen(QPen(postColor.lighter(120), 2));
                p.drawLine(r.x() + 5, r.y() + TILE/3, r.x() + TILE - 5, r.y() + TILE/3);
                p.drawLine(r.x() + 5, r.y() + 2*TILE/3, r.x() + TILE - 5, r.y() + 2*TILE/3);

                // Key hint glow
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 100, 80));
                p.drawEllipse(r.center(), 5, 5);

                // Arrow hint on path
                p.setPen(QPen(QColor(255, 220, 80, 120), 1.5));
                int midY = r.y() + TILE/2;
                p.drawLine(r.x() + 8, midY, r.x() + TILE - 8, midY);
                p.drawLine(r.x() + TILE - 12, midY - 3, r.x() + TILE - 8, midY);
                p.drawLine(r.x() + TILE - 12, midY + 3, r.x() + TILE - 8, midY);
                break;
            }

            case TerrainKind::LongGrass: {
                p.fillRect(r, QColor(70 + v, 130 + v/2, 60 + v));

                p.setPen(QPen(QColor(50 + v, 100, 45), 1));
                for (int blade = 0; blade < 5; ++blade) {
                    int bx = 4 + (tileRng() % (TILE - 8));
                    int lean = (tileRng() % 7) - 3;
                    p.drawLine(r.x() + bx, r.y() + TILE - 2,
                               r.x() + bx + lean, r.y() + 4 + (tileRng() % 8));
                }

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(40, 90, 35, 60));
                int patches = 1 + tileRng() % 2;
                for (int pp = 0; pp < patches; ++pp) {
                    int px = tileRng() % (TILE - 8);
                    int py = tileRng() % (TILE - 8);
                    p.drawEllipse(r.x() + px, r.y() + py, 8, 6);
                }
                break;
            }

            case TerrainKind::LeafPile: {
                // Draw grass base underneath
                QColor grassBase(105 + v, 170 + v/2, 85 + v);
                p.fillRect(r, grassBase);

                // The actual leaf pile detail is drawn by NaturalEffects::drawLeafPiles()
                // in world space so it can be animated. Just darken the base slightly
                // to hint that something is here even without the overlay.
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(120, 80, 30, 40));
                p.drawEllipse(r.center(), TILE/2 - 2, TILE/2 - 4);
                break;
            }

            case TerrainKind::Pen: {
                QColor plank(130 + v/2, 90 + v/3, 45 + v/4);
                QColor plankDark(100 + v/2, 65 + v/3, 30 + v/4);

                p.fillRect(r, plank);

                p.setPen(QPen(plankDark, 1));
                for (int row = 0; row < TILE; row += 6) {
                    p.drawLine(r.x(), r.y() + row, r.x() + TILE, r.y() + row);
                }

                p.setPen(QPen(plankDark, 2));
                p.drawLine(r.x() + 4, r.y(), r.x() + 4, r.y() + TILE);
                p.drawLine(r.x() + TILE - 4, r.y(), r.x() + TILE - 4, r.y() + TILE);

                p.setPen(QColor(70, 70, 70));
                p.drawPoint(r.x() + 4, r.y() + 6);
                p.drawPoint(r.x() + 4, r.y() + 18);
                p.drawPoint(r.x() + TILE - 4, r.y() + 6);
                p.drawPoint(r.x() + TILE - 4, r.y() + 18);

                p.setPen(QPen(QColor(160, 160, 160, 80), 0.5));
                for (int dx = 4; dx < TILE; dx += 6) {
                    p.drawLine(r.x() + dx, r.y(), r.x() + dx, r.y() + TILE);
                }
                for (int dy = 4; dy < TILE; dy += 6) {
                    p.drawLine(r.x(), r.y() + dy, r.x() + TILE, r.y() + dy);
                }
                break;
            }
            }

            // Tile edge transitions
            if (x > 0 && m_world.tileAt(x-1, y) != t) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 20));
                p.drawRect(r.x(), r.y(), 2, TILE);
            }
            if (y > 0 && m_world.tileAt(x, y-1) != t) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 20));
                p.drawRect(r.x(), r.y(), TILE, 2);
            }
        }
    }
    p.end();

    generateProps();
}

void GameWidget::generateProps() {
    m_props.clear();
    std::mt19937 propRng(123);

    for (int y = 1; y < m_world.gridH() - 1; ++y) {
        for (int x = 1; x < m_world.gridW() - 1; ++x) {
            TerrainKind t = m_world.tileAt(x, y);

            if (t == TerrainKind::Grass) {
                if (propRng() % 12 == 0) {
                    Prop pr;
                    pr.pos = QPointF(x * TILE + propRng() % TILE,
                                     y * TILE + propRng() % TILE);
                    pr.type = propRng() % 3;

                    switch (pr.type) {
                    case 0: // Flower
                        pr.color = QColor(
                            QList<QColor>{
                                QColor(255, 100, 100),
                                QColor(255, 255, 100),
                                QColor(200, 100, 255),
                                QColor(255, 180, 200)
                            }.at(propRng() % 4));
                        break;
                    case 1: // Rock
                        pr.color = QColor(140 + propRng()%30, 135 + propRng()%30, 130 + propRng()%30);
                        break;
                    case 2: // Grass tuft
                        pr.color = QColor(60 + propRng()%20, 120 + propRng()%30, 50);
                        break;
                    }
                    m_props.push_back(pr);
                }
            }
            else if (t == TerrainKind::Path) {
                // Occasional pebble on the path
                if (propRng() % 20 == 0) {
                    Prop pr;
                    pr.pos = QPointF(x * TILE + propRng() % TILE,
                                     y * TILE + propRng() % TILE);
                    pr.type = 1; // Rock
                    pr.color = QColor(170 + propRng()%20, 155 + propRng()%20, 120);
                    m_props.push_back(pr);
                }
            }
        }
    }
}

void GameWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    m_animTime += 0.016f;  // Increment ~60fps

    QPointF offset = cameraOffset();

    // --- LAYER 0-2: PARALLAX BACKGROUND ---
    // Sky gradient, clouds, distant hills
    drawParallaxBackground(painter, offset);

    // --- LAYER 3: FENCE EXTERIOR ---
    // Dark grass border, trees, bushes, flowers around the map edges
    drawFenceExterior(painter, offset);

    painter.save();
    painter.translate(offset);

    QRectF visibleRect(-offset.x(), -offset.y(), width(), height());

    // Clipped terrain blit
    QRect srcRect = visibleRect.toAlignedRect().intersected(
        QRect(0, 0, m_terrainCache.width(), m_terrainCache.height())
        );

    if (!srcRect.isEmpty()) {
        painter.drawPixmap(QPointF(srcRect.x(), srcRect.y()), m_terrainCache, srcRect);
    }

    // Draw leaf pile decorations (autumn)
    if (m_effects->season() == Season::Autumn) {
        m_effects->drawLeafPiles(painter);
    }

    // Decorative Props (flowers, rocks, tufts)
    QRectF propCull = visibleRect.adjusted(-20, -20, 20, 20);
    for (const auto& prop : m_props) {
        if (!propCull.contains(prop.pos)) continue;

        painter.setPen(Qt::NoPen);
        switch (prop.type) {
        case 0: { // Flower
            float bob = std::sin(m_animTime * 2.0f + prop.pos.x() * 0.1f) * 1.5f;
            QPointF fp = prop.pos + QPointF(0, bob);

            // Stem
            painter.setPen(QPen(QColor(60, 120, 40), 1));
            painter.drawLine(fp + QPointF(0, 3), fp + QPointF(0, 7));

            // Petals
            painter.setPen(Qt::NoPen);
            painter.setBrush(prop.color);
            painter.drawEllipse(fp, 3, 3);

            // Center
            painter.setBrush(QColor(255, 220, 50));
            painter.drawEllipse(fp, 1.5, 1.5);
            break;
        }
        case 1: { // Rock
            painter.setBrush(prop.color);
            painter.setPen(QPen(prop.color.darker(130), 0.5));
            painter.drawEllipse(prop.pos, 3, 2);
            break;
        }
        case 2: { // Grass tuft
            painter.setPen(QPen(prop.color, 1.5));
            painter.drawLine(prop.pos + QPointF(-2, 3), prop.pos + QPointF(-1, -3));
            painter.drawLine(prop.pos + QPointF(0, 3), prop.pos + QPointF(1, -4));
            painter.drawLine(prop.pos + QPointF(2, 3), prop.pos + QPointF(3, -2));
            break;
        }
        }
    }

    // Goal marker (pulsing)
    {
        float pulse = 10.f + std::sin(m_animTime * 3.0f) * 3.f;
        float glow = 0.5f + std::sin(m_animTime * 3.0f) * 0.3f;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 0, int(glow * 80)));
        painter.drawEllipse(m_world.goalPoint(), pulse + 6, pulse + 6);

        painter.setBrush(QColor(255, 220, 50));
        painter.setPen(QPen(QColor(200, 160, 0), 1.5));
        painter.drawEllipse(m_world.goalPoint(), pulse, pulse);
    }

    // Pickups (floating animation)
    for (const auto& pk : m_world.pickups()) {
        if (pk.taken) continue;

        float bob = std::sin(m_animTime * 3.5f + pk.id * 1.3f) * 3.0f;
        QPointF drawPos = pk.pos + QPointF(0, bob);

        // Glow under pickup
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 200, 40));
        painter.drawEllipse(pk.pos, 10, 6);

        painter.setPen(QPen(Qt::black, 1));

        switch (pk.kind) {
        case PickupKind::Key: {
            // Rotating shimmer
            float shimmer = 0.7f + std::sin(m_animTime * 5.0f + pk.id) * 0.3f;
            painter.setBrush(QColor(0, int(255 * shimmer), 255));
            QPolygonF diamond;
            diamond << drawPos + QPointF(0, -9)
                    << drawPos + QPointF(7, 0)
                    << drawPos + QPointF(0, 9)
                    << drawPos + QPointF(-7, 0);
            painter.drawPolygon(diamond);

            // Inner highlight
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 100));
            painter.drawEllipse(drawPos + QPointF(-1, -2), 2, 3);
            break;
        }
        case PickupKind::Carrot: {
            painter.setBrush(QColor(255, 140, 0));
            QPolygonF carrot;
            carrot << drawPos + QPointF(-4, -5)
                   << drawPos + QPointF(4, -5)
                   << drawPos + QPointF(1, 9)
                   << drawPos + QPointF(-1, 9);
            painter.drawPolygon(carrot);

            // Green top
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(50, 160, 50));
            painter.drawEllipse(drawPos + QPointF(0, -7), 5, 3);
            break;
        }
        case PickupKind::Egg: {
            painter.setBrush(QColor(255, 250, 230));
            painter.drawEllipse(drawPos, 5, 7);

            // Subtle speckles
            painter.setPen(QColor(200, 180, 150));
            painter.drawPoint(drawPos + QPointF(-2, -1));
            painter.drawPoint(drawPos + QPointF(1, 2));
            painter.drawPoint(drawPos + QPointF(2, -3));
            break;
        }
        case PickupKind::Milk: {
            // Bottle body
            painter.setBrush(Qt::white);
            painter.drawRect(QRectF(drawPos.x() - 4, drawPos.y() - 5, 8, 12));

            // Cap
            painter.setBrush(QColor(50, 120, 200));
            painter.drawRect(QRectF(drawPos.x() - 3, drawPos.y() - 8, 6, 4));

            // Label
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(200, 230, 255));
            painter.drawRect(QRectF(drawPos.x() - 3, drawPos.y() - 1, 6, 4));
            break;
        }
        }
    }

    // Projectiles (with trail)
    for (const auto& pr : m_world.projectiles()) {
        // Trail
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 200, 60));
        painter.drawEllipse(pr.pos - pr.vel * 0.02, 3, 3);
        painter.setBrush(QColor(255, 255, 200, 30));
        painter.drawEllipse(pr.pos - pr.vel * 0.04, 2, 2);

        // Main projectile
        painter.setBrush(QColor(255, 255, 220));
        painter.setPen(QPen(QColor(200, 180, 100), 1));
        painter.drawEllipse(pr.pos, 4, 4);
    }

    // Enemies
    QRectF cullRect = visibleRect.adjusted(-50, -50, 50, 50);
    for (const auto& e : m_world.enemies()) {
        if (!cullRect.contains(e.pos)) continue;

        // Movement bob
        float speed = std::hypot(e.vel.x(), e.vel.y());
        float bob = (speed > 10.f) ? std::sin(m_animTime * 8.0f + e.id) * 2.0f : 0.0f;
        QPointF drawPos = e.pos + QPointF(0, bob);

        // Facing angle for directional features
        float angle = std::atan2(e.vel.y(), e.vel.x());

        switch (e.kind) {
        case EnemyKind::Fox: {
            // Shadow
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawEllipse(e.pos + QPointF(1, 4), 12, 6);

            // Body (elongated)
            painter.setBrush(QColor(220, 120, 30));
            painter.setPen(QPen(QColor(180, 90, 10), 1));
            painter.drawEllipse(drawPos, 11, 8);

            // Snout
            QPointF snout = drawPos + QPointF(std::cos(angle) * 10, std::sin(angle) * 10);
            painter.setBrush(QColor(240, 160, 60));
            painter.drawEllipse(snout, 5, 4);

            // Nose
            painter.setBrush(Qt::black);
            painter.setPen(Qt::NoPen);
            QPointF nose = drawPos + QPointF(std::cos(angle) * 14, std::sin(angle) * 14);
            painter.drawEllipse(nose, 2, 2);

            // Ears
            painter.setBrush(QColor(220, 120, 30));
            painter.setPen(QPen(Qt::black, 0.5));
            QPointF earL = drawPos + QPointF(std::cos(angle - 2.0) * 8, std::sin(angle - 2.0) * 8);
            QPointF earR = drawPos + QPointF(std::cos(angle + 2.0) * 8, std::sin(angle + 2.0) * 8);
            painter.drawEllipse(earL, 4, 5);
            painter.drawEllipse(earR, 4, 5);

            // Tail
            QPointF tail = drawPos - QPointF(std::cos(angle) * 14, std::sin(angle) * 14);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 200, 150));
            painter.drawEllipse(tail, 5, 4);
            break;
        }
        case EnemyKind::Pig: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawEllipse(e.pos + QPointF(1, 4), 11, 6);

            // Round pink body
            painter.setBrush(QColor(255, 180, 180));
            painter.setPen(QPen(QColor(220, 140, 140), 1));
            painter.drawEllipse(drawPos, 10, 9);

            // Snout
            QPointF snout = drawPos + QPointF(std::cos(angle) * 9, std::sin(angle) * 9);
            painter.setBrush(QColor(255, 150, 150));
            painter.drawEllipse(snout, 5, 4);

            // Nostrils
            painter.setBrush(QColor(200, 100, 100));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(snout + QPointF(-1.5, 0), 1.5, 1.5);
            painter.drawEllipse(snout + QPointF(1.5, 0), 1.5, 1.5);

            // Ears
            painter.setBrush(QColor(255, 160, 160));
            painter.setPen(QPen(QColor(200, 120, 120), 0.5));
            QPointF earL = drawPos + QPointF(std::cos(angle - 1.8) * 8, std::sin(angle - 1.8) * 8 - 3);
            QPointF earR = drawPos + QPointF(std::cos(angle + 1.8) * 8, std::sin(angle + 1.8) * 8 - 3);
            painter.drawEllipse(earL, 4, 5);
            painter.drawEllipse(earR, 4, 5);
            break;
        }
        case EnemyKind::Cow: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawEllipse(e.pos + QPointF(2, 5), 16, 8);

            // Large white body
            painter.setBrush(Qt::white);
            painter.setPen(QPen(QColor(180, 180, 180), 1));
            painter.drawEllipse(drawPos, 16, 12);

            // Black spots
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(40, 40, 40));
            painter.drawEllipse(drawPos + QPointF(-5, -3), 5, 4);
            painter.drawEllipse(drawPos + QPointF(4, 2), 6, 4);
            painter.drawEllipse(drawPos + QPointF(-2, 5), 4, 3);

            // Head
            QPointF head = drawPos + QPointF(std::cos(angle) * 14, std::sin(angle) * 14);
            painter.setBrush(Qt::white);
            painter.setPen(QPen(QColor(180, 180, 180), 1));
            painter.drawEllipse(head, 7, 6);

            // Muzzle
            painter.setBrush(QColor(255, 200, 180));
            QPointF muzzle = head + QPointF(std::cos(angle) * 5, std::sin(angle) * 5);
            painter.drawEllipse(muzzle, 4, 3);

            // Eyes
            painter.setBrush(Qt::black);
            painter.setPen(Qt::NoPen);
            QPointF eyeL = head + QPointF(std::cos(angle - 1.2) * 4, std::sin(angle - 1.2) * 4);
            QPointF eyeR = head + QPointF(std::cos(angle + 1.2) * 4, std::sin(angle + 1.2) * 4);
            painter.drawEllipse(eyeL, 1.5, 1.5);
            painter.drawEllipse(eyeR, 1.5, 1.5);
            break;
        }
        case EnemyKind::Sheep: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawEllipse(e.pos + QPointF(1, 4), 12, 6);

            // Fluffy wool body (bumpy outline via overlapping circles)
            painter.setBrush(QColor(240, 240, 235));
            painter.setPen(Qt::NoPen);
            for (int i = 0; i < 8; ++i) {
                float a = i * M_PI / 4.0f;
                QPointF bump = drawPos + QPointF(std::cos(a) * 6, std::sin(a) * 6);
                painter.drawEllipse(bump, 6, 6);
            }
            painter.drawEllipse(drawPos, 8, 8);

            // Dark face
            QPointF face = drawPos + QPointF(std::cos(angle) * 10, std::sin(angle) * 10);
            painter.setBrush(QColor(60, 50, 50));
            painter.setPen(QPen(QColor(40, 35, 35), 1));
            painter.drawEllipse(face, 5, 5);

            // Eyes
            painter.setBrush(Qt::white);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(face + QPointF(-2, -1), 1.5, 1.5);
            painter.drawEllipse(face + QPointF(2, -1), 1.5, 1.5);
            break;
        }
        case EnemyKind::Crow: {
            // Wing flap animation
            float flap = std::sin(m_animTime * 12.0f + e.id) * 4.0f;

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 20));
            painter.drawEllipse(e.pos + QPointF(2, 8), 10, 4);

            // Wings
            painter.setBrush(QColor(30, 30, 30));
            painter.drawEllipse(drawPos + QPointF(-6, flap - 2), 8, 4);
            painter.drawEllipse(drawPos + QPointF(6, flap - 2), 8, 4);

            // Body
            painter.setBrush(QColor(50, 50, 50));
            painter.setPen(QPen(QColor(30, 30, 30), 1));
            painter.drawEllipse(drawPos, 6, 7);

            // Beak
            painter.setBrush(QColor(255, 180, 0));
            QPointF beak = drawPos + QPointF(std::cos(angle) * 7, std::sin(angle) * 7);
            painter.drawEllipse(beak, 3, 2);

            // Eyes
            painter.setBrush(QColor(200, 0, 0));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(drawPos + QPointF(std::cos(angle - 0.8) * 4,
                                                  std::sin(angle - 0.8) * 4), 1.5, 1.5);
            break;
        }
        case EnemyKind::Goose: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 25));
            painter.drawEllipse(e.pos + QPointF(1, 4), 11, 6);

            // Body
            painter.setBrush(Qt::white);
            painter.setPen(QPen(QColor(200, 200, 200), 1));
            painter.drawEllipse(drawPos, 10, 9);

            // Long neck
            QPointF neck = drawPos + QPointF(std::cos(angle) * 12, std::sin(angle) * 12);
            painter.setPen(QPen(Qt::white, 5));
            painter.drawLine(drawPos + QPointF(std::cos(angle)*6, std::sin(angle)*6), neck);

            // Head
            painter.setPen(QPen(QColor(200,200,200), 1));
            painter.setBrush(Qt::white);
            painter.drawEllipse(neck, 5, 4);

            // Orange beak
            painter.setBrush(QColor(255, 160, 30));
            painter.setPen(Qt::NoPen);
            QPointF beak = neck + QPointF(std::cos(angle) * 6, std::sin(angle) * 6);
            painter.drawEllipse(beak, 4, 2);

            // Angry eye
            painter.setBrush(Qt::black);
            QPointF eye = neck + QPointF(std::cos(angle - 0.6) * 3, std::sin(angle - 0.6) * 3 - 1);
            painter.drawEllipse(eye, 1.5, 1.5);
            break;
        }
        case EnemyKind::Bull: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 35));
            painter.drawEllipse(e.pos + QPointF(2, 5), 16, 8);

            // Massive body
            painter.setBrush(QColor(100, 50, 20));
            painter.setPen(QPen(QColor(70, 35, 10), 1.5));
            painter.drawEllipse(drawPos, 15, 12);

            // Muscle highlight
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(130, 70, 30, 80));
            painter.drawEllipse(drawPos + QPointF(-3, -2), 8, 6);

            // Head
            QPointF head = drawPos + QPointF(std::cos(angle) * 14, std::sin(angle) * 14);
            painter.setBrush(QColor(80, 40, 15));
            painter.setPen(QPen(QColor(60, 30, 10), 1));
            painter.drawEllipse(head, 8, 7);

            // Horns
            painter.setPen(QPen(QColor(220, 210, 180), 2.5));
            QPointF hornL = head + QPointF(std::cos(angle - 1.3) * 9, std::sin(angle - 1.3) * 9 - 3);
            QPointF hornR = head + QPointF(std::cos(angle + 1.3) * 9, std::sin(angle + 1.3) * 9 - 3);
            painter.drawLine(head + QPointF(std::cos(angle - 1.3) * 5, std::sin(angle - 1.3) * 5), hornL);
            painter.drawLine(head + QPointF(std::cos(angle + 1.3) * 5, std::sin(angle + 1.3) * 5), hornR);

            // Nostrils (angry snort)
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(40, 20, 10));
            QPointF snout = head + QPointF(std::cos(angle) * 6, std::sin(angle) * 6);
            painter.drawEllipse(snout + QPointF(-2, 0), 2, 2);
            painter.drawEllipse(snout + QPointF(2, 0), 2, 2);

            // Red eyes
            painter.setBrush(QColor(200, 30, 30));
            QPointF eyeL = head + QPointF(std::cos(angle - 0.8) * 5, std::sin(angle - 0.8) * 5 - 1);
            QPointF eyeR = head + QPointF(std::cos(angle + 0.8) * 5, std::sin(angle + 0.8) * 5 - 1);
            painter.drawEllipse(eyeL, 2, 2);
            painter.drawEllipse(eyeR, 2, 2);
            break;
        }
        case EnemyKind::Rooster: {
            // Shadow
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 30));
            painter.drawEllipse(e.pos + QPointF(1, 4), 11, 6);

            // Tail feathers (drawn first, behind body)
            QPointF tailBase = drawPos - QPointF(std::cos(angle) * 8, std::sin(angle) * 8);
            painter.setBrush(QColor(30, 80, 30));
            for (int i = -2; i <= 2; ++i) {
                float tailAngle = angle + M_PI + i * 0.3f;
                float tailLen = 10.f + std::abs(i) * 2.f;
                QPointF featherTip = tailBase + QPointF(
                                         std::cos(tailAngle) * tailLen,
                                         std::sin(tailAngle) * tailLen
                                         );
                QPolygonF feather;
                feather << tailBase + QPointF(std::cos(tailAngle + 0.3) * 3,
                                              std::sin(tailAngle + 0.3) * 3)
                        << featherTip
                        << tailBase + QPointF(std::cos(tailAngle - 0.3) * 3,
                                              std::sin(tailAngle - 0.3) * 3);
                painter.drawPolygon(feather);
            }

            // Body
            painter.setBrush(QColor(160, 80, 30));
            painter.setPen(QPen(QColor(120, 55, 15), 1));
            painter.drawEllipse(drawPos, 10, 9);

            // Breast feathers
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(180, 100, 40, 100));
            painter.drawEllipse(drawPos + QPointF(std::cos(angle) * 3, std::sin(angle) * 3),
                                7, 6);

            // Head
            QPointF head = drawPos + QPointF(std::cos(angle) * 10, std::sin(angle) * 10);
            painter.setBrush(QColor(160, 80, 30));
            painter.setPen(QPen(QColor(120, 55, 15), 1));
            painter.drawEllipse(head, 5, 5);

            // Comb (big red, on top of head)
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(220, 30, 30));
            QPolygonF comb;
            QPointF combBase = head + QPointF(0, -5);
            comb << combBase + QPointF(-4, 0)
                 << combBase + QPointF(-2, -6)
                 << combBase + QPointF(0, -3)
                 << combBase + QPointF(2, -7)
                 << combBase + QPointF(4, -2)
                 << combBase + QPointF(4, 0);
            painter.drawPolygon(comb);

            // Wattle (hanging red bit under beak)
            painter.setBrush(QColor(200, 30, 30));
            QPointF wattlePos = head + QPointF(std::cos(angle) * 5, std::sin(angle) * 5 + 3);
            painter.drawEllipse(wattlePos, 2.5, 3.5);

            // Beak
            painter.setBrush(QColor(255, 200, 50));
            QPointF beak = head + QPointF(std::cos(angle) * 6, std::sin(angle) * 6);
            QPolygonF beakShape;
            beakShape << beak
                      << head + QPointF(std::cos(angle - 0.4) * 4, std::sin(angle - 0.4) * 4)
                      << head + QPointF(std::cos(angle + 0.4) * 4, std::sin(angle + 0.4) * 4);
            painter.drawPolygon(beakShape);

            // Eye (angry)
            painter.setBrush(QColor(200, 50, 0));
            QPointF eyePos = head + QPointF(std::cos(angle - 0.7) * 3,
                                            std::sin(angle - 0.7) * 3 - 1);
            painter.drawEllipse(eyePos, 1.5, 1.5);

            // Pupil
            painter.setBrush(Qt::black);
            painter.drawEllipse(eyePos, 0.8, 0.8);

            // Legs (simple lines when moving)
            if (speed > 10.f) {
                float legPhase = std::sin(m_animTime * 12.0f + e.id) * 4.0f;
                painter.setPen(QPen(QColor(200, 160, 50), 1.5));
                painter.drawLine(drawPos + QPointF(-2, 8),
                                 drawPos + QPointF(-2 + legPhase, 14));
                painter.drawLine(drawPos + QPointF(2, 8),
                                 drawPos + QPointF(2 - legPhase, 14));
            }
            break;
        }
        default: { // Chicken fallback
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 25));
            painter.drawEllipse(e.pos + QPointF(1, 3), 9, 5);

            painter.setBrush(Qt::white);
            painter.setPen(QPen(QColor(200, 200, 200), 1));
            painter.drawEllipse(drawPos, 8, 8);

            // Beak
            painter.setBrush(QColor(255, 180, 0));
            painter.setPen(Qt::NoPen);
            QPointF beak = drawPos + QPointF(std::cos(angle) * 8, std::sin(angle) * 8);
            painter.drawEllipse(beak, 3, 2);

            // Comb
            painter.setBrush(Qt::red);
            painter.drawEllipse(drawPos + QPointF(0, -7), 3, 4);
            break;
        }
        }
    }

    // Friendlies
    for (const auto& f : m_world.friendlies()) {
        if (!cullRect.contains(f.pos)) continue;

        switch (f.kind) {
        case FriendlyKind::Goldfish: {
            float swimBob = std::sin(m_animTime * 4.0f + f.id * 2.1f) * 2.0f;
            float tailWag = std::sin(m_animTime * 8.0f + f.id) * 15.0f * (M_PI / 180.0f);
            QPointF drawPos = f.pos + QPointF(0, swimBob);

            float angle = std::atan2(f.vel.y(), f.vel.x());
            if (std::hypot(f.vel.x(), f.vel.y()) < 1.0f) {
                angle = std::sin(m_animTime * 0.5f + f.id) * 0.3f;  // Gentle idle sway
            }

            // Water ripple underneath
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 25));
            float ripple = 5.0f + std::sin(m_animTime * 3.0f + f.id) * 2.0f;
            painter.drawEllipse(f.pos, ripple, ripple * 0.5f);

            // Tail fin
            QPointF tailBase = drawPos - QPointF(std::cos(angle) * 6, std::sin(angle) * 6);
            QPointF tailTip = tailBase - QPointF(std::cos(angle + tailWag) * 5,
                                                 std::sin(angle + tailWag) * 5);
            painter.setBrush(QColor(255, 140, 0, 180));
            QPolygonF tail;
            tail << tailBase + QPointF(0, -2)
                 << tailTip
                 << tailBase + QPointF(0, 2);
            painter.drawPolygon(tail);

            // Body
            painter.setBrush(QColor(255, 165, 30));
            painter.setPen(QPen(QColor(220, 120, 0), 0.5));
            painter.drawEllipse(drawPos, 5, 3);

            // Lighter belly
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 200, 100, 120));
            painter.drawEllipse(drawPos + QPointF(0, 1), 3, 1.5);

            // Eye
            painter.setBrush(Qt::black);
            QPointF eyePos = drawPos + QPointF(std::cos(angle) * 3, std::sin(angle) * 3 - 1);
            painter.drawEllipse(eyePos, 1, 1);

            // Dorsal fin
            painter.setBrush(QColor(255, 120, 0, 160));
            QPolygonF dorsal;
            dorsal << drawPos + QPointF(-1, -3)
                   << drawPos + QPointF(1, -5)
                   << drawPos + QPointF(3, -3);
            painter.drawPolygon(dorsal);
            break;
        }

        default: {
            // Original friendly rendering (farm animal circles)
            float bob = std::sin(m_animTime * 2.0f + f.id) * 1.0f;
            QPointF drawPos = f.pos + QPointF(0, bob);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 25));
            painter.drawEllipse(f.pos + QPointF(1, 3), 8, 4);

            painter.setBrush(QColor(220, 220, 210));
            painter.setPen(QPen(QColor(180, 180, 170), 1));
            painter.drawEllipse(drawPos, 7, 7);

            // Gentle face
            float angle = std::atan2(f.vel.y(), f.vel.x());
            if (std::hypot(f.vel.x(), f.vel.y()) < 1.0f) angle = 0;

            painter.setBrush(Qt::black);
            painter.setPen(Qt::NoPen);
            QPointF eyeL = drawPos + QPointF(std::cos(angle - 0.6) * 4,
                                             std::sin(angle - 0.6) * 4);
            QPointF eyeR = drawPos + QPointF(std::cos(angle + 0.6) * 4,
                                             std::sin(angle + 0.6) * 4);
            painter.drawEllipse(eyeL, 1, 1);
            painter.drawEllipse(eyeR, 1, 1);
            break;
        }
        }
    }

    // Players
    for (const auto& p : m_world.players()) {
        if (!p.alive) continue;
        if (p.invulTicks > 0 && (p.invulTicks / 4) % 2 == 0) continue;

        float playerBob = (std::hypot(p.vel.x(), p.vel.y()) > 10.f)
                              ? std::sin(m_animTime * 10.0f) * 1.5f : 0.0f;
        QPointF drawPos = p.pos + QPointF(0, playerBob);
        bool isLocal = (p.id == m_localPlayerId);

        // Shadow
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 35));
        painter.drawEllipse(p.pos + QPointF(1, 5), 12, 6);

        // Stun effect - yellow splat ring
        if (p.stunTicks > 0) {
            float stunPulse = std::sin(m_animTime * 15.0f) * 0.3f + 0.7f;
            painter.setBrush(QColor(255, 255, 0, int(stunPulse * 100)));
            painter.drawEllipse(drawPos, 16, 16);

            // Dizzy stars
            for (int s = 0; s < 3; ++s) {
                float starAngle = m_animTime * 5.0f + s * (2.0f * M_PI / 3.0f);
                float starDist = 14.0f + std::sin(m_animTime * 3.0f + s) * 2.0f;
                QPointF starPos = drawPos + QPointF(
                                      std::cos(starAngle) * starDist,
                                      std::sin(starAngle) * starDist - 8
                                      );
                painter.setBrush(QColor(255, 255, 100));
                painter.setPen(Qt::NoPen);

                // Small 4-point star shape
                QPolygonF star;
                float sr = 3.0f;
                for (int i = 0; i < 8; ++i) {
                    float a = i * M_PI / 4.0f;
                    float r = (i % 2 == 0) ? sr : sr * 0.4f;
                    star << starPos + QPointF(std::cos(a) * r, std::sin(a) * r);
                }
                painter.drawPolygon(star);
            }
        }

        // Body
        QColor bodyColor = isLocal ? QColor(60, 100, 220) : QColor(200, 60, 200);
        painter.setBrush(bodyColor);
        painter.setPen(QPen(bodyColor.darker(140), 1.5));
        painter.drawEllipse(drawPos, 11, 11);

        // Overalls
        painter.setPen(Qt::NoPen);
        painter.setBrush(bodyColor.lighter(130));
        painter.drawRect(QRectF(drawPos.x() - 5, drawPos.y() + 2, 10, 6));

        // Face direction indicator
        static const QPointF faceDirs[4] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
        QPointF faceDir = faceDirs[p.facing];
        QPointF eyeCenter = drawPos + faceDir * 5;

        // Eyes
        if (p.stunTicks > 0) {
            painter.setPen(QPen(Qt::black, 1.5));
            painter.setBrush(Qt::NoBrush);
            float spin = m_animTime * 10.0f;
            // X eyes when stunned
            QPointF le = eyeCenter + QPointF(-2.5, -2);
            QPointF re = eyeCenter + QPointF(2.5, -2);
            painter.drawLine(le + QPointF(-2, -2), le + QPointF(2, 2));
            painter.drawLine(le + QPointF(2, -2), le + QPointF(-2, 2));
            painter.drawLine(re + QPointF(-2, -2), re + QPointF(2, 2));
            painter.drawLine(re + QPointF(2, -2), re + QPointF(-2, 2));
        } else {
            painter.setBrush(Qt::white);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(eyeCenter + QPointF(-2.5, -2), 3, 3);
            painter.drawEllipse(eyeCenter + QPointF(2.5, -2), 3, 3);
            painter.setBrush(Qt::black);
            painter.drawEllipse(eyeCenter + faceDir * 1.5 + QPointF(-2.5, -2), 1.5, 1.5);
            painter.drawEllipse(eyeCenter + faceDir * 1.5 + QPointF(2.5, -2), 1.5, 1.5);
        }

        // Hat
        painter.setBrush(QColor(180, 140, 60));
        painter.setPen(QPen(QColor(140, 100, 40), 1));
        painter.drawEllipse(drawPos + QPointF(0, -10), 9, 3);
        painter.drawRect(QRectF(drawPos.x() - 5, drawPos.y() - 16, 10, 7));

        // Speed boost effect
        if (p.boostTicks > 0) {
            painter.setPen(Qt::NoPen);
            float alpha = 40.f + std::sin(m_animTime * 8.0f) * 20.f;
            painter.setBrush(QColor(255, 140, 0, int(alpha)));
            painter.drawEllipse(drawPos, 16, 16);
        }

        // Name tag
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 8, QFont::Bold));
        QRectF nameRect(drawPos.x() - 40, drawPos.y() - 28, 80, 14);
        painter.drawText(nameRect, Qt::AlignCenter, p.name);
    }

    // World-space weather effects (puddles)
    m_effects->drawWorldEffects(painter, -offset, width(), height(), m_animTime);

    // Particles
    updateAndDrawParticles(painter, 0.016f);

    painter.restore();

    // --- 2. HUD ---
    auto* lp = m_world.player(m_localPlayerId);

    painter.fillRect(0, 0, width(), 40, QColor(0, 0, 0, 180));

    if (lp) {
        // Health bar
        int hbX = 20, hbY = 12, hbW = 120, hbH = 16;
        float hpPct = qBound(0.0f, lp->hp / lp->maxHp, 1.0f);

        painter.setBrush(QColor(60, 0, 0));
        painter.setPen(Qt::NoPen);
        painter.drawRect(hbX, hbY, hbW, hbH);

        QColor hpColor = (hpPct < 0.4f) ? Qt::red : (hpPct < 0.7f ? Qt::yellow : Qt::green);
        painter.setBrush(hpColor);
        painter.drawRect(hbX, hbY, int(hbW * hpPct), hbH);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawRect(hbX, hbY, hbW, hbH);

        painter.setFont(QFont("Arial", 8, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(hbX, hbY, hbW, hbH, Qt::AlignCenter,
                         QString("%1 / %2").arg(int(lp->hp)).arg(int(lp->maxHp)));

        // Biome tracking
        int newBiome = m_world.getRegionIndex(lp->pos);
        if (newBiome != m_currentBiome && newBiome >= 0) {
            m_currentBiome = newBiome;
            m_biomeFlashTimer = 3.0f;

            BiomeType types[] = {
                BiomeType::Grassy, BiomeType::Muddy, BiomeType::Watery,
                BiomeType::Crops, BiomeType::Coop, BiomeType::Field
            };
            if (newBiome < 6) {
                m_biomeFlashName = BiomeManager::getBiomeName(types[newBiome]);
            }
        }

        if (m_biomeFlashTimer > 0) {
            m_biomeFlashTimer -= 0.016f;
        }

        // Stats
        qint64 displayTime = m_finished ? m_finishMs : m_world.elapsedMs();
        int currentBiome = m_world.getRegionIndex(lp->pos);
        int req = (currentBiome >= 5) ? 0 : 1;  // Last biome (Field) needs no key

        QString hud = QString("TIME: %1.%2s   |   DEATHS: %3   |   EGGS: %4   |   KEYS: %5/%6")
                          .arg(displayTime / 1000)
                          .arg((displayTime % 1000) / 10, 2, 10, QChar('0'))
                          .arg(lp->deaths)
                          .arg(lp->eggs)
                          .arg(lp->keysHeld).arg(req);

        int textX = hbX + hbW + 20;
        painter.setFont(QFont("Arial", 11, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(textX, 26, hud);

        // Biome name display (right side of HUD)
        QString biomeName = m_biomeFlashName.isEmpty() ? "---" : m_biomeFlashName;

        // Flash effect: bright on entry, fades to dim
        QColor biomeTextColor;
        if (m_biomeFlashTimer > 2.0f) {
            // Bright flash phase
            float t = (m_biomeFlashTimer - 2.0f) / 1.0f;
            biomeTextColor = QColor(
                255,
                int(230 + t * 25),
                int(120 + t * 135)
                );
        } else if (m_biomeFlashTimer > 0.0f) {
            // Fade from bright to settled
            float t = m_biomeFlashTimer / 2.0f;
            biomeTextColor = QColor(
                int(180 + t * 75),
                int(180 + t * 50),
                int(160 + t * -40)
                );
        } else {
            // Settled dim color
            biomeTextColor = QColor(180, 180, 160);
        }

        // Background pill behind biome name
        int pillW = 140;
        int pillX = width() - pillW - 15;
        int pillY = 8;
        int pillH = 24;

        // Pill background - slightly brighter during flash
        int bgAlpha = (m_biomeFlashTimer > 0) ? 100 : 60;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, bgAlpha));
        painter.drawRoundedRect(pillX, pillY, pillW, pillH, 12, 12);

        // Pill border during flash
        if (m_biomeFlashTimer > 1.5f) {
            float borderAlpha = qBound(0.0f, (m_biomeFlashTimer - 1.5f) / 1.5f, 1.0f);
            painter.setPen(QPen(QColor(255, 220, 80, int(borderAlpha * 150)), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(pillX, pillY, pillW, pillH, 12, 12);
        }

        // Biome name text
        painter.setPen(biomeTextColor);
        if (m_biomeFlashTimer > 2.0f) {
            painter.setFont(QFont("Arial", 12, QFont::Bold));
        } else {
            painter.setFont(QFont("Arial", 10, QFont::Bold));
        }
        painter.drawText(pillX, pillY, pillW, pillH, Qt::AlignCenter, biomeName);

        // Natural effects info (below HUD bar)
        m_effects->drawHudInfo(painter, hbX, 50);
    }

    // Weather/time overlay (drawn over everything except HUD)
    m_effects->drawOverlay(painter, width(), height(), m_animTime);

    // --- MINIMAP ---
    m_minimap->draw(painter, m_world, m_localPlayerId, width(), height(), m_animTime);

    // --- 3. LEADERBOARD ---
    if (m_showLeaderboard) {
        int boardW = 250;
        int boardX = width() - boardW - 20;
        int boardY = 50;

        auto activePlayers = m_world.players();
        auto topScores = Leaderboard::load();

        int rowH = 25, padding = 10;
        int totalH = int((activePlayers.size() + qMin(5, int(topScores.size()))) * rowH) + 60;

        painter.setBrush(QColor(0, 0, 0, 160));
        painter.setPen(QPen(Qt::cyan, 1));
        painter.drawRoundedRect(boardX, boardY, boardW, totalH, 5, 5);

        int currentY = boardY + 25;
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.setPen(Qt::cyan);
        painter.drawText(boardX + padding, currentY, "ACTIVE PLAYERS");

        painter.setFont(QFont("Arial", 9));
        painter.setPen(Qt::white);
        currentY += rowH;
        for (const auto& p : activePlayers) {
            QString status = (p.finishMs >= 0)
            ? QString("%1.%2s").arg(p.finishMs/1000).arg((p.finishMs%1000)/10, 2, 10, QChar('0'))
            : "Racing...";
            painter.drawText(boardX + padding, currentY, p.name);
            painter.drawText(boardX + boardW - 70, currentY, status);
            currentY += rowH;
        }

        currentY += 10;
        painter.setPen(Qt::yellow);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.drawText(boardX + padding, currentY, "ALL-TIME BEST");

        painter.setFont(QFont("Arial", 9));
        painter.setPen(QColor(200, 200, 200));
        currentY += rowH;
        for (int i = 0; i < qMin(5, int(topScores.size())); ++i) {
            qint64 ms = topScores[i].second;
            QString timeStr = QString("%1.%2s").arg(ms/1000).arg((ms%1000)/10, 2, 10, QChar('0'));
            painter.drawText(boardX + padding, currentY,
                             QString("%1. %2").arg(i+1).arg(topScores[i].first));
            painter.drawText(boardX + boardW - 70, currentY, timeStr);
            currentY += rowH;
        }
    }

    // --- VIGNETTE OVERLAY ---
    // Subtle darkening at screen edges for atmosphere
    {
        // Top
        QLinearGradient vigTop(0, 0, 0, 80);
        vigTop.setColorAt(0.0, QColor(0, 0, 0, 50));
        vigTop.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(0, 40, width(), 80, vigTop);  // Start below HUD

        // Bottom
        QLinearGradient vigBot(0, height() - 60, 0, height());
        vigBot.setColorAt(0.0, QColor(0, 0, 0, 0));
        vigBot.setColorAt(1.0, QColor(0, 0, 0, 60));
        painter.fillRect(0, height() - 60, width(), 60, vigBot);

        // Left
        QLinearGradient vigLeft(0, 0, 50, 0);
        vigLeft.setColorAt(0.0, QColor(0, 0, 0, 40));
        vigLeft.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(0, 40, 50, height() - 40, vigLeft);

        // Right
        QLinearGradient vigRight(width() - 50, 0, width(), 0);
        vigRight.setColorAt(0.0, QColor(0, 0, 0, 0));
        vigRight.setColorAt(1.0, QColor(0, 0, 0, 40));
        painter.fillRect(width() - 50, 40, 50, height() - 40, vigRight);
    }

    // --- 4. RESULTS OVERLAY ---
    if (m_showResultsMenu) {
        painter.fillRect(rect(), QColor(0, 0, 0, 200));

        int centerX = width() / 2;
        int centerY = height() / 2;

        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 30, QFont::Bold));
        painter.drawText(0, centerY - 100, width(), 50, Qt::AlignCenter, "COURSE COMPLETE!");

        painter.setFont(QFont("Arial", 18));
        painter.drawText(0, centerY - 40, width(), 40, Qt::AlignCenter,
                         QString("Final Time: %1.%2s")
                             .arg(m_finishMs/1000).arg((m_finishMs%1000)/10, 2, 10, QChar('0')));

        int btnW = 300, btnH = 50;
        m_retryRect = QRect(centerX - btnW/2, centerY + 40, btnW, btnH);
        m_menuRect  = QRect(centerX - btnW/2, centerY + 110, btnW, btnH);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(46, 204, 113));
        painter.drawRoundedRect(m_retryRect, 10, 10);
        painter.setBrush(QColor(231, 76, 60));
        painter.drawRoundedRect(m_menuRect, 10, 10);

        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(m_retryRect, Qt::AlignCenter, "RETRY (R)");
        painter.drawText(m_menuRect, Qt::AlignCenter, "MAIN MENU (M)");
    }
}

void GameWidget::keyPressEvent(QKeyEvent* e) {
    m_keys.insert(e->key());

    if (m_showResultsMenu) {
        if (e->key() == Qt::Key_R) { // Shortcut for Retry
            m_showResultsMenu = false;
            if (m_role == Role::SinglePlayer) startSinglePlayer(m_playerName);
        }
        if (e->key() == Qt::Key_M) { // Shortcut for Menu
            emit exitToMenu();
        }
    }

    if (e->key() == Qt::Key_Tab) m_showLeaderboard = true;
    if (e->key() == Qt::Key_Escape) emit exitToMenu();
    if (e->key() == Qt::Key_J) doShoot();
}

void GameWidget::keyReleaseEvent(QKeyEvent* e) {
    m_keys.remove(e->key());
    if (e->key() == Qt::Key_Tab) m_showLeaderboard = false;
    update();
}

void GameWidget::mousePressEvent(QMouseEvent* e) {
    if (m_showResultsMenu) {
        if (m_retryRect.contains(e->pos())) {
            m_showResultsMenu = false;
            // Simply restart the current mode
            if (m_role == Role::SinglePlayer) startSinglePlayer(m_playerName);
            else if (m_role == Role::Host) startAsHost(m_net, m_playerName);
            // (Clients usually have to wait for Host to restart)
        }
        else if (m_menuRect.contains(e->pos())) {
            emit exitToMenu();
        }
        return; // Don't shoot while menu is up
    }

    if (e->button() == Qt::LeftButton) {
        m_mousePendingShoot = true;
        doShoot();
    }
}

void GameWidget::doShoot() {
    auto* p = m_world.player(m_localPlayerId);
    // Only play sound if we actually have eggs and aren't on cooldown
    if (p && p->eggs > 0 && p->shootCooldown <= 0) {
        m_audio->play(Audio::Shoot, 0.5f);
    }
}

void GameWidget::spawnDeathBurst(QPointF pos, QColor color, int count) {
    std::mt19937 rng(QDateTime::currentMSecsSinceEpoch());
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = pos;

        float angle = (rng() % 360) * M_PI / 180.0f;
        float speed = 80.0f + (rng() % 200);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);

        p.maxLife = 0.4f + (rng() % 40) / 100.0f;  // 0.4 - 0.8 seconds
        p.life = p.maxLife;
        p.size = 2.0f + (rng() % 4);

        // Vary color slightly
        int rv = rng() % 30 - 15;
        p.color = QColor(
            qBound(0, color.red() + rv, 255),
            qBound(0, color.green() + rv, 255),
            qBound(0, color.blue() + rv, 255)
            );

        m_particles.push_back(p);
    }
}

void GameWidget::spawnEnemyPoof(QPointF pos, QColor color) {
    std::mt19937 rng(QDateTime::currentMSecsSinceEpoch());
    for (int i = 0; i < 12; ++i) {
        Particle p;
        p.pos = pos + QPointF((rng() % 10) - 5, (rng() % 10) - 5);

        float angle = (rng() % 360) * M_PI / 180.0f;
        float speed = 30.0f + (rng() % 80);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed - 40.0f);  // Drift upward

        p.maxLife = 0.3f + (rng() % 30) / 100.0f;
        p.life = p.maxLife;
        p.size = 3.0f + (rng() % 5);

        // Smoky colors mixed with enemy color
        if (rng() % 3 == 0) {
            p.color = QColor(200, 200, 200);  // Smoke
        } else {
            int rv = rng() % 20 - 10;
            p.color = QColor(
                qBound(0, color.red() + rv, 255),
                qBound(0, color.green() + rv, 255),
                qBound(0, color.blue() + rv, 255)
                );
        }

        m_particles.push_back(p);
    }
}

void GameWidget::updateAndDrawParticles(QPainter& painter, float dt) {
    for (auto& p : m_particles) {
        p.pos += p.vel * dt;
        p.vel *= 0.92f;           // Drag
        p.vel.setY(p.vel.y() + 120.0f * dt);  // Gravity
        p.life -= dt;
    }

    // Draw surviving particles
    painter.setPen(Qt::NoPen);
    for (const auto& p : m_particles) {
        if (p.life <= 0) continue;

        float alpha = (p.life / p.maxLife);
        float size = p.size * (0.5f + alpha * 0.5f);

        QColor c = p.color;
        c.setAlpha(int(alpha * 220));
        painter.setBrush(c);
        painter.drawEllipse(p.pos, size, size);
    }

    // Remove dead particles
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
                                     [](const Particle& p){ return p.life <= 0; }), m_particles.end());
}