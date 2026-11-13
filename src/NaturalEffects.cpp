#include "NaturalEffects.h"
#include "GameWorld.h"
#include <QtMath>
#include <cmath>
#include <algorithm>

NaturalEffects::NaturalEffects(QObject* parent) : QObject(parent) {
    m_rng.seed(42);
    reset();
}

void NaturalEffects::reset() {
    m_time = TimeOfDay::Day;
    m_timeTimer = 0.0f;
    m_timeDuration = 30.0f + (m_rng() % 30);
    m_timeTransition = 1.0f;

    m_season = Season::Summer;
    m_seasonTimer = 0.0f;
    m_seasonDuration = 40.0f + (m_rng() % 20);

    m_weather = WeatherType::Clear;
    m_weatherTimer = 0.0f;
    m_weatherDuration = 15.0f + (m_rng() % 20);
    m_weatherIntensity = 0.0f;

    m_rain.clear();
    m_snow.clear();
    m_puddles.clear();
    m_leafPileDecor.clear();
    m_leafPilesPlaced = false;
    m_bolts.clear();
    m_fog.clear();
    m_lightningTimer = 0.0f;
    m_lightningFlash = 0.0f;
}

void NaturalEffects::advanceTime() {
    int next;
    do {
        next = m_rng() % 4;
    } while (next == (int)m_time);

    m_time = (TimeOfDay)next;
    m_timeDuration = 25.0f + (m_rng() % 35);
    m_timeTimer = 0.0f;
    m_timeTransition = 0.0f;
}

void NaturalEffects::advanceSeason() {
    int next;
    do {
        next = m_rng() % 4;
    } while (next == (int)m_season);

    m_season = (Season)next;
    m_seasonDuration = 35.0f + (m_rng() % 25);
    m_seasonTimer = 0.0f;

    if (m_season != Season::Winter) m_snow.clear();

    pickWeather();
}

void NaturalEffects::pickWeather() {
    int roll = m_rng() % 100;

    switch (m_season) {
    case Season::Spring:
        if (roll < 40) m_weather = WeatherType::Rain;
        else if (roll < 55) m_weather = WeatherType::Fog;
        else m_weather = WeatherType::Clear;
        break;
    case Season::Summer:
        if (roll < 20) m_weather = WeatherType::Storm;
        else if (roll < 35) m_weather = WeatherType::Rain;
        else m_weather = WeatherType::Clear;
        break;
    case Season::Autumn:
        if (roll < 35) m_weather = WeatherType::Rain;
        else if (roll < 50) m_weather = WeatherType::Fog;
        else m_weather = WeatherType::Clear;
        break;
    case Season::Winter:
        if (roll < 50) m_weather = WeatherType::Snow;
        else if (roll < 65) m_weather = WeatherType::Fog;
        else m_weather = WeatherType::Clear;
        break;
    }

    m_weatherDuration = 15.0f + (m_rng() % 25);
    m_weatherTimer = 0.0f;
    m_weatherIntensity = 0.0f;
}

void NaturalEffects::update(float dt, GameWorld& world) {
    dt = qMin(dt, 0.033f);

    m_timeTimer += dt;
    if (m_timeTransition < 1.0f) {
        m_timeTransition = qMin(1.0f, m_timeTransition + dt * 0.5f);
    }
    if (m_timeTimer >= m_timeDuration) {
        advanceTime();
    }

    m_seasonTimer += dt;
    if (m_seasonTimer >= m_seasonDuration) {
        if (m_season == Season::Autumn && m_leafPilesPlaced) {
            removeLeafPiles(world);
            m_leafPilesPlaced = false;
        }
        advanceSeason();
    }

    if (m_season == Season::Autumn && !m_leafPilesPlaced) {
        placeLeafPiles(world);
        m_leafPilesPlaced = true;
    }

    m_weatherTimer += dt;

    float weatherProgress = m_weatherTimer / m_weatherDuration;
    if (weatherProgress < 0.15f) {
        m_weatherIntensity = weatherProgress / 0.15f;
    } else if (weatherProgress > 0.85f) {
        m_weatherIntensity = (1.0f - weatherProgress) / 0.15f;
    } else {
        m_weatherIntensity = 1.0f;
    }
    m_weatherIntensity = qBound(0.0f, m_weatherIntensity, 1.0f);

    if (m_weatherTimer >= m_weatherDuration) {
        pickWeather();
    }

    if (m_lightningFlash > 0) {
        m_lightningFlash -= dt * 4.0f;
    }

    for (auto& b : m_bolts) b.life -= dt;
    m_bolts.erase(std::remove_if(m_bolts.begin(), m_bolts.end(),
                                 [](const LightningBolt& b){ return b.life <= 0; }), m_bolts.end());

    for (auto& p : m_puddles) {
        p.life -= dt * 0.05f;
        p.ripplePhase += dt * 3.0f;
    }
    m_puddles.erase(std::remove_if(m_puddles.begin(), m_puddles.end(),
                                   [](const Puddle& p){ return p.life <= 0; }), m_puddles.end());

    for (auto& r : m_rain) {
        r.y += r.speed * dt;
    }
    m_rain.erase(std::remove_if(m_rain.begin(), m_rain.end(),
                                [](const RainDrop& r){ return r.y > 2000; }), m_rain.end());

    for (auto& s : m_snow) {
        s.y += s.speed * dt;
        s.x += std::sin(s.wobble) * s.drift * dt;
        s.wobble += dt * 2.0f;
    }
    m_snow.erase(std::remove_if(m_snow.begin(), m_snow.end(),
                                [](const Snowflake& s){ return s.y > 2000; }), m_snow.end());

    for (auto& f : m_fog) {
        f.x += f.drift * dt;
    }
}

