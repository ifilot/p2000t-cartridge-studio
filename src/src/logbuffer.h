#ifndef LOGBUFFER_H
#define LOGBUFFER_H

#include <QMutex>
#include <QStringList>

/** Thread-safe, bounded storage for messages emitted from GUI and worker threads. */
class LogBuffer {
public:
    void append(const QString& message);
    QStringList snapshot() const;

private:
    static constexpr int MAX_MESSAGES = 5000;
    static constexpr int TRIM_MESSAGES = 1000;

    mutable QMutex mutex;
    QStringList messages;
};

#endif // LOGBUFFER_H
