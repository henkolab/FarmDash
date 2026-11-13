#pragma once
#include "Entities.h"
#include <QVector>
#include <QRect>
#include <QColor>
#include <random>

class GameWorld;

enum class BiomeType : quint8 {
    Grassy = 0,  // New (Start)
    Muddy,
    Watery,
    Crops,
    Coop,
    Field
};

struct BiomeRegion {
    QRect area;
    BiomeType type;
};

class BiomeManager {
public:
    // Main generator
    static void generateBiome(GameWorld& world, const BiomeRegion& region, std::mt19937& rng);

    // Helpers used by generateBiome
    static void fillTiles(GameWorld& world, const QRect& area, TerrainKind tile);
    static void createBlobs(GameWorld& world, const QRect& area, TerrainKind tile, int count, int radius, std::mt19937& rng);

    // Logic mapping
    static TerrainKind getPrimaryTile(BiomeType type);
    static QVector<EnemyKind> getEnemiesForBiome(BiomeType type);
    static QString getBiomeName(BiomeType type);
    static QColor getPathColor(BiomeType type);
    static QColor getGatePostColor(BiomeType type);
};