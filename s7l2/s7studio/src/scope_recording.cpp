// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "scope_recording.hpp"
#include "observation/run_metadata.hpp"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace s7studio::ui {

namespace {
constexpr qint64 kSpillIndexStride = 4096;
}

auto ScopeRecording::Series::displayName() const -> QString
{
    const auto label = channelName + (channelUnit.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(channelUnit));
    return comparisonLabel.isEmpty() ? label : comparisonLabel + QStringLiteral(" / ") + label;
}

ScopeRecording::ScopeRecording(QString scopeName)
    : scopeName_(std::move(scopeName))
{
}

void ScopeRecording::notifyStatus() const
{
    if (statusChanged_) statusChanged_();
}

bool ScopeRecording::setChannelUnit(const QString& channel, const QString& unit)
{
    auto& series = series_[QStringLiteral("live:") + channel];
    if (series.channelUnit == unit) return false;
    series.channelUnit = unit; return true;
}

void ScopeRecording::appendSample(const QString& channel, const double timeSeconds, const double value)
{
    auto& series = series_[QStringLiteral("live:") + channel];
    if (!series.hasSamples) {
        series.channelName = channel;
        series.firstTime = timeSeconds;
        series.hasSamples = true;
    }
    series.lastTime = timeSeconds;
    series.lastValue = value;
    // Keep the same sampling stride for old and newly arriving preview points.
    // Repeatedly thinning only the old prefix erases the start of long runs.
    const auto sampleIndex = series.receivedSamples++;
    if (sampleIndex % series.plotStride == 0) {
        series.time.push_back(timeSeconds);
        series.value.push_back(value);
    }
    if (series.spillFile) {
        if (!appendToSpill(series, timeSeconds, value)) {
            series.spillFailed = true;
            temporaryWriteError_ = QStringLiteral("temporary data write failed; recorded samples beyond this point are not full resolution");
            notifyStatus();
        }
    } else if (!series.spillFailed && series.time.size() >= kMaxInMemorySamplesPerSeries && !spillSeries(series)) {
        // Publish the loss warning after marking this channel incomplete.
        // Stop retrying the failed storage operation on every sample.
        series.spillFailed = true;
        notifyStatus();
    }
    // The in-memory buffer is bounded either way so memory stays flat; once
    // spilling has failed there is no disk backup, so decimating here is
    // irreversible data loss, not a safe trim, which is why spillFailed also
    // keeps the warning above on screen instead of clearing after one retry.
    if (series.time.size() > kMaxInMemorySamplesPerSeries) {
        decimatePlotBuffer(series);
    }
}

void ScopeRecording::clearLive()
{
    for (auto it = series_.begin(); it != series_.end();) {
        if (!it->isComparison()) it = series_.erase(it);
        else ++it;
    }
    droppedSamples_ = 0;
    temporaryWriteError_.clear();
    runMetadata_ = {};
    notifyStatus();
}

void ScopeRecording::archiveLive(const QString& runLabel)
{
    const auto sequence = ++comparisonSequence_;
    const bool incomplete = droppedSamples_ > 0 || !temporaryWriteError_.isEmpty()
        || std::any_of(series_.cbegin(), series_.cend(), [](const Series& series) {
            return !series.isComparison() && (series.spillFailed || !series.recordingWarning.isEmpty());
        });
    QMap<QString, Series> archived;
    for (auto it = series_.begin(); it != series_.end();) {
        if (it->isComparison()) { ++it; continue; }
        auto series = std::move(it.value());
        it = series_.erase(it);
        series.sourceMetadata = runMetadata_;
        series.comparisonLabel = QStringLiteral("%1 #%2%3").arg(runLabel).arg(sequence)
            .arg(incomplete ? QStringLiteral(" INCOMPLETE") : QString());
        if (incomplete) series.recordingWarning = QStringLiteral("incomplete recording; dropped %1 samples; %2")
            .arg(droppedSamples_).arg(temporaryWriteError_);
        const auto key = QStringLiteral("comparison:%1:%2").arg(sequence).arg(series.channelName);
        archived.insert(key, std::move(series));
    }
    for (auto it = archived.begin(); it != archived.end(); ++it) series_.insert(it.key(), std::move(it.value()));
    droppedSamples_ = 0;
    temporaryWriteError_.clear();
    runMetadata_ = {};
    notifyStatus();
}

