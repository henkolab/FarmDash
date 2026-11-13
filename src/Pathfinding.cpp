#include "Pathfinding.h"
#include "GameWorld.h"
#include <QVector>
#include <queue>
#include <cmath>
#include <algorithm>

namespace {

struct Node {
    int f;
    int idx;
    bool operator<(const Node& o) const { return f > o.f; }
};

inline int heur(int ax, int ay, int bx, int by) {
    return std::abs(ax - bx) + std::abs(ay - by);
}

} // namespace

QVector<QPointF> Pathfinder::findPath(const GameWorld& w, QPointF from, QPointF to, bool flying, bool canSwim) {
    const int W = w.gridW();
    const int H = w.gridH();
    const int totalTiles = W * H;

    int sx = qBound(0, int(from.x()) / TILE, W - 1);
    int sy = qBound(0, int(from.y()) / TILE, H - 1);
    int gx = qBound(0, int(to.x())   / TILE, W - 1);
    int gy = qBound(0, int(to.y())   / TILE, H - 1);

    const int startIdx = sy * W + sx;
    const int goalIdx  = gy * W + gx;

    if (startIdx == goalIdx) return {};

    // Per-call vectors: avoids static cross-enemy corruption
    // Sized to totalTiles = 200*30 = 6000 ints = 24KB each, acceptable
    QVector<int> came(totalTiles, -1);
    QVector<int> gScore(totalTiles, INT_MAX);
    QVector<bool> closed(totalTiles, false);  // Fixes stale node re-expansion

    gScore[startIdx] = 0;

    std::priority_queue<Node> open;
    open.push({ heur(sx, sy, gx, gy), startIdx });

    // Precompute neighbor offsets
    const int dirs[4] = { 1, -1, W, -W };

    bool found = false;

    // Limit iterations to prevent runaway pathfinding on disconnected maps
    const int maxIter = totalTiles * 2;
    int iter = 0;

    while (!open.empty() && iter++ < maxIter) {
        Node cur = open.top();
        open.pop();

        if (cur.idx == goalIdx) { found = true; break; }

        // Skip stale nodes - fixes re-expansion bug
        if (closed[cur.idx]) continue;
        closed[cur.idx] = true;

        int cx = cur.idx % W;
        int cy = cur.idx / W;
        int curG = gScore[cur.idx];

        for (int i = 0; i < 4; ++i) {
            // Boundary checks per direction
            if (i == 0 && cx >= W - 1) continue;  // right
            if (i == 1 && cx <= 0)     continue;  // left
            if (i == 2 && cy >= H - 1) continue;  // down
            if (i == 3 && cy <= 0)     continue;  // up

            int nIdx = cur.idx + dirs[i];

            if (closed[nIdx]) continue;

            TerrainKind t = w.tileAt(nIdx % W, nIdx / W);

            // Terrain blocking
            if (t == TerrainKind::Fence ||
                t == TerrainKind::Barn  ||
                t == TerrainKind::Crops ||
                t == TerrainKind::Gate  ||
                t == TerrainKind::Pen) continue;

            if (t == TerrainKind::Water && !flying && !canSwim) continue;

            if (!flying) {
                if (t == TerrainKind::Fence) continue;
                if (t == TerrainKind::Gate)  continue;
                if (t == TerrainKind::Crops) continue;
                if (t == TerrainKind::Water && !canSwim) continue;
            }

            // Terrain cost weighting
            int cost;
            switch (t) {
            case TerrainKind::Mud:      cost = 3; break;
            case TerrainKind::LongGrass: cost = 2; break;
            case TerrainKind::LeafPile:  cost = 2; break;
            default:                    cost = 1; break;
            }

            int nextG = curG + cost;
            if (nextG < gScore[nIdx]) {
                gScore[nIdx] = nextG;
                came[nIdx] = cur.idx;
                int nx = nIdx % W;
                int ny = nIdx / W;
                open.push({ nextG + heur(nx, ny, gx, gy), nIdx });
            }
        }
    }

    if (!found) return {};

    // Reconstruct path with cycle guard
    QVector<QPointF> out;
    out.reserve(64);

    int current = goalIdx;
    int safetyLimit = totalTiles;  // Prevents infinite loop on corrupt came[]

    while (current != startIdx && current != -1 && safetyLimit-- > 0) {
        int x = current % W;
        int y = current / W;
        out.append(QPointF(x * TILE + TILE / 2.0, y * TILE + TILE / 2.0));
        current = came[current];
    }

    if (safetyLimit <= 0) return {};  // Cycle detected, discard corrupt path

    std::reverse(out.begin(), out.end());
    return out;
}