void NaturalEffects::spawnRain(int screenW, int screenH) {
    int count = int(25 * m_weatherIntensity);
    for (int i = 0; i < count; ++i) {
        RainDrop r;
        r.x = (m_rng() % (screenW + 200)) - 100;
        r.y = -(m_rng() % 200);
        r.speed = 700.0f + (m_rng() % 400);
        r.length = 12.0f + (m_rng() % 18);
        m_rain.push_back(r);
    }

    if (m_rng() % 40 == 0 && m_puddles.size() < 20) {
        Puddle p;
        p.x = m_rng() % screenW;
        p.y = m_rng() % screenH;
        p.radius = 4.0f + (m_rng() % 8);
        p.life = 1.0f;
        p.ripplePhase = 0;
        m_puddles.push_back(p);
    }
}

void NaturalEffects::spawnSnow(int screenW, int screenH) {
    int count = int(15 * m_weatherIntensity);
    for (int i = 0; i < count; ++i) {
        Snowflake s;
        s.x = (m_rng() % (screenW + 200)) - 100;
        s.y = -(m_rng() % 100);
        s.speed = 50.0f + (m_rng() % 80);
        s.drift = 20.0f + (m_rng() % 30);
        s.size = 2.0f + (m_rng() % 40) / 10.0f;
        s.wobble = (m_rng() % 628) / 100.0f;
        m_snow.push_back(s);
    }
}

void NaturalEffects::spawnFog(int screenW, int screenH) {
    if (m_fog.size() > 8) return;
    if (m_rng() % 30 != 0) return;

    FogPatch f;
    f.x = -(m_rng() % 200);
    f.y = (m_rng() % screenH);
    f.radius = 100.0f + (m_rng() % 200);
    f.alpha = 20.0f + (m_rng() % 30);
    f.drift = 15.0f + (m_rng() % 20);
    m_fog.push_back(f);
}

void NaturalEffects::generateLightning(int screenW, int screenH) {
    if (m_weather != WeatherType::Storm) return;
    if (m_weatherIntensity < 0.5f) return;

    m_lightningTimer -= 0.016f;
    if (m_lightningTimer > 0) return;

    m_lightningTimer = 2.0f + (m_rng() % 60) / 10.0f;
    m_lightningFlash = 1.0f;

    LightningBolt bolt;
    bolt.life = 0.15f;

    float bx = screenW * 0.2f + (m_rng() % (int)(screenW * 0.6f));
    float by = 0;
    bolt.points.push_back(QPointF(bx, by));

    while (by < screenH * 0.7f) {
        bx += (m_rng() % 60) - 30;
        by += 20 + (m_rng() % 40);
        bolt.points.push_back(QPointF(bx, by));

        if (m_rng() % 4 == 0 && bolt.points.size() < 15) {
            float branchX = bx + (m_rng() % 80) - 40;
            float branchY = by + 15 + (m_rng() % 25);
            bolt.points.push_back(QPointF(branchX, branchY));
            bolt.points.push_back(QPointF(bx, by));
        }
    }

    m_bolts.push_back(bolt);
}

