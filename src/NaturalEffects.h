#pragma once
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QColor>
#include <QVector>
#include <random>

class GameWorld;

enum class TimeOfDay : quint8 {
    Dawn = 0,
    Day,
    Dusk,
    Night
};

enum class Season : quint8 {
    Spring = 0,
    Summer,
    Autumn,
    Winter
};

enum class WeatherType : quint8 {
    Clear = 0,
    Rain,
    Storm,
    Snow,
    Fog
};

class NaturalEffects : public QObject {
    Q_OBJECT
public:
    explicit NaturalEffects(QObject* parent = nullptr);

    void update(float dt, GameWorld& world);
    void drawWorldEffects(QPainter& painter, QPointF cameraOffset, int screenW, int screenH, float animTime);
    void drawOverlay(QPainter& painter, int screenW, int screenH, float animTime);
    void drawHudInfo(QPainter& painter, int x, int y);
    void drawLeafPiles(QPainter& painter);

    void reset();

    TimeOfDay timeOfDay() const { return m_time; }
    Season season() const { return m_season; }
    WeatherType weather() const { return m_weather; }

private:
    // Time cycle
    TimeOfDay m_time = TimeOfDay::Day;
    float m_timeTimer = 0.0f;
    float m_timeDuration = 0.0f;
    float m_timeTransition = 0.0f;  // 0-1 blend into current state

    // Season cycle
    Season m_season = Season::Summer;
    float m_seasonTimer = 0.0f;
    float m_seasonDuration = 0.0f;

    // Weather
    WeatherType m_weather = WeatherType::Clear;
    float m_weatherTimer = 0.0f;
    float m_weatherDuration = 0.0f;
    float m_weatherIntensity = 0.0f;  // 0-1 ramp in/out

    std::mt19937 m_rng;

    // Rain particles
    struct RainDrop {
        float x, y;
        float speed;
        float length;
    };
    QVector<RainDrop> m_rain;

    // Snow particles
    struct Snowflake {
        float x, y;
        float speed;
        float drift;
        float size;
        float wobble;
    };
    QVector<Snowflake> m_snow;

    // Puddles (spawned during rain)
    struct Puddle {
        float x, y;
        float radius;
        float life;
        float ripplePhase;
    };
    QVector<Puddle> m_puddles;

    // Lightning
    float m_lightningTimer = 0.0f;
    float m_lightningFlash = 0.0f;
    struct LightningBolt {
        QVector<QPointF> points;
        float life;
    };
    QVector<LightningBolt> m_bolts;

    // Fog
    struct FogPatch {
        float x, y;
        float radius;
        float alpha;
        float drift;
    };
    QVector<FogPatch> m_fog;

    struct LeafPileDecor {
        float x, y;
        float radius;
        QColor color1, color2, color3;
        int pattern;
    };
    QVector<LeafPileDecor> m_leafPileDecor;
    bool m_leafPilesPlaced = false;

    void advanceTime();
    void advanceSeason();
    void pickWeather();
    void spawnRain(int screenW, int screenH);
    void spawnSnow(int screenW, int screenH);
    void spawnFog(int screenW, int screenH);
    void generateLightning(int screenW, int screenH);
    void placeLeafPiles(GameWorld& world);
    void removeLeafPiles(GameWorld& world);

    QColor getTimeOverlayColor() const;
    float getTimeBrightness() const;
};