#pragma once

#include <QByteArray>
#include <QSaveFile>
#include <QString>

namespace s7studio {

// Never fall back to truncating the destination if a temporary file cannot be
// created. Success includes flushing and atomically committing the replacement.
inline auto writeFileAtomically(const QString& path, const QByteArray& bytes,
                                QString& error) -> bool
{
    error.clear();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

} // namespace s7studio