float NaturalEffects::getTimeBrightness() const {
    float base;
    switch (m_time) {
    case TimeOfDay::Dawn:  base = 0.7f;  break;
    case TimeOfDay::Day:   base = 1.0f;  break;
    case TimeOfDay::Dusk:  base = 0.65f; break;
    case TimeOfDay::Night: base = 0.35f; break;
    }

    float prev;
    switch ((TimeOfDay)(((int)m_time + 3) % 4)) {
    case TimeOfDay::Dawn:  prev = 0.7f;  break;
    case TimeOfDay::Day:   prev = 1.0f;  break;
    case TimeOfDay::Dusk:  prev = 0.65f; break;
    case TimeOfDay::Night: prev = 0.35f; break;
    }

    return prev + (base - prev) * m_timeTransition;
}

QColor NaturalEffects::getTimeOverlayColor() const {
    QColor base;
    switch (m_time) {
    case TimeOfDay::Dawn:  base = QColor(255, 180, 120); break;
    case TimeOfDay::Day:   base = QColor(255, 255, 255); break;
    case TimeOfDay::Dusk:  base = QColor(255, 140, 100); break;
    case TimeOfDay::Night: base = QColor(60, 80, 140);   break;
    }
    return base;
}

void NaturalEffects::placeLeafPiles(GameWorld& world) {
    m_leafPileDecor.clear();

    int gw = world.gridW();
    int gh = world.gridH();

    for (int y = 2; y < gh - 2; ++y) {
        for (int x = 2; x < gw - 2; ++x) {
            TerrainKind t = world.tileAt(x, y);

            if (t != TerrainKind::Grass) continue;
            if (m_rng() % 14 != 0) continue;

            bool nearPath = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    TerrainKind adj = world.tileAt(x + dx, y + dy);
                    if (adj == TerrainKind::Path || adj == TerrainKind::Gate ||
                        adj == TerrainKind::Fence || adj == TerrainKind::Barn) {
                        nearPath = true;
                    }
                }
            }
            if (nearPath) continue;

            world.setTileAt(x, y, TerrainKind::LeafPile);

            LeafPileDecor d;
            d.x = x * TILE + TILE / 2.0f;
            d.y = y * TILE + TILE / 2.0f;
            d.radius = TILE * 0.4f;
            d.pattern = m_rng();

            QColor autumnColors[] = {
                QColor(200, 80, 30),
                QColor(220, 160, 30),
                QColor(180, 60, 20),
                QColor(190, 120, 40),
                QColor(160, 40, 30),
                QColor(210, 140, 20),
                QColor(170, 90, 25)
            };
            d.color1 = autumnColors[m_rng() % 7];
            d.color2 = autumnColors[m_rng() % 7];
            d.color3 = autumnColors[m_rng() % 7];

            m_leafPileDecor.push_back(d);
        }
    }
}

void NaturalEffects::removeLeafPiles(GameWorld& world) {
    int gw = world.gridW();
    int gh = world.gridH();

    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            if (world.tileAt(x, y) == TerrainKind::LeafPile) {
                world.setTileAt(x, y, TerrainKind::Grass);
            }
        }
    }
    m_leafPileDecor.clear();
}

void NaturalEffects::drawLeafPiles(QPainter& painter) {
    painter.setPen(Qt::NoPen);

    for (const auto& lp : m_leafPileDecor) {
        std::mt19937 pileRng(lp.pattern);

        for (int i = 0; i < 8; ++i) {
            float ox = (pileRng() % int(lp.radius * 1.6f + 1)) - lp.radius * 0.8f;
            float oy = (pileRng() % int(lp.radius * 1.2f + 1)) - lp.radius * 0.6f;
            float sr = lp.radius * 0.3f + (pileRng() % int(lp.radius * 0.4f + 1));

            QColor colors[] = { lp.color1, lp.color2, lp.color3 };
            QColor c = colors[pileRng() % 3];
            int v = pileRng() % 20 - 10;
            c = QColor(qBound(0, c.red() + v, 255),
                       qBound(0, c.green() + v, 255),
                       qBound(0, c.blue() + v, 255));

            painter.setBrush(c);
            painter.drawEllipse(QPointF(lp.x + ox, lp.y + oy), sr, sr * 0.7f);
        }

        for (int i = 0; i < 5; ++i) {
            float lx = lp.x + (pileRng() % int(lp.radius * 1.4f + 1)) - lp.radius * 0.7f;
            float ly = lp.y + (pileRng() % int(lp.radius * 1.0f + 1)) - lp.radius * 0.5f;
            float angle = (pileRng() % 360) * M_PI / 180.0f;
            float leafSize = 2.0f + (pileRng() % 3);

            QColor colors[] = { lp.color1, lp.color2, lp.color3 };
            QColor c = colors[pileRng() % 3];

            painter.save();
            painter.translate(lx, ly);
            painter.rotate(angle * 180.0f / M_PI);

            painter.setBrush(c);
            painter.drawEllipse(QPointF(0, 0), leafSize, leafSize * 0.5f);

            painter.setPen(QPen(c.darker(150), 0.5));
            painter.drawLine(QPointF(0, 0), QPointF(-leafSize * 0.6f, 0));
            painter.setPen(Qt::NoPen);

            painter.restore();
        }

        painter.setPen(QColor(60, 30, 15, 60));
        for (int i = 0; i < 4; ++i) {
            float dx = (pileRng() % int(lp.radius * 1.2f + 1)) - lp.radius * 0.6f;
            float dy = (pileRng() % int(lp.radius * 0.8f + 1)) - lp.radius * 0.4f;
            painter.drawPoint(QPointF(lp.x + dx, lp.y + dy));
        }
        painter.setPen(Qt::NoPen);
    }
}

