#include "logbuffer.h"

#include <QMutexLocker>

void LogBuffer::append(const QString& message)
{
    QMutexLocker lock(&this->mutex);
    this->messages.append(message);
    if(this->messages.size() > MAX_MESSAGES) {
        this->messages.erase(this->messages.begin(), this->messages.begin() + TRIM_MESSAGES);
    }
}

QStringList LogBuffer::snapshot() const
{
    QMutexLocker lock(&this->mutex);
    return this->messages;
}