void ScopeRecording::appendComparison(const ScopeRecording& other, const QString& label)
{
    const auto sequence = ++comparisonSequence_;
    for (const auto& original : other.series_) {
        if (original.isComparison()) continue;
        auto series = original;
        series.sourceMetadata = other.runMetadata_;
        series.comparisonLabel = QStringLiteral("%1 #%2%3").arg(label).arg(sequence)
            .arg(other.exportMayBeIncomplete() ? QStringLiteral(" INCOMPLETE") : QString());
        if (other.exportMayBeIncomplete()) series.recordingWarning = other.statusText();
        series_.insert(QStringLiteral("comparison:%1:%2").arg(sequence).arg(series.channelName), std::move(series));
    }
    notifyStatus();
}

void ScopeRecording::copyComparisonsFrom(const ScopeRecording& other)
{
    for (auto it = other.series_.cbegin(); it != other.series_.cend(); ++it)
        if (it->isComparison()) series_.insert(it.key(), it.value());
    comparisonSequence_ = std::max(comparisonSequence_, other.comparisonSequence_);
}

void ScopeRecording::clearComparisons()
{
    for (auto it = series_.begin(); it != series_.end();) {
        if (it->isComparison()) it = series_.erase(it);
        else ++it;
    }
    notifyStatus();
}

void ScopeRecording::setDroppedSampleCount(const std::uint64_t dropped)
{
    droppedSamples_ = dropped;
    notifyStatus();
}

auto ScopeRecording::statusText() const -> QString
{
    QStringList parts;
    bool spilled = false;
    bool spillFailed = false;
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        spilled |= static_cast<bool>(it->spillFile);
        spillFailed |= it->spillFailed;
        if (!it->recordingWarning.isEmpty()) {
            const auto warning = it->comparisonLabel + QStringLiteral(": ") + it->recordingWarning;
            if (!parts.contains(warning)) parts << warning;
        }
    }
    if (spilled) {
        parts << QStringLiteral("temporary disk buffer active");
    }
    if (spillFailed) {
        parts << QStringLiteral("WARNING: temporary disk buffer unavailable; recording is no longer full resolution");
    }
    if (droppedSamples_ > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(droppedSamples_);
    }
    if (!temporaryWriteError_.isEmpty()) {
        parts << temporaryWriteError_;
    }
    return parts.join(QStringLiteral(" | "));
}

auto ScopeRecording::exportMayBeIncomplete() const -> bool
{
    return droppedSamples_ > 0 || !temporaryWriteError_.isEmpty() || std::any_of(series_.cbegin(), series_.cend(), [](const Series& series) {
        return series.spillFailed || !series.recordingWarning.isEmpty();
    });
}

auto ScopeRecording::timeRange(double& minTime, double& maxTime) const -> bool
{
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (const auto& series : series_) {
        if (!series.hasSamples) continue;
        low = std::min(low, series.firstTime);
        high = std::max(high, series.lastTime);
    }
    if (low > high) return false;
    minTime = low;
    maxTime = high;
    return true;
}

bool ScopeRecording::writeRecordedCsv(QIODevice& device, QString* error) const
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!device.isWritable()) return fail(QStringLiteral("CSV destination is not writable"));
    if (error) error->clear();
    QTextStream out(&device);
    out.setLocale(QLocale::c());
    out.setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
    out.setRealNumberNotation(QTextStream::SmartNotation);
    const auto field = [](QString value) {
        if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) ||
            value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
            value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            return QLatin1Char('"') + value + QLatin1Char('"');
        }
        return value;
    };
    out << "scope,channel,time_s,value\n";
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        const auto& series = it.value();
        const auto prefix = field(series.comparisonLabel.isEmpty() ? scopeName_ : series.comparisonLabel)
            + QLatin1Char(',') + field(series.channelName) + QLatin1Char(',');
        if (!visitSamples(series, [&](double time, double value) {
            out << prefix << time << ',' << value << '\n';
        }, error)) return false;
    }
    out.flush();
    if (out.status() != QTextStream::Ok) return fail(QStringLiteral("CSV write failed"));
    return true;
}

