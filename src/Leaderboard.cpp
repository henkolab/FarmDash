#include "Leaderboard.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

QString Leaderboard::filePath() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + "/leaderboard.json";
}

QVector<QPair<QString,qint64>> Leaderboard::load() {
    QVector<QPair<QString,qint64>> rows;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return rows;
    auto doc = QJsonDocument::fromJson(f.readAll());
    for (auto v : doc.array()) {
        auto o = v.toObject();
        rows.push_back({ o.value("name").toString(), qint64(o.value("ms").toDouble()) });
    }
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b){ return a.second < b.second; });
    return rows;
}

void Leaderboard::add(const QString& name, qint64 ms) {
    auto rows = load();
    rows.push_back({name, ms});
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b){ return a.second < b.second; });
    while (rows.size() > 50) rows.pop_back();
    QJsonArray arr;
    for (auto& r : rows) {
        QJsonObject o; o["name"] = r.first; o["ms"] = double(r.second);
        arr.append(o);
    }
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly|QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void Leaderboard::clear() {
    QFile::remove(filePath());
}
