#pragma once

#include <QObject>
#include <QFutureWatcher>

namespace LAStudio {

// Owns only disposable workflow cache trees. Models, user projects, exports,
// logs, and downloaded runtimes are intentionally outside this service.
class CacheLifecycleService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheBytesChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(bool clearing READ clearing NOTIFY clearingChanged)

public:
    explicit CacheLifecycleService(QObject *parent = nullptr);

    qint64 cacheBytes() const { return m_cacheBytes; }
    bool refreshing() const { return m_refreshing; }
    bool clearing() const { return m_clearing; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clear();

signals:
    void cacheBytesChanged();
    void refreshingChanged();
    void clearingChanged();
    void errorOccurred(const QString &message);
    void cleared();

private:
    qint64 m_cacheBytes = 0;
    bool m_refreshing = false;
    bool m_clearing = false;
    QFutureWatcher<qint64> m_refreshWatcher;
    QFutureWatcher<QString> m_clearWatcher;
};

} // namespace LAStudio