bool ScopeRecording::visitSamples(const Series& series,
    const std::function<void(double, double)>& visitor, QString* error) const
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (series.spillFile) {
        const auto position = series.spillFile->pos();
        if (!series.spillFile->flush() || !series.spillFile->seek(0)) {
            series.spillFile->seek(position);
            return fail(QStringLiteral("Cannot read temporary recording"));
        }
        QDataStream in(series.spillFile.data());
        in.setByteOrder(QDataStream::LittleEndian);
        while (!in.atEnd()) {
            double time = 0, value = 0;
            in >> time >> value;
            if (in.status() != QDataStream::Ok) {
                series.spillFile->seek(position);
                return fail(QStringLiteral("Temporary recording read failed"));
            }
            visitor(time, value);
        }
        if (!series.spillFile->seek(position)) return fail(QStringLiteral("Cannot restore recording position"));
    } else {
        for (qsizetype i = 0; i < series.time.size(); ++i) visitor(series.time[i], series.value[i]);
    }
    return true;
}

bool ScopeRecording::writeRunResults(QIODevice& device, QString* error) const
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();
    if (!device.isWritable()) return fail(QStringLiteral("Result destination is not writable"));
    if (runMetadata_.contains("provenance") && !runMetadata_.value("provenance").isObject())
        return fail(QStringLiteral("Invalid run provenance"));
    if (!observation::validateRunProvenance(runMetadata_.value("provenance").toObject(), error)) return false;
    QJsonArray channels;
    bool incomplete = droppedSamples_ > 0 || !temporaryWriteError_.isEmpty();
    for (const auto& series : series_) {
        if (series.isComparison() || !series.hasSamples) continue;
        const auto count = series.spillFile ? series.spilledSamples : series.time.size();
        channels.append(QJsonObject{{"name", series.channelName}, {"unit", series.channelUnit}, {"samples", QString::number(count)}});
        incomplete |= series.spillFailed;
    }
    if (channels.isEmpty()) return fail(QStringLiteral("There is no current recording to save"));
    if (channels.size() > 1024) return fail(QStringLiteral("Scope results support at most 1,024 channels"));
    const auto header = QJsonDocument(QJsonObject{{"scope", scopeName_},
        {"saved_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"dropped_samples", QString::number(droppedSamples_)}, {"incomplete", incomplete},
        {"storage_warning", temporaryWriteError_}, {"channels", channels}, {"observation", runMetadata_}}).toJson(QJsonDocument::Compact);
    if (header.size() > kMaxResultHeaderBytes) return fail(QStringLiteral("Result metadata exceeds 16 MiB"));
    QDataStream out(&device);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);
    out.writeRawData("S7SCOPE\n", 8);
    out << kResultVersion << quint32(header.size());
    out.writeRawData(header.constData(), header.size());
    bool validSamples = true;
    for (const auto& series : series_) {
        if (series.isComparison() || !series.hasSamples) continue;
        double previous = -std::numeric_limits<double>::infinity();
        if (!visitSamples(series, [&](double time, double value) {
            validSamples &= std::isfinite(time) && std::isfinite(value) && time > previous;
            previous = time;
            out << time << value;
        }, error)) return false;
    }
    if (!validSamples) return fail(QStringLiteral("Recording contains non-finite values or unordered timestamps"));
    if (out.status() != QDataStream::Ok) return fail(QStringLiteral("Result write failed"));
    return true;
}

