#include "NetworkManager.h"
#include <QNetworkDatagram>
#include <QDataStream>
#include <QDebug>
#include <functional>

static constexpr quint16 kDiscoveryPort = 45454;

NetworkManager::NetworkManager(QObject* p) : QObject(p) {}
NetworkManager::~NetworkManager() { stopHost(); stopDiscovery(); }

bool NetworkManager::startHost(const QString& sessionName, quint16 tcpPort) {
    stopHost();
    m_mode = Mode::Host;
    m_sessionName = sessionName;
    m_tcpPort = tcpPort;
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, tcpPort)) {
        qWarning() << "TCP listen failed:" << m_server->errorString();
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &NetworkManager::onNewTcpConnection);

    m_udp = new QUdpSocket(this);
    m_udp->bind(QHostAddress::AnyIPv4, kDiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    m_broadcastTimer = new QTimer(this);
    connect(m_broadcastTimer, &QTimer::timeout, this, &NetworkManager::onBroadcastTick);
    m_broadcastTimer->start(1000);
    return true;
}

void NetworkManager::stopHost() {
    if (m_broadcastTimer) { m_broadcastTimer->stop(); m_broadcastTimer->deleteLater(); m_broadcastTimer = nullptr; }
    if (m_server) { m_server->close(); m_server->deleteLater(); m_server = nullptr; }
    for (auto* s : m_clientSockets) s->deleteLater();
    m_clientSockets.clear(); m_socketToClient.clear(); m_recvBuffers.clear();
    if (m_udp && m_mode == Mode::Host) { m_udp->deleteLater(); m_udp = nullptr; }
    if (m_mode == Mode::Host) m_mode = Mode::Idle;
}

void NetworkManager::onBroadcastTick() {
    QByteArray dg;
    QDataStream d(&dg, QIODevice::WriteOnly);
    d.setVersion(QDataStream::Qt_6_2);
    d << QString("FARMRUN_HOST") << m_sessionName << m_tcpPort;
    m_udp->writeDatagram(dg, QHostAddress::Broadcast, kDiscoveryPort);
}

void NetworkManager::startDiscovery() {
    stopDiscovery();
    m_mode = Mode::Client;  // discovery doesn't switch us out of Idle until join, but ok
    m_udp = new QUdpSocket(this);
    m_udp->bind(QHostAddress::AnyIPv4, kDiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);
}

void NetworkManager::stopDiscovery() {
    if (m_udp && m_mode != Mode::Host) { m_udp->deleteLater(); m_udp = nullptr; }
    m_hosts.clear();
}

void NetworkManager::onUdpReadyRead() {
    while (m_udp && m_udp->hasPendingDatagrams()) {
        auto dg = m_udp->receiveDatagram();
        QDataStream d(dg.data());
        d.setVersion(QDataStream::Qt_6_2);
        QString tag, name; quint16 port;
        d >> tag >> name >> port;
        if (tag != "FARMRUN_HOST") continue;
        DiscoveredHost h{ name, dg.senderAddress(), port };
        QString key = name + ":" + dg.senderAddress().toString() + ":" + QString::number(port);
        if (!m_hosts.contains(key)) {
            m_hosts.insert(key, h);
            emit hostsChanged();
        }
    }
}

void NetworkManager::joinHost(const QHostAddress& addr, quint16 port, const QString& playerName) {
    m_mode = Mode::Client;
    m_clientName = playerName;
    if (m_clientSocket) m_clientSocket->deleteLater();
    m_clientSocket = new QTcpSocket(this);
    connect(m_clientSocket, &QTcpSocket::connected, this, &NetworkManager::onClientConnectedToServer);
    connect(m_clientSocket, &QTcpSocket::readyRead, this, &NetworkManager::onTcpReadyRead);
    connect(m_clientSocket, &QTcpSocket::disconnected, this, [this]{ emit disconnectedFromServer(); });
    m_clientSocket->connectToHost(addr, port);
}

void NetworkManager::onClientConnectedToServer() {
    sendToServer(Proto::encodeHello(m_clientName));
    emit connectedToServer();
}

void NetworkManager::onNewTcpConnection() {
    while (m_server->hasPendingConnections()) {
        auto* s = m_server->nextPendingConnection();
        quint32 cid = m_nextClientId++;
        m_clientSockets.insert(cid, s);
        m_socketToClient.insert(s, cid);
        connect(s, &QTcpSocket::disconnected, this, &NetworkManager::onTcpDisconnected);
        connect(s, &QTcpSocket::readyRead, this, &NetworkManager::onTcpReadyRead);
        emit clientConnected(cid);
    }
}

void NetworkManager::onTcpDisconnected() {
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    quint32 cid = m_socketToClient.value(s, 0);
    if (cid) { emit clientDisconnected(cid); m_clientSockets.remove(cid); }
    m_socketToClient.remove(s);
    m_recvBuffers.remove(s);
    s->deleteLater();
}

void NetworkManager::readFromSocket(QTcpSocket* s, std::function<void(const QByteArray&, QTcpSocket*)> cb) {
    auto& buf = m_recvBuffers[s];
    buf.append(s->readAll());
    while (true) {
        if (buf.size() < 4) return;
        QDataStream d(buf);
        d.setVersion(QDataStream::Qt_6_2);
        quint32 len; d >> len;
        if (quint32(buf.size()) < 4 + len) return;
        QByteArray payload = buf.mid(4, len);
        buf.remove(0, 4 + len);
        cb(payload, s);
    }
}

void NetworkManager::onTcpReadyRead() {
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    if (s == m_clientSocket) {
        readFromSocket(s, [&](const QByteArray& p, QTcpSocket*){
            emit messageFromServer(p);
        });
    } else {
        quint32 cid = m_socketToClient.value(s, 0);
        readFromSocket(s, [&](const QByteArray& p, QTcpSocket*){
            emit messageFromClient(cid, p);
        });
    }
}

void NetworkManager::sendToServer(const QByteArray& payload) {
    if (!m_clientSocket) return;
    m_clientSocket->write(Proto::frame(payload));
}

void NetworkManager::broadcastToClients(const QByteArray& payload) {
    auto framed = Proto::frame(payload);
    for (auto* s : m_clientSockets) s->write(framed);
}