void NaturalEffects::drawWorldEffects(QPainter& painter, QPointF cameraOffset, int screenW, int screenH, float animTime) {
    painter.setPen(Qt::NoPen);
    for (const auto& p : m_puddles) {
        float alpha = p.life * m_weatherIntensity * 80;

        painter.setBrush(QColor(80, 120, 180, int(alpha)));
        painter.drawEllipse(QPointF(p.x - cameraOffset.x(), p.y - cameraOffset.y()),
                            p.radius, p.radius * 0.6f);

        float rippleR = p.radius * (0.5f + std::sin(p.ripplePhase) * 0.3f);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(150, 190, 220, int(alpha * 0.5f)), 0.5));
        painter.drawEllipse(QPointF(p.x - cameraOffset.x(), p.y - cameraOffset.y()),
                            rippleR, rippleR * 0.6f);
        painter.setPen(Qt::NoPen);
    }
}

void NaturalEffects::drawOverlay(QPainter& painter, int screenW, int screenH, float animTime) {
    if (m_weather == WeatherType::Rain || m_weather == WeatherType::Storm) {
        spawnRain(screenW, screenH);
    }
    if (m_weather == WeatherType::Snow) {
        spawnSnow(screenW, screenH);
    }
    if (m_weather == WeatherType::Fog) {
        spawnFog(screenW, screenH);
    }
    if (m_weather == WeatherType::Storm) {
        generateLightning(screenW, screenH);
    }

    // Time of day overlay
    float brightness = getTimeBrightness();
    QColor timeColor = getTimeOverlayColor();

    if (brightness < 0.99f) {
        int darkAlpha = int((1.0f - brightness) * 160);
        painter.fillRect(0, 0, screenW, screenH,
                         QColor(timeColor.red()/4, timeColor.green()/4, timeColor.blue()/3, darkAlpha));
    }

    if (m_time == TimeOfDay::Dawn || m_time == TimeOfDay::Dusk) {
        float tintAlpha = m_timeTransition * 30.0f;
        painter.fillRect(0, 0, screenW, screenH,
                         QColor(timeColor.red(), timeColor.green(), timeColor.blue(), int(tintAlpha)));
    }

    // Season tint
    switch (m_season) {
    case Season::Winter:
        painter.fillRect(0, 0, screenW, screenH, QColor(180, 200, 230, 20));
        break;
    case Season::Autumn:
        painter.fillRect(0, 0, screenW, screenH, QColor(200, 140, 60, 15));
        break;
    case Season::Spring:
        painter.fillRect(0, 0, screenW, screenH, QColor(100, 200, 100, 10));
        break;
    default: break;
    }

    // Lightning flash
    if (m_lightningFlash > 0) {
        int flashAlpha = int(m_lightningFlash * 120);
        painter.fillRect(0, 0, screenW, screenH, QColor(255, 255, 255, flashAlpha));
    }

    // Lightning bolts
    for (const auto& bolt : m_bolts) {
        float boltAlpha = bolt.life / 0.15f;
        painter.setPen(QPen(QColor(255, 255, 255, int(boltAlpha * 255)), 2.5));
        for (int i = 1; i < bolt.points.size(); ++i) {
            painter.drawLine(bolt.points[i-1], bolt.points[i]);
        }
        painter.setPen(QPen(QColor(200, 200, 255, int(boltAlpha * 80)), 6));
        for (int i = 1; i < bolt.points.size(); ++i) {
            painter.drawLine(bolt.points[i-1], bolt.points[i]);
        }
    }

    // Rain
    if (!m_rain.isEmpty()) {
        for (const auto& r : m_rain) {
            int baseAlpha = int(m_weatherIntensity * 220);
            int dropAlpha = qMax(0, baseAlpha - (int)(m_rng() % 40));
            float thickness = (r.speed > 850.0f) ? 1.5f : 1.0f;

            painter.setPen(QPen(QColor(180, 210, 240, dropAlpha), thickness));
            painter.drawLine(QPointF(r.x, r.y),
                             QPointF(r.x - 3, r.y + r.length));
        }

        painter.setPen(Qt::NoPen);
        for (int i = 0; i < int(5 * m_weatherIntensity); ++i) {
            float sx = m_rng() % screenW;
            float sy = 100 + (m_rng() % (screenH - 150));
            painter.setBrush(QColor(200, 220, 240, 60 + m_rng() % 40));
            painter.drawEllipse(QPointF(sx, sy), 2, 1);
        }
    }

    // Snow
    if (!m_snow.isEmpty()) {
        painter.setPen(Qt::NoPen);
        for (const auto& s : m_snow) {
            int snowAlpha = int(m_weatherIntensity * 240);

            painter.setBrush(QColor(220, 230, 255, snowAlpha / 3));
            painter.drawEllipse(QPointF(s.x, s.y), s.size + 1.5f, s.size + 1.5f);

            painter.setBrush(QColor(255, 255, 255, snowAlpha));
            painter.drawEllipse(QPointF(s.x, s.y), s.size, s.size);
        }
    }

    // Fog
    if (!m_fog.isEmpty()) {
        painter.setPen(Qt::NoPen);
        for (const auto& f : m_fog) {
            float fogAlpha = f.alpha * m_weatherIntensity;
            painter.setBrush(QColor(200, 210, 220, int(fogAlpha)));
            painter.drawEllipse(QPointF(f.x, f.y), f.radius, f.radius * 0.4f);
        }

        m_fog.erase(std::remove_if(m_fog.begin(), m_fog.end(),
                                   [screenW](const FogPatch& f){ return f.x - f.radius > screenW + 100; }),
                    m_fog.end());
    }
}

