#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QTimer>
#include <QHostAddress>
#include "Protocol.h"

struct DiscoveredHost {
    QString name;
    QHostAddress address;
    quint16 tcpPort;
};

class NetworkManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { Idle, Host, Client };

    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager() override;

    // Hosting
    bool startHost(const QString& sessionName, quint16 tcpPort = 45455);
    void stopHost();

    // Client
    void startDiscovery();
    void stopDiscovery();
    void joinHost(const QHostAddress& addr, quint16 tcpPort, const QString& playerName);

    Mode mode() const { return m_mode; }
    QVector<DiscoveredHost> hosts() const {
        auto v = m_hosts.values();
        return QVector<DiscoveredHost>(v.begin(), v.end());
    }

    // Sending (high-level)
    void sendToServer(const QByteArray& payload);  // client -> server
    void broadcastToClients(const QByteArray& payload); // host -> all clients

signals:
    void hostsChanged();
    void messageFromClient(quint32 clientId, QByteArray payload);
    void messageFromServer(QByteArray payload);
    void clientConnected(quint32 clientId);
    void clientDisconnected(quint32 clientId);
    void connectedToServer();
    void disconnectedFromServer();

private slots:
    void onUdpReadyRead();
    void onBroadcastTick();
    void onNewTcpConnection();
    void onTcpDisconnected();
    void onTcpReadyRead();
    void onClientConnectedToServer();

private:
    Mode m_mode = Mode::Idle;
    QUdpSocket* m_udp = nullptr;
    QTimer* m_broadcastTimer = nullptr;
    QString m_sessionName;
    quint16 m_tcpPort = 0;

    QTcpServer* m_server = nullptr;
    QHash<quint32, QTcpSocket*> m_clientSockets; // host side
    QHash<QTcpSocket*, quint32> m_socketToClient;
    quint32 m_nextClientId = 1;

    QTcpSocket* m_clientSocket = nullptr;        // client side
    QString     m_clientName;

    QHash<QTcpSocket*, QByteArray> m_recvBuffers;
    QHash<QString, DiscoveredHost> m_hosts;

    void readFromSocket(QTcpSocket* s, std::function<void(const QByteArray&, QTcpSocket*)> cb);
};