bool ScopeRecording::openRunResults(QIODevice& device, const QString& label, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();
    if (!device.isReadable()) return fail(QStringLiteral("Result source is not readable"));
    QDataStream in(&device);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::DoublePrecision);
    char magic[8];
    quint32 version = 0, headerSize = 0;
    if (in.readRawData(magic, 8) != 8 || QByteArray(magic, 8) != QByteArray("S7SCOPE\n", 8))
        return fail(QStringLiteral("Not an S7 Scope result"));
    in >> version >> headerSize;
    if (in.status() != QDataStream::Ok || version != kResultVersion || headerSize > kMaxResultHeaderBytes)
        return fail(QStringLiteral("Unsupported or invalid Scope result header"));
    QByteArray header(headerSize, '\0');
    if (in.readRawData(header.data(), header.size()) != header.size())
        return fail(QStringLiteral("Truncated Scope result header"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(header, &parseError);
    const auto metadata = document.object();
    const auto channels = metadata.value("channels").toArray();
    bool droppedOk = false;
    const auto dropped = metadata.value("dropped_samples").toString().toULongLong(&droppedOk);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !metadata.value("scope").isString() || !metadata.value("saved_utc").isString()
        || !metadata.value("incomplete").isBool() || !metadata.value("storage_warning").isString()
        || !droppedOk || channels.isEmpty() || channels.size() > 1024)
        return fail(QStringLiteral("Invalid Scope result metadata"));
    if (metadata.contains("observation") && !metadata.value("observation").isObject())
        return fail(QStringLiteral("Invalid observation metadata"));
    const auto observation = metadata.value("observation").toObject();
    if (observation.contains("provenance") && !observation.value("provenance").isObject())
        return fail(QStringLiteral("Invalid run provenance"));
    if (!s7studio::observation::validateRunProvenance(observation.value("provenance").toObject(), error)) return false;
    const bool incomplete = metadata.value("incomplete").toBool() || dropped > 0
        || !metadata.value("storage_warning").toString().isEmpty();
    // Stage into independent bounded buffers; malformed/failed imports cannot
    // replace or partially append to the displayed run.
    ScopeRecording staged(metadata.value("scope").toString());
    for (const auto& entry : channels) {
        const auto channel = entry.toObject();
        bool countOk = false;
        const auto count = channel.value("samples").toString().toULongLong(&countOk);
        const auto name = channel.value("name").toString();
        if ((channel.contains("unit") && !channel.value("unit").isString()) || !channel.value("name").isString() || !countOk || count == 0
            || staged.series_.contains(QStringLiteral("live:") + name)
            || (!device.isSequential() && count > quint64(std::max<qint64>(0, device.bytesAvailable())) / 16))
            return fail(QStringLiteral("Invalid Scope channel or sample count"));
        staged.setChannelUnit(name, channel.value("unit").toString());
        double previous = -std::numeric_limits<double>::infinity();
        for (quint64 i = 0; i < count; ++i) {
            double time = 0, value = 0;
            in >> time >> value;
            if (in.status() != QDataStream::Ok || !std::isfinite(time) || !std::isfinite(value) || time <= previous)
                return fail(QStringLiteral("Truncated, non-finite or unordered Scope samples"));
            staged.appendSample(name, time, value);
            previous = time;
        }
    }
    if (!in.atEnd()) return fail(QStringLiteral("Unexpected trailing Scope result data"));
    if (!staged.temporaryWriteError_.isEmpty()) return fail(staged.temporaryWriteError_);
    const auto id = ++comparisonSequence_;
    const auto comparisonLabel = QStringLiteral("%1 [%2] #%3%4")
        .arg(label.isEmpty() ? QStringLiteral("Saved run") : label, metadata.value("scope").toString())
        .arg(id).arg(incomplete ? QStringLiteral(" (INCOMPLETE)") : QString{});
    for (auto it = staged.series_.begin(); it != staged.series_.end(); ++it) {
        auto series = std::move(it.value());
        series.comparisonLabel = comparisonLabel;
        series.sourceMetadata = observation;
        series.sourceMetadata["incomplete"] = incomplete;
        series.sourceMetadata["storage_error"] = metadata.value("storage_warning").toString();
        if (incomplete) series.recordingWarning = QStringLiteral("incomplete recording; dropped %1 samples; %2")
            .arg(dropped).arg(metadata.value("storage_warning").toString());
        series_.insert(QStringLiteral("comparison:%1:").arg(id) + series.channelName, std::move(series));
    }
    notifyStatus();
    return true;
}

auto ScopeRecording::spillSeries(Series& series) -> bool
{
    QSettings settings;
    temporaryDirectory_ = settings.value(QStringLiteral("scope/temporaryDirectory"), QDir::tempPath()).toString();
    if (!QDir().mkpath(temporaryDirectory_)) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data directory");
        notifyStatus();
        return false;
    }
    auto file = QSharedPointer<QTemporaryFile>::create(
        QDir(temporaryDirectory_).filePath(QStringLiteral("s7scope-XXXXXX.bin")));
    file->setAutoRemove(true);
    if (!file->open()) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data file: %1").arg(file->errorString());
        notifyStatus();
        return false;
    }
    file->setAutoRemove(true);
    QDataStream out(file.data());
    out.setByteOrder(QDataStream::LittleEndian);
    for (int index = 0; index < series.time.size(); ++index) {
        if (index % kSpillIndexStride == 0) {
            series.spillIndex.push_back(Series::SpillIndexEntry{series.time[index], file->pos()});
        }
        out << series.time[index] << series.value[index];
    }
    if (out.status() != QDataStream::Ok || !file->flush()) {
        temporaryWriteError_ = QStringLiteral("cannot write temporary data file: %1").arg(file->errorString());
        notifyStatus();
        return false;
    }
    series.spilledSamples = series.time.size();
    series.spillFile = std::move(file);
    notifyStatus();
    return true;
}

