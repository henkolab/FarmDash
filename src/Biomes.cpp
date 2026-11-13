#include "Biomes.h"
#include "GameWorld.h"

void BiomeManager::generateBiome(GameWorld& world, const BiomeRegion& region, std::mt19937& rng) {
    switch (region.type) {
    case BiomeType::Grassy:
        createBlobs(world, region.area, TerrainKind::Grass, 20, 2, rng);
        createBlobs(world, region.area, TerrainKind::LongGrass, 12, 3, rng);
        break;

    case BiomeType::Muddy:
        createBlobs(world, region.area, TerrainKind::Grass, 30, 2, rng);
        break;

    case BiomeType::Watery:
        createBlobs(world, region.area, TerrainKind::Water, 10, 4, rng);
        break;

    case BiomeType::Crops:
        for (int x = region.area.left() + 1; x < region.area.right(); x += 2) {
            if (rng() % 10 < 7) {
                int startY = 2 + (rng() % 5);
                int endY = world.gridH() - 3 - (rng() % 5);
                for (int y = startY; y < endY; ++y) {
                    if (world.tileAt(x, y) != TerrainKind::Path)
                        world.setTileAt(x, y, TerrainKind::Crops);
                }
            }
        }
        break;

    case BiomeType::Coop: {
        // Scatter some long grass between the pens
        createBlobs(world, region.area, TerrainKind::LongGrass, 6, 2, rng);

        // Scatter chicken pens across the biome
        int penCount = 4 + (rng() % 3);
        for (int i = 0; i < penCount; ++i) {
            // Random pen size
            int penW = 3 + (rng() % 3);  // 3-5 tiles wide
            int penH = 3 + (rng() % 3);  // 3-5 tiles tall

            // Random position within biome, away from edges
            int px = region.area.left() + 2 + (rng() % (region.area.width() - penW - 4));
            int py = region.area.top() + 2 + (rng() % (region.area.height() - penH - 4));

            // Check if pen overlaps the path
            bool overlapsPath = false;
            for (int yy = py - 1; yy <= py + penH + 1; ++yy) {
                for (int xx = px - 1; xx <= px + penW + 1; ++xx) {
                    if (world.tileAt(xx, yy) == TerrainKind::Path) {
                        overlapsPath = true;
                        break;
                    }
                }
                if (overlapsPath) break;
            }
            if (overlapsPath) continue;

            // Draw pen walls
            for (int xx = px; xx < px + penW; ++xx) {
                world.setTileAt(xx, py, TerrainKind::Pen);
                world.setTileAt(xx, py + penH - 1, TerrainKind::Pen);
            }
            for (int yy = py; yy < py + penH; ++yy) {
                world.setTileAt(px, yy, TerrainKind::Pen);
                world.setTileAt(px + penW - 1, yy, TerrainKind::Pen);
            }

            // Pen entrance - gap in bottom wall
            int gateX = px + 1 + (rng() % (penW - 2));
            world.setTileAt(gateX, py + penH - 1, TerrainKind::Grass);

            // Hay/straw floor inside pen
            for (int yy = py + 1; yy < py + penH - 1; ++yy) {
                for (int xx = px + 1; xx < px + penW - 1; ++xx) {
                    if (rng() % 3 == 0) {
                        world.setTileAt(xx, yy, TerrainKind::Mud);  // Dirty patches
                    }
                }
            }
        }
        break;
    }

    case BiomeType::Field:
        createBlobs(world, region.area, TerrainKind::Mud, 6, 2, rng);
        break;
    }
}

void BiomeManager::fillTiles(GameWorld& world, const QRect& area, TerrainKind tile) {
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            world.setTileAt(x, y, tile);
        }
    }
}

void BiomeManager::createBlobs(GameWorld& world, const QRect& area, TerrainKind tile,int count, int radius, std::mt19937& rng) {
    if (area.width() <= 0 || area.height() <= 0) return;  // ADD THIS GUARD

    for (int i = 0; i < count; ++i) {
        int cx = area.left() + (rng() % area.width());
        int cy = area.top()  + (rng() % area.height());
        for (int yy = -radius; yy <= radius; ++yy) {
            for (int xx = -radius; xx <= radius; ++xx) {
                if (xx*xx + yy*yy <= radius*radius) {
                    int tx = cx + xx;
                    int ty = cy + yy;

                    if (tx == area.right()) continue;

                    TerrainKind current = world.tileAt(tx, ty);
                    if (current == TerrainKind::Path && tile != TerrainKind::Mud) continue;
                    if (current == TerrainKind::Fence || current == TerrainKind::Gate) continue;

                    world.setTileAt(tx, ty, tile);
                }
            }
        }
    }
}

TerrainKind BiomeManager::getPrimaryTile(BiomeType type) {
    switch (type) {
    case BiomeType::Muddy: return TerrainKind::Mud;
    case BiomeType::Coop:  return TerrainKind::Grass;
    default:               return TerrainKind::Grass;
    }
}

QVector<EnemyKind> BiomeManager::getEnemiesForBiome(BiomeType type) {
    switch (type) {
    case BiomeType::Grassy: return { EnemyKind::Fox };
    case BiomeType::Muddy:  return { EnemyKind::Pig };
    case BiomeType::Watery: return { EnemyKind::Goose };
    case BiomeType::Crops:  return { EnemyKind::Chicken };
    case BiomeType::Coop:   return { EnemyKind::Rooster };
    case BiomeType::Field:  return { EnemyKind::Cow, EnemyKind::Sheep };
    default:                return { EnemyKind::Chicken };
    }
}

QString BiomeManager::getBiomeName(BiomeType type) {
    switch (type) {
    case BiomeType::Grassy: return "Grassy";
    case BiomeType::Muddy:  return "Muddy";
    case BiomeType::Watery: return "Watery";
    case BiomeType::Crops:  return "Crops";
    case BiomeType::Coop:   return "Coop";
    case BiomeType::Field:  return "Field";
    default:                return "Unknown";
    }
}

QColor BiomeManager::getPathColor(BiomeType type) {
    switch (type) {
    case BiomeType::Grassy: return QColor(190, 160, 105);   // Warm dirt
    case BiomeType::Muddy:  return QColor(140, 100, 65);    // Muddy ruts
    case BiomeType::Watery: return QColor(155, 155, 150);   // Wet stone
    case BiomeType::Crops:  return QColor(180, 170, 135);   // Pale straw gravel
    case BiomeType::Coop:   return QColor(155, 115, 65);    // Worn wood/straw
    case BiomeType::Field:  return QColor(175, 150, 100);   // Dusty ranch trail
    default:                return QColor(200, 180, 120);
    }
}

QColor BiomeManager::getGatePostColor(BiomeType type) {
    switch (type) {
    case BiomeType::Grassy: return QColor(120, 85, 45);     // Light wood
    case BiomeType::Muddy:  return QColor(90, 65, 35);      // Dark wet wood
    case BiomeType::Watery: return QColor(130, 130, 125);   // Stone posts
    case BiomeType::Crops:  return QColor(160, 140, 80);    // Pale fence rail
    case BiomeType::Coop:   return QColor(140, 95, 50);     // Farm rail
    case BiomeType::Field:  return QColor(110, 80, 40);     // Ranch wood
    default:                return QColor(120, 80, 40);
    }
}