#include "Minimap.h"
#include "Biomes.h"
#include <QtMath>
#include <cmath>

Minimap::Minimap(QObject* parent) : QObject(parent) {}

QColor Minimap::terrainColor(TerrainKind t) const {
    switch (t) {
    case TerrainKind::Grass:     return QColor(90, 155, 75);
    case TerrainKind::Mud:       return QColor(110, 75, 45);
    case TerrainKind::Water:     return QColor(55, 110, 180);
    case TerrainKind::Path:      return QColor(180, 160, 105);
    case TerrainKind::Fence:     return QColor(100, 70, 35);
    case TerrainKind::Barn:      return QColor(170, 35, 35);
    case TerrainKind::Crops:     return QColor(170, 155, 50);
    case TerrainKind::Gate:      return QColor(190, 170, 80);
    case TerrainKind::LongGrass: return QColor(65, 115, 55);
    case TerrainKind::Pen:       return QColor(120, 85, 40);
    default:                     return QColor(90, 155, 75);
    }
}

void Minimap::generate(const GameWorld& world) {
    m_mapW = world.gridW();
    m_mapH = world.gridH();

    // Each grid tile = 1 pixel on minimap
    m_mapCache = QPixmap(m_mapW, m_mapH);
    QPainter p(&m_mapCache);
    p.setRenderHint(QPainter::Antialiasing, false);

    for (int y = 0; y < m_mapH; ++y) {
        for (int x = 0; x < m_mapW; ++x) {
            TerrainKind t = world.tileAt(x, y);
            p.setPen(Qt::NoPen);
            p.fillRect(x, y, 1, 1, terrainColor(t));
        }
    }
    p.end();

    m_scale = float(MINIMAP_W) / float(m_mapW);
}

void Minimap::draw(QPainter& painter, const GameWorld& world, quint32 localPlayerId,
                   int screenW, int screenH, float animTime) {
    if (!m_visible) return;

    int drawX = screenW - MINIMAP_W - MARGIN;
    int drawY = screenH - int(m_mapH * m_scale) - MARGIN;
    int drawH = int(m_mapH * m_scale);

    // Background with border
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(drawX - 4, drawY - 4, MINIMAP_W + 8, drawH + 8, 4, 4);

    // Draw cached terrain
    painter.drawPixmap(drawX, drawY, MINIMAP_W, drawH, m_mapCache);

    // Border
    painter.setPen(QPen(QColor(180, 160, 120), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(drawX - 1, drawY - 1, MINIMAP_W + 2, drawH + 2, 3, 3);

    // Draw goal
    QPointF goalWorld = world.goalPoint();
    float goalMX = drawX + (goalWorld.x() / (m_mapW * TILE)) * MINIMAP_W;
    float goalMY = drawY + (goalWorld.y() / (m_mapH * TILE)) * drawH;

    float goalPulse = 2.5f + std::sin(animTime * 4.0f) * 1.0f;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 0, 150));
    painter.drawEllipse(QPointF(goalMX, goalMY), goalPulse, goalPulse);

    // Draw pickups (keys only - too cluttered otherwise)
    for (const auto& pk : world.pickups()) {
        if (pk.taken || pk.kind != PickupKind::Key) continue;

        float pkMX = drawX + (pk.pos.x() / (m_mapW * TILE)) * MINIMAP_W;
        float pkMY = drawY + (pk.pos.y() / (m_mapH * TILE)) * drawH;

        painter.setBrush(QColor(0, 255, 255, 180));
        painter.drawRect(QRectF(pkMX - 1, pkMY - 1, 2, 2));
    }

    // Draw enemies as small red dots
    for (const auto& e : world.enemies()) {
        float eMX = drawX + (e.pos.x() / (m_mapW * TILE)) * MINIMAP_W;
        float eMY = drawY + (e.pos.y() / (m_mapH * TILE)) * drawH;

        QColor ec;
        switch (e.kind) {
        case EnemyKind::Crow: ec = QColor(80, 80, 80); break;
        default:              ec = QColor(220, 50, 50); break;
        }
        painter.setBrush(ec);
        painter.drawRect(QRectF(eMX - 0.5, eMY - 0.5, 1.5, 1.5));
    }

    // Draw other players as magenta dots
    for (const auto& p : world.players()) {
        if (!p.alive) continue;
        if (p.id == localPlayerId) continue;

        float pMX = drawX + (p.pos.x() / (m_mapW * TILE)) * MINIMAP_W;
        float pMY = drawY + (p.pos.y() / (m_mapH * TILE)) * drawH;

        painter.setBrush(QColor(220, 80, 220));
        painter.setPen(QPen(Qt::white, 0.5));
        painter.drawEllipse(QPointF(pMX, pMY), 2.5, 2.5);
    }

    // Draw local player as bright blue with pulse
    const Player* local = nullptr;
    for (const auto& p : world.players()) {
        if (p.id == localPlayerId) { local = &p; break; }
    }

    if (local && local->alive) {
        float pMX = drawX + (local->pos.x() / (m_mapW * TILE)) * MINIMAP_W;
        float pMY = drawY + (local->pos.y() / (m_mapH * TILE)) * drawH;

        // Pulse ring
        float pulse = 4.0f + std::sin(animTime * 5.0f) * 1.5f;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(100, 150, 255, 60));
        painter.drawEllipse(QPointF(pMX, pMY), pulse, pulse);

        // Player dot
        painter.setBrush(QColor(80, 130, 255));
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(QPointF(pMX, pMY), 3, 3);

        // Direction arrow
        static const QPointF faceDirs[4] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
        QPointF dir = faceDirs[local->facing];
        QPointF arrowTip = QPointF(pMX, pMY) + dir * 6;
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawLine(QPointF(pMX, pMY), arrowTip);
    }

    // Biome labels along the top of minimap
    painter.setFont(QFont("Arial", 6));
    BiomeType types[] = {
        BiomeType::Grassy, BiomeType::Muddy, BiomeType::Watery,
        BiomeType::Crops, BiomeType::Coop, BiomeType::Field
    };

    int chunk = m_mapW / 6;
    for (int i = 0; i < 6; ++i) {
        float labelX = drawX + (float(chunk * i + chunk/2) / m_mapW) * MINIMAP_W;

        // Highlight current biome
        bool isCurrent = false;
        if (local) {
            int playerBiome = world.getRegionIndex(local->pos);
            isCurrent = (playerBiome == i);
        }

        if (isCurrent) {
            painter.setPen(QColor(255, 220, 80));
        } else {
            painter.setPen(QColor(200, 200, 200, 120));
        }

        QString name = BiomeManager::getBiomeName(types[i]);
        QRectF labelRect(labelX - 20, drawY - 12, 40, 10);
        painter.drawText(labelRect, Qt::AlignCenter, name);
    }
}