auto ScopeRecording::appendToSpill(Series& series, const double timeSeconds, const double value) -> bool
{
    if (!series.spillFile) return false;
    // All recording readers restore the append position. Seeking to size()
    // for every sample flushes QFile's write buffer and can make capture fall
    // behind the worker; retain sequential buffered writes instead.
    if (series.spilledSamples % kSpillIndexStride == 0) {
        series.spillIndex.push_back(Series::SpillIndexEntry{timeSeconds, series.spillFile->pos()});
    }
    QDataStream out(series.spillFile.data());
    out.setByteOrder(QDataStream::LittleEndian);
    out << timeSeconds << value;
    if (out.status() != QDataStream::Ok) {
        return false;
    }
    ++series.spilledSamples;
    return true;
}

auto ScopeRecording::visiblePoints(const QString& key, const double lowerTime, const double upperTime,
                                   const bool fitFullRun) const -> QList<QPointF>
{
    const auto it = series_.constFind(key);
    if (it == series_.cend()) return {};
    const auto& series = it.value();
    const auto preview = [&series] {
        QList<QPointF> points;
        points.reserve(series.time.size() + 1);
        for (qsizetype i = 0; i < series.time.size(); ++i) points.push_back({series.time[i], series.value[i]});
        if (!series.time.isEmpty() && series.lastTime > series.time.back()) {
            points.push_back({series.lastTime, series.lastValue});
        }
        return points;
    };
    double dataMin = 0.0, dataMax = 0.0;
    static_cast<void>(timeRange(dataMin, dataMax));
    if (!series.spillFile || fitFullRun || upperTime - lowerTime >= dataMax - dataMin) {
        return preview();
    }

    auto index = std::upper_bound(series.spillIndex.cbegin(), series.spillIndex.cend(), lowerTime,
        [](const double time, const Series::SpillIndexEntry& entry) { return time < entry.timeSeconds; });
    if (index != series.spillIndex.cbegin()) {
        --index;
    }
    if (series.spillIndex.isEmpty() || !series.spillFile->seek(index->byteOffset)) {
        return preview();
    }

    QDataStream in(series.spillFile.data());
    in.setByteOrder(QDataStream::LittleEndian);
    QList<QPointF> points;
    double sampleTime = 0.0;
    double sampleValue = 0.0;
    double previousTime = 0.0;
    double previousValue = 0.0;
    bool hasPrevious = false;
    while (!in.atEnd()) {
        in >> sampleTime >> sampleValue;
        if (in.status() != QDataStream::Ok || sampleTime > upperTime) {
            break;
        }
        if (sampleTime < lowerTime) {
            previousTime = sampleTime;
            previousValue = sampleValue;
            hasPrevious = true;
            continue;
        }
        if (hasPrevious) {
            points.push_back({previousTime, previousValue});
            hasPrevious = false;
        }
        points.push_back({sampleTime, sampleValue});
    }
    series.spillFile->seek(series.spillFile->size());
    while (points.size() > kMaxInMemorySamplesPerSeries) {
        QList<QPointF> reduced;
        reduced.reserve((points.size() + 1) / 2);
        for (qsizetype sample = 0; sample < points.size(); sample += 2) reduced.push_back(points[sample]);
        points = std::move(reduced);
    }
    return points;
}

