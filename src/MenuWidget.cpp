#include "MenuWidget.h"
#include "Leaderboard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QApplication>

MenuWidget::MenuWidget(QWidget* parent) : QWidget(parent) {
    m_net = new NetworkManager(this);
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildMain());          // 0
    m_pages->addWidget(buildMultiplayer());   // 1
    m_pages->addWidget(buildLeaderboard());   // 2
    auto* v = new QVBoxLayout(this);
    v->addWidget(m_pages);
    setStyleSheet(R"(
        QWidget { background: #2c1d10; color: #ffe9c2; font-size: 16px; }
        QPushButton { background: #b8541c; color: #fff; padding: 10px 18px;
                      border-radius: 8px; font-weight: bold; }
        QPushButton:hover { background: #d56a2a; }
        QLineEdit, QListWidget { background: #3d2a18; color: #ffe9c2;
                                  border: 1px solid #6b4628; padding: 6px; }
        QLabel#title { font-size: 36px; font-weight: bold; color: #ffd070; }
    )");
}

QString MenuWidget::playerName() const { return m_nameEdit ? m_nameEdit->text() : QString("Farmer"); }

QWidget* MenuWidget::buildMain() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addStretch(1);
    auto* title = new QLabel("FarmRun");
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    v->addWidget(title);
    auto* subtitle = new QLabel("Multiplayer farm-themed obstacle speedrunning");
    subtitle->setAlignment(Qt::AlignCenter);
    v->addWidget(subtitle);
    v->addSpacing(20);

    m_nameEdit = new QLineEdit("Farmer");
    m_nameEdit->setMaximumWidth(280);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* nm = new QLabel("Name:"); row->addWidget(nm); row->addWidget(m_nameEdit);
    row->addStretch(1);
    v->addLayout(row);
    v->addSpacing(20);

    auto button = [](const QString& t){ auto* b = new QPushButton(t); b->setMinimumWidth(280); return b; };
    auto* sp = button("Single Player");
    auto* mp = button("Multiplayer");
    auto* lb = button("Leaderboard");
    auto* qt = button("Quit");
    auto add = [&](QPushButton* b){ auto* h = new QHBoxLayout; h->addStretch(1); h->addWidget(b); h->addStretch(1); v->addLayout(h); };
    add(sp); add(mp); add(lb); add(qt);
    v->addStretch(2);

    connect(sp, &QPushButton::clicked, this, [this]{ emit requestSinglePlayer(playerName()); });
    connect(mp, &QPushButton::clicked, this, [this]{ m_pages->setCurrentIndex(1); m_net->startDiscovery(); });
    connect(lb, &QPushButton::clicked, this, [this]{ m_pages->setCurrentIndex(2); });
    connect(qt, &QPushButton::clicked, this, [this]{ qApp->quit(); });
    return w;
}

QWidget* MenuWidget::buildMultiplayer() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* title = new QLabel("Multiplayer"); title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter); v->addWidget(title);

    m_hostList = new QListWidget;
    v->addWidget(new QLabel("Discovered hosts on your LAN:"));
    v->addWidget(m_hostList, 1);

    auto* row = new QHBoxLayout;
    auto* host = new QPushButton("Host new game");
    auto* join = new QPushButton("Join selected");
    auto* refresh = new QPushButton("Refresh");
    auto* back = new QPushButton("Back");
    row->addWidget(host); row->addWidget(join); row->addWidget(refresh); row->addStretch(1); row->addWidget(back);
    v->addLayout(row);

    connect(m_net, &NetworkManager::hostsChanged, this, [this]{
        m_hostList->clear();
        for (auto& h : m_net->hosts())
            m_hostList->addItem(QString("%1   (%2:%3)").arg(h.name, h.address.toString()).arg(h.tcpPort));
    });
    connect(host,    &QPushButton::clicked, this, [this]{ emit requestHost(playerName()); });
    connect(refresh, &QPushButton::clicked, this, [this]{ m_net->stopDiscovery(); m_net->startDiscovery(); });
    connect(back,    &QPushButton::clicked, this, [this]{ m_net->stopDiscovery(); m_pages->setCurrentIndex(0); });
    connect(join,    &QPushButton::clicked, this, [this]{
        int row = m_hostList->currentRow();
        if (row < 0) return;
        auto h = m_net->hosts().value(row);
        emit requestJoin(h.address, h.tcpPort, playerName());
    });
    return w;
}

QWidget* MenuWidget::buildLeaderboard() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* title = new QLabel("Leaderboard"); title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter); v->addWidget(title);
    auto* list = new QListWidget;
    v->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    auto* clear = new QPushButton("Clear");
    auto* back  = new QPushButton("Back");
    row->addWidget(clear); row->addStretch(1); row->addWidget(back);
    v->addLayout(row);
    refreshLeaderboard(list);
    connect(clear, &QPushButton::clicked, this, [this, list]{ Leaderboard::clear(); refreshLeaderboard(list); });
    connect(back,  &QPushButton::clicked, this, [this, list]{ refreshLeaderboard(list); m_pages->setCurrentIndex(0); });
    return w;
}

void MenuWidget::refreshLeaderboard(QListWidget* list) {
    list->clear();
    auto rows = Leaderboard::load();
    int i = 1;
    for (auto& r : rows) {
        list->addItem(QString("%1.  %2  —  %3.%4 s")
            .arg(i++).arg(r.first)
            .arg(r.second/1000).arg((r.second%1000)/10, 2, 10, QChar('0')));
    }
    if (rows.isEmpty()) list->addItem("No runs yet — be the first!");
}
