#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include "NetworkManager.h"

class MenuWidget : public QWidget {
    Q_OBJECT
public:
    explicit MenuWidget(QWidget* parent = nullptr);
    NetworkManager* network() { return m_net; }
    QString playerName() const;

signals:
    void requestSinglePlayer(QString playerName);
    void requestHost(QString playerName);
    void requestJoin(QHostAddress addr, quint16 port, QString playerName);

private:
    NetworkManager* m_net = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLineEdit*      m_nameEdit = nullptr;
    QListWidget*    m_hostList = nullptr;

    QWidget* buildMain();
    QWidget* buildMultiplayer();
    QWidget* buildLeaderboard();

    void refreshLeaderboard(QListWidget* list);
};
