#pragma once
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include "GameWorld.h"

class Minimap : public QObject {
    Q_OBJECT
public:
    explicit Minimap(QObject* parent = nullptr);

    void generate(const GameWorld& world);
    void draw(QPainter& painter, const GameWorld& world, quint32 localPlayerId,
              int screenW, int screenH, float animTime);

    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }

private:
    QPixmap m_mapCache;
    bool m_visible = true;
    int m_mapW = 0;
    int m_mapH = 0;
    float m_scale = 1.0f;

    // Display settings
    static constexpr int MINIMAP_W = 600;
    static constexpr int MINIMAP_H = 300;
    static constexpr int MARGIN = 15;

    QColor terrainColor(TerrainKind t) const;
};