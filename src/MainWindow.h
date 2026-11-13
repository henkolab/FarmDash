#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include "MenuWidget.h"
#include "GameWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
private:
    QStackedWidget* m_stack = nullptr;
    MenuWidget*     m_menu  = nullptr;
    GameWidget*     m_game  = nullptr;
    void wireSignals();
};
