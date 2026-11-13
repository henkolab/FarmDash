#include "Protocol.h"
#include <functional>
#include <QDataStream>
#include <qiodevice.h>

namespace Proto {

static QByteArray pack(MsgType t, std::function<void(QDataStream&)> w) {
    QByteArray ba;
    QDataStream ds(&ba, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_2);
    ds << quint8(t);
    w(ds);
    return ba;
}

QByteArray encodeHello(const QString& name)
{ return pack(Hello, [&](QDataStream& d){ d << name; }); }

QByteArray encodeWelcome(quint32 pid, quint32 seed)
{ return pack(Welcome, [&](QDataStream& d){ d << pid << seed; }); }

QByteArray encodeStartCountdown(qint32 ms)
{ return pack(StartCountdown, [&](QDataStream& d){ d << ms; }); }

QByteArray encodeInput(const PlayerInput& in)
{ return pack(InputUpdate, [&](QDataStream& d){ d << in.dx << in.dy << in.jump << in.shoot; }); }

QByteArray encodeFinished(qint64 t)
{ return pack(Finished, [&](QDataStream& d){ d << t; }); }

QByteArray encodeChat(const QString& s)
{ return pack(Chat, [&](QDataStream& d){ d << s; }); }

QByteArray encodeSnapshot(const QVector<PlayerSnap>& ps,
                          const QVector<EnemySnap>& es,
                          const QVector<PickupSnap>& pk,
                          const QVector<FriendlySnap>& fs,
                          const QVector<ProjectileSnap>& prs,
                          qint64 elapsed) {
    return pack(StateSnapshot, [&](QDataStream& d){
        d << elapsed;

        // Players (Optimized: Removed name)
        d << quint32(ps.size());
        for (const auto& p : ps) {
            d << p.id << p.x << p.y << p.facing << p.alive << p.hp;
        }

        // Enemies
        d << quint32(es.size());
        for (const auto& e : es) {
            d << e.id << e.kind << e.x << e.y << e.hp;
        }

        // Pickups
        d << quint32(pk.size());
        for (const auto& p : pk) {
            d << p.id << p.kind << p.x << p.y << p.taken;
        }

        // Friendlies (Newly Added)
        d << quint32(fs.size());
        for (const auto& f : fs) {
            d << f.id << f.kind << f.x << f.y;
        }

        // Projectiles (Newly Added)
        d << quint32(prs.size());
        for (const auto& pr : prs) {
            d << pr.id << pr.x << pr.y;
        }
    });
}

QByteArray encodeLeaderboard(const QVector<QPair<QString,qint64>>& rows) {
    return pack(Leaderboard, [&](QDataStream& d){
        d << quint32(rows.size());
        for (auto& r : rows) d << r.first << r.second;
    });
}

QByteArray frame(const QByteArray& payload) {
    QByteArray out;
    {
        QDataStream d(&out, QIODevice::WriteOnly);
        d.setVersion(QDataStream::Qt_6_2);
        d << quint32(payload.size());
    }
    out.append(payload);
    return out;
}

}