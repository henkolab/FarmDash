#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QSet>
#include <QElapsedTimer>
#include "GameWorld.h"
#include "Protocol.h"
#include "Audio.h"
#include "NetworkManager.h"
#include "Minimap.h"
#include "NaturalEffects.h"

class GameWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum class Role { SinglePlayer, Host, Client };

    explicit GameWidget(QWidget* parent = nullptr);
    ~GameWidget() override;

    void startSinglePlayer(const QString& playerName);
    void startAsHost(NetworkManager* net, const QString& playerName);
    void startAsClient(NetworkManager* net, const QString& playerName);

signals:
    void exitToMenu();
    void roundFinished(qint64 timeMs);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private slots:
    void onTick();
    void onMessageFromClient(quint32 cid, QByteArray payload);
    void onMessageFromServer(QByteArray payload);
    void onClientConnected(quint32 cid);

private:
    Role m_role = Role::SinglePlayer;
    GameWorld m_world;
    Audio* m_audio = nullptr;
    QTimer m_tickTimer;
    QElapsedTimer m_dtClock;
    QSet<int> m_keys;
    QString m_playerName;
    quint32 m_localPlayerId = 0;
    NetworkManager* m_net = nullptr;
    QHash<quint32, quint32> m_clientToPlayer;
    QHash<quint32, Proto::PlayerInput> m_collectedInputs;
    qint64 m_countdownEndsAt = 0;
    bool m_showLeaderboard = false;
    bool m_finished = false;
    qint64 m_finishMs = -1;
    int m_exitCountdown = -1;
    bool m_mousePendingShoot = false;

    void drawTerrain(QPainter& painter);
    void drawEntities(QPainter& painter);
    void drawHud(QPainter& painter);
    QPointF cameraOffset() const;
    Proto::PlayerInput readLocalInput() const;
    void doShoot();

    QRect m_retryRect;
    QRect m_menuRect;
    bool m_showResultsMenu = false;

    QPixmap m_terrainCache;
    void updateTerrainCache();

    float m_animTime = 0.0f;  // Global animation timer

    // Decorative props generated once per map
    struct Prop {
        QPointF pos;
        int type;  // 0=flower, 1=rock, 2=grass tuft
        QColor color;
    };
    QVector<Prop> m_props;
    void generateProps();

    struct Cloud {
        float x, y;       // Position in world-ish space
        float width;
        float height;
        float speed;       // Drift speed
        int alpha;
    };
    QVector<Cloud> m_clouds;

    struct BgBush {
        float x, y;
        float radius;
        QColor color;
    };
    QVector<BgBush> m_bgBushes;

    struct BgFlower {
        float x, y;
        QColor color;
    };
    QVector<BgFlower> m_bgFlowers;

    void generateBackground();
    void drawParallaxBackground(QPainter& painter, QPointF offset);
    void drawFenceExterior(QPainter& painter, QPointF offset);

    int m_currentBiome = -1;
    float m_biomeFlashTimer = 0.0f;
    QString m_biomeFlashName;

    struct Particle {
        QPointF pos;
        QPointF vel;
        float life;
        float maxLife;
        QColor color;
        float size;
    };
    QVector<Particle> m_particles;

    void spawnDeathBurst(QPointF pos, QColor color, int count);
    void spawnEnemyPoof(QPointF pos, QColor color);
    void updateAndDrawParticles(QPainter& painter, float dt);

    // Track enemy count to detect kills
    int m_lastEnemyCount = 0;
    QVector<QPointF> m_lastEnemyPositions;
    QVector<QColor> m_lastEnemyColors;

    Minimap* m_minimap = nullptr;
    NaturalEffects* m_effects = nullptr;
};