#pragma once
#include <QVector>
#include <QPoint>
#include <QPointF>

class GameWorld;

// Coarse grid A*; treats Water/Fence/Barn tiles as blocked unless 'flying' is true.
class Pathfinder {
public:
    static QVector<QPointF> findPath(const GameWorld& world,
                                     QPointF fromWorld,
                                     QPointF toWorld,
                                     bool flying,
                                     bool canSwim);
};