auto ScopeRecording::analysisSamples(const QString& key, const double lower, const double upper,
                                     const qsizetype limit, bool* truncated, QString* error) const -> QVector<QPointF>
{
    if (truncated) *truncated = false;
    if (error) error->clear();
    std::deque<QPointF> samples;
    const auto it = series_.constFind(key);
    if (it == series_.cend() || limit < 1 || !std::isfinite(lower) || !std::isfinite(upper) || lower > upper)
        return {};
    const auto& series = it.value();
    const auto finish = [&] {
        QVector<QPointF> result;
        result.reserve(static_cast<qsizetype>(samples.size()));
        for (const auto& point : samples) result.push_back(point);
        return result;
    };
    const auto add = [&](double time, double value) {
        if (time < lower || time > upper) return;
        if (samples.size() == static_cast<std::size_t>(limit)) {
            if (truncated) *truncated = true;
            samples.pop_front();
        }
        samples.push_back({time, value});
    };
    if (!series.spillFile) {
        for (qsizetype i = 0; i < series.time.size(); ++i) add(series.time[i], series.value[i]);
        return finish();
    }
    const auto position = series.spillFile->pos();
    if (!series.spillFile->flush()) {
        if (error) *error = QStringLiteral("Cannot flush temporary recording for analysis");
        return {};
    }
    // Start near the end of the requested interval when it exceeds the cap.
    auto end = std::upper_bound(series.spillIndex.cbegin(), series.spillIndex.cend(), upper,
        [](double time, const Series::SpillIndexEntry& entry) { return time < entry.timeSeconds; });
    auto start = end;
    const auto blocks = static_cast<qsizetype>(limit / kSpillIndexStride + 2);
    for (qsizetype i = 0; i < blocks && start != series.spillIndex.cbegin(); ++i) --start;
    if (start == series.spillIndex.cend() || !series.spillFile->seek(start->byteOffset)) {
        if (error) *error = QStringLiteral("Cannot seek temporary recording for analysis");
        series.spillFile->seek(position);
        return {};
    }
    if (start != series.spillIndex.cbegin() && start->timeSeconds > lower && truncated) *truncated = true;
    QDataStream in(series.spillFile.data());
    in.setByteOrder(QDataStream::LittleEndian);
    while (!in.atEnd()) {
        double time = 0, value = 0;
        in >> time >> value;
        if (in.status() != QDataStream::Ok) {
            if (error) *error = QStringLiteral("Temporary recording read failed");
            series.spillFile->seek(position);
            return {};
        }
        if (time > upper) break;
        add(time, value);
    }
    if (!series.spillFile->seek(position)) {
        if (error) *error = QStringLiteral("Cannot restore temporary recording position");
        return {};
    }
    return finish();
}

auto ScopeRecording::surroundingSamples(const QString& key, const double timeSeconds) const -> SurroundingSamples
{
    const auto it = series_.constFind(key);
    if (it == series_.cend() || !std::isfinite(timeSeconds)) return {};
    const auto& series = it.value();
    SurroundingSamples result;
    const auto consider = [&](const double time, const double value) {
        if (!std::isfinite(time)) return;
        if (time <= timeSeconds && (!result.before || time > result.before->x()))
            result.before = QPointF(time, value);
        if (time >= timeSeconds && (!result.after || time < result.after->x()))
            result.after = QPointF(time, value);
    };
    if (!series.spillFile || series.spillFailed) {
        const auto sample = std::lower_bound(series.time.cbegin(), series.time.cend(), timeSeconds);
        if (sample != series.time.cend()) consider(*sample, series.value[sample - series.time.cbegin()]);
        if (sample != series.time.cbegin()) {
            const auto previous = sample - 1;
            consider(*previous, series.value[previous - series.time.cbegin()]);
        }
        if (series.hasSamples) consider(series.lastTime, series.lastValue);
    }
    if (!series.spillFile || series.spillIndex.isEmpty()) return result;

    auto index = std::upper_bound(series.spillIndex.cbegin(), series.spillIndex.cend(), timeSeconds,
        [](const double time, const Series::SpillIndexEntry& entry) { return time < entry.timeSeconds; });
    if (index != series.spillIndex.cbegin()) --index;
    if (!series.spillFile->seek(index->byteOffset)) return result;
    QDataStream in(series.spillFile.data());
    in.setByteOrder(QDataStream::LittleEndian);
    for (qint64 sample = 0; sample <= kSpillIndexStride && !in.atEnd(); ++sample) {
        double time = 0.0, value = 0.0;
        in >> time >> value;
        if (in.status() != QDataStream::Ok) break;
        consider(time, value);
        if (time >= timeSeconds) break;
    }
    series.spillFile->seek(series.spillFile->size());
    return result;
}

void ScopeRecording::decimatePlotBuffer(Series& series)
{
    series.plotStride *= 2;
    QVector<double> time;
    QVector<double> value;
    time.reserve((series.time.size() + 1) / 2);
    value.reserve((series.value.size() + 1) / 2);
    for (int index = 0; index < series.time.size(); index += 2) {
        time.push_back(series.time[index]);
        value.push_back(series.value[index]);
    }
    series.time = std::move(time);
    series.value = std::move(value);
}

} // namespace s7studio::ui