void NaturalEffects::drawHudInfo(QPainter& painter, int x, int y) {
    QString timeStr;
    QColor timeColor;
    switch (m_time) {
    case TimeOfDay::Dawn:  timeStr = "Dawn";  timeColor = QColor(255, 200, 140); break;
    case TimeOfDay::Day:   timeStr = "Day";   timeColor = QColor(255, 255, 200); break;
    case TimeOfDay::Dusk:  timeStr = "Dusk";  timeColor = QColor(255, 160, 120); break;
    case TimeOfDay::Night: timeStr = "Night"; timeColor = QColor(140, 160, 220); break;
    }

    QString seasonStr;
    QColor seasonColor;
    switch (m_season) {
    case Season::Spring: seasonStr = "Spring"; seasonColor = QColor(140, 220, 140); break;
    case Season::Summer: seasonStr = "Summer"; seasonColor = QColor(255, 220, 100); break;
    case Season::Autumn: seasonStr = "Autumn"; seasonColor = QColor(220, 150, 60);  break;
    case Season::Winter: seasonStr = "Winter"; seasonColor = QColor(200, 220, 255); break;
    }

    QString weatherStr;
    switch (m_weather) {
    case WeatherType::Clear: weatherStr = "Clear"; break;
    case WeatherType::Rain:  weatherStr = "Rain";  break;
    case WeatherType::Storm: weatherStr = "Storm"; break;
    case WeatherType::Snow:  weatherStr = "Snow";  break;
    case WeatherType::Fog:   weatherStr = "Fog";   break;
    }

    painter.setFont(QFont("Arial", 8));

    painter.setPen(timeColor);
    painter.drawText(x, y, timeStr);

    painter.setPen(QColor(150, 150, 150));
    painter.drawText(x + 35, y, "|");

    painter.setPen(seasonColor);
    painter.drawText(x + 45, y, seasonStr);

    painter.setPen(QColor(150, 150, 150));
    painter.drawText(x + 95, y, "|");

    painter.setPen(QColor(200, 200, 200));
    painter.drawText(x + 105, y, weatherStr);
}