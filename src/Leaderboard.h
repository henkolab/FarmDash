#pragma once
#include <QString>
#include <QVector>
#include <QPair>

class Leaderboard {
public:
    static QVector<QPair<QString,qint64>> load();
    static void add(const QString& name, qint64 timeMs);
    static void clear();
private:
    static QString filePath();
};
