#include "MainWindow.h"

MainWindow::MainWindow() {
    setWindowTitle("FarmRun");
    m_stack = new QStackedWidget(this);
    m_menu  = new MenuWidget;
    m_game  = new GameWidget;
    m_stack->addWidget(m_menu);
    m_stack->addWidget(m_game);
    setCentralWidget(m_stack);
    wireSignals();
}

void MainWindow::wireSignals() {
    connect(m_menu, &MenuWidget::requestSinglePlayer, this, [this](QString name){
        m_stack->setCurrentWidget(m_game);
        m_game->startSinglePlayer(name);
    });
    connect(m_menu, &MenuWidget::requestHost, this, [this](QString name){
        if (m_menu->network()->startHost(QString("%1's farm").arg(name))) {
            m_stack->setCurrentWidget(m_game);
            m_game->startAsHost(m_menu->network(), name);
        }
    });
    connect(m_menu, &MenuWidget::requestJoin, this, [this](QHostAddress a, quint16 p, QString name){
        m_menu->network()->joinHost(a, p, name);
        m_stack->setCurrentWidget(m_game);
        m_game->startAsClient(m_menu->network(), name);
    });
    connect(m_game, &GameWidget::exitToMenu, this, [this]{
        m_stack->setCurrentWidget(m_menu);
    });
}
