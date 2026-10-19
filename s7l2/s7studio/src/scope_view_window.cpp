#include "scope_view_window.hpp"

#include "qcp.h"

#include <QCheckBox>
#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPen>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryFile>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace s7studio::ui {

namespace {
constexpr qint64 kSpillIndexStride = 4096;
}

ScopeViewWindow::ScopeViewWindow(QString probeName,
                                 QWidget* parent,
                                 const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , probeName_(std::move(probeName))
{
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(900, 360);
    setWindowTitle(QStringLiteral("Scope - %1").arg(probeName_));
    resize(900, 520);

    auto* rootLayout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();

    autoScaleYCheck_ = new QCheckBox(QStringLiteral("Auto Y"), this);
    autoScaleYCheck_->setChecked(true);
    controls->addWidget(autoScaleYCheck_);
    fitAllTimeCheck_ = new QCheckBox(QStringLiteral("Fit full run"), this);
    fitAllTimeCheck_->setChecked(true);
    controls->addWidget(fitAllTimeCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear current"), this);
    exportButton_ = new QPushButton(QStringLiteral("Export Full CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    auto* saveButton = new QPushButton(QStringLiteral("Save Run…"), this);
    auto* openButton = new QPushButton(QStringLiteral("Open Run…"), this);
    auto* removeButton = new QPushButton(QStringLiteral("Remove comparisons"), this);
    saveButton->setToolTip(QStringLiteral("Save a snapshot of the current recording, including capture-loss warnings. Comparisons are saved separately."));
    openButton->setToolTip(QStringLiteral("Overlay saved results at their original model timestamps."));
    controls->addWidget(saveButton);
    controls->addWidget(openButton);
    controls->addWidget(removeButton);
    connect(saveButton, &QPushButton::clicked, this, &ScopeViewWindow::saveRunResults);
    connect(openButton, &QPushButton::clicked, this, &ScopeViewWindow::openRunResultsFile);
    connect(removeButton, &QPushButton::clicked, this, &ScopeViewWindow::clearComparisons);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);

    rootLayout->addLayout(controls);
    rootLayout->addWidget(statusLabel_);

    plot_ = new QCustomPlot(this);
    rootLayout->addWidget(plot_, 1);
    timeScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    timeScrollBar_->setRange(0, 10000);
    timeScrollBar_->setEnabled(false);
    timeScrollBar_->setToolTip(QStringLiteral("Scroll the visible time range. Zoom with the mouse wheel and pan by dragging in the plot."));
    rootLayout->addWidget(timeScrollBar_);

    configurePlot();

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &ScopeViewWindow::refreshPlot);
    repaintTimer_->start();

    connect(clearButton_, &QPushButton::clicked, this, &ScopeViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &ScopeViewWindow::exportRecordedCsv);
    connect(timeScrollBar_, &QScrollBar::valueChanged, this, &ScopeViewWindow::scrollToTimePosition);
    connect(fitAllTimeCheck_, &QCheckBox::toggled, this, [this](const bool fitAll) {
        if (fitAll && hasTimeRange_) {
            plot_->xAxis->setRange(dataMinTime_, dataMaxTime_ > dataMinTime_ ? dataMaxTime_ : dataMinTime_ + 1.0);
        }
        updateTimeScrollBar();
        const auto range = plot_->xAxis->range();
        for (auto it = series_.begin(); it != series_.end(); ++it) {
            setVisibleGraphData(it.value(), range.lower, range.upper, fitAll);
        }
        plot_->replot(QCustomPlot::rpQueuedReplot);
    });
    connect(plot_->xAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) {
                updateTimeScrollBar();
                if (!fitAllTimeCheck_->isChecked() && hasTimeRange_) {
                    for (auto it = series_.begin(); it != series_.end(); ++it) {
                        setVisibleGraphData(it.value(), range.lower, range.upper, false);
                    }
                    plot_->replot(QCustomPlot::rpQueuedReplot);
                }
            });
}

auto ScopeViewWindow::probeName() const -> QString
{
    return probeName_;
}

void ScopeViewWindow::appendSample(const double timeSeconds, const double value)
{
    appendSample(probeName_, timeSeconds, value);
}

void ScopeViewWindow::appendSample(const QString& channel, const double timeSeconds, const double value)
{
    auto& series = series_[QStringLiteral("live:") + channel];
    if (series.graphIndex < 0) {
        series.channelName = channel;
        addSeriesGraph(series);
    }
    if (!series.hasSamples) {
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
            updateStatus();
        }
    } else if (!series.spillFailed && series.time.size() >= kMaxInMemorySamplesPerSeries && !spillSeries(series)) {
        // Publish the loss warning after marking this channel incomplete.
        // Stop retrying the failed storage operation on every sample.
        series.spillFailed = true;
        updateStatus();
    }
    // The in-memory buffer is bounded either way so memory stays flat; once
    // spilling has failed there is no disk backup, so decimating here is
    // irreversible data loss, not a safe trim, which is why spillFailed also
    // keeps the warning above on screen instead of clearing after one retry.
    if (series.time.size() > kMaxInMemorySamplesPerSeries) {
        decimatePlotBuffer(series);
    }

    dirty_ = true;
}

void ScopeViewWindow::clearSamples()
{
    clearBuffers();
}

void ScopeViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    droppedSamples_ = dropped;
    updateStatus();
}

void ScopeViewWindow::updateStatus()
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
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void ScopeViewWindow::clearBuffers()
{
    for (auto it = series_.begin(); it != series_.end();) {
        if (it->comparisonLabel.isEmpty()) it = series_.erase(it);
        else ++it;
    }
    rebuildGraphs();
    hasTimeRange_ = false;
    timeScrollBar_->setEnabled(false);
    droppedSamples_ = 0;
    temporaryWriteError_.clear();
    updateStatus();

    for (int index = 0; index < plot_->graphCount(); ++index) {
        plot_->graph(index)->data()->clear();
    }

    dirty_ = true;
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::exportRecordedCsv()
{
    const bool anySpillFailed = std::any_of(series_.cbegin(), series_.cend(),
                                            [](const Series& series) { return series.spillFailed; });
    const bool incompleteComparison = std::any_of(series_.cbegin(), series_.cend(),
        [](const Series& series) { return !series.recordingWarning.isEmpty(); });
    if (anySpillFailed || droppedSamples_ > 0 || incompleteComparison) {
        QMessageBox::warning(this, QStringLiteral("Export may be incomplete"),
            QStringLiteral("Capture loss or a storage failure affected this recording or an imported run. "
                           "CSV contains only the retained samples. Save Run preserves the current recording's "
                           "capture-loss metadata; CSV does not."));
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export scope data"),
        QStringLiteral("%1_samples.csv").arg(probeName_),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }

    QString error;
    if (!writeRecordedCsv(file, &error)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), error);
    }
}

bool ScopeViewWindow::writeRecordedCsv(QIODevice& device, QString* error) const
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
        const auto prefix = field(series.comparisonLabel.isEmpty() ? probeName_ : series.comparisonLabel)
            + QLatin1Char(',') + field(series.channelName) + QLatin1Char(',');
        if (!visitSamples(series, [&](double time, double value) {
            out << prefix << time << ',' << value << '\n';
        }, error)) return false;
    }
    out.flush();
    if (out.status() != QTextStream::Ok) return fail(QStringLiteral("CSV write failed"));
    return true;
}

void ScopeViewWindow::addSeriesGraph(Series& series)
{
    plot_->addGraph();
    series.graphIndex = plot_->graphCount() - 1;
    auto* graph = plot_->graph(series.graphIndex);
    graph->setName(series.comparisonLabel.isEmpty() ? series.channelName
        : series.comparisonLabel + QStringLiteral(" / ") + series.channelName);
    static const QVector<QColor> colors{QColor("#1f77b4"), QColor("#d62728"), QColor("#2ca02c"),
        QColor("#9467bd"), QColor("#ff7f0e"), QColor("#17becf")};
    QPen pen(colors[series.graphIndex % colors.size()]);
    pen.setWidthF(1.6);
    if (!series.comparisonLabel.isEmpty()) pen.setStyle(Qt::DashLine);
    graph->setPen(pen);
}

void ScopeViewWindow::rebuildGraphs()
{
    plot_->clearGraphs();
    for (auto& series : series_) addSeriesGraph(series);
    hasTimeRange_ = false;
    dirty_ = true;
}

void ScopeViewWindow::clearComparisons()
{
    for (auto it = series_.begin(); it != series_.end();) {
        if (!it->comparisonLabel.isEmpty()) it = series_.erase(it);
        else ++it;
    }
    rebuildGraphs();
    updateStatus();
    refreshPlot();
}

bool ScopeViewWindow::visitSamples(const Series& series,
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

bool ScopeViewWindow::writeRunResults(QIODevice& device, QString* error) const
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();
    if (!device.isWritable()) return fail(QStringLiteral("Result destination is not writable"));
    QJsonArray channels;
    bool incomplete = droppedSamples_ > 0 || !temporaryWriteError_.isEmpty();
    for (const auto& series : series_) {
        if (!series.comparisonLabel.isEmpty() || !series.hasSamples) continue;
        const auto count = series.spillFile ? series.spilledSamples : series.time.size();
        channels.append(QJsonObject{{"name", series.channelName}, {"samples", QString::number(count)}});
        incomplete |= series.spillFailed;
    }
    if (channels.isEmpty()) return fail(QStringLiteral("There is no current recording to save"));
    if (channels.size() > 1024) return fail(QStringLiteral("Scope results support at most 1,024 channels"));
    const auto header = QJsonDocument(QJsonObject{{"scope", probeName_},
        {"saved_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"dropped_samples", QString::number(droppedSamples_)}, {"incomplete", incomplete},
        {"storage_warning", temporaryWriteError_}, {"channels", channels}}).toJson(QJsonDocument::Compact);
    if (header.size() > 65536) return fail(QStringLiteral("Result metadata exceeds 64 KiB"));
    QDataStream out(&device);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);
    out.writeRawData("S7SCOPE\n", 8);
    out << quint32(1) << quint32(header.size());
    out.writeRawData(header.constData(), header.size());
    bool validSamples = true;
    for (const auto& series : series_) {
        if (!series.comparisonLabel.isEmpty() || !series.hasSamples) continue;
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

bool ScopeViewWindow::openRunResults(QIODevice& device, const QString& label, QString* error)
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
    if (in.status() != QDataStream::Ok || version != 1 || headerSize > 65536)
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
    const bool incomplete = metadata.value("incomplete").toBool() || dropped > 0
        || !metadata.value("storage_warning").toString().isEmpty();
    // Stage into independent bounded buffers; malformed/failed imports cannot
    // replace or partially append to the displayed run.
    ScopeViewWindow staged(metadata.value("scope").toString());
    for (const auto& entry : channels) {
        const auto channel = entry.toObject();
        bool countOk = false;
        const auto count = channel.value("samples").toString().toULongLong(&countOk);
        const auto name = channel.value("name").toString();
        if (!channel.value("name").isString() || !countOk || count == 0
            || staged.series_.contains(QStringLiteral("live:") + name)
            || (!device.isSequential() && count > quint64(std::max<qint64>(0, device.bytesAvailable())) / 16))
            return fail(QStringLiteral("Invalid Scope channel or sample count"));
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
        if (incomplete) series.recordingWarning = QStringLiteral("incomplete recording; dropped %1 samples; %2")
            .arg(dropped).arg(metadata.value("storage_warning").toString());
        addSeriesGraph(series);
        series_.insert(QStringLiteral("comparison:%1:").arg(id) + series.channelName, std::move(series));
    }
    dirty_ = true;
    updateStatus();
    refreshPlot();
    return true;
}

void ScopeViewWindow::saveRunResults()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save current Scope run"),
        probeName_ + QStringLiteral(".s7scope"), QStringLiteral("S7 Scope results (*.s7scope)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    QString error;
    if (!file.open(QIODevice::WriteOnly)) error = file.errorString();
    else if (writeRunResults(file, &error) && !file.commit()) error = file.errorString();
    if (!error.isEmpty()) QMessageBox::critical(this, QStringLiteral("Save run failed"), error);
}

void ScopeViewWindow::openRunResultsFile()
{
    const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open runs for comparison"), {},
        QStringLiteral("S7 Scope results (*.s7scope)"));
    for (const auto& path : paths) {
        QFile file(path);
        QString error;
        if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
        else static_cast<void>(openRunResults(file, QFileInfo(path).completeBaseName(), &error));
        if (!error.isEmpty()) QMessageBox::critical(this, QStringLiteral("Open run failed"), error);
    }
}

auto ScopeViewWindow::spillSeries(Series& series) -> bool
{
    QSettings settings;
    temporaryDirectory_ = settings.value(QStringLiteral("scope/temporaryDirectory"), QDir::tempPath()).toString();
    if (!QDir().mkpath(temporaryDirectory_)) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data directory");
        updateStatus();
        return false;
    }
    auto file = QSharedPointer<QTemporaryFile>::create(
        QDir(temporaryDirectory_).filePath(QStringLiteral("s7scope-XXXXXX.bin")));
    file->setAutoRemove(true);
    if (!file->open()) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data file: %1").arg(file->errorString());
        updateStatus();
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
        updateStatus();
        return false;
    }
    series.spilledSamples = series.time.size();
    series.spillFile = std::move(file);
    updateStatus();
    return true;
}

auto ScopeViewWindow::appendToSpill(Series& series, const double timeSeconds, const double value) -> bool
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

void ScopeViewWindow::setVisibleGraphData(Series& series,
                                          const double lowerTime,
                                          const double upperTime,
                                          const bool fitFullRun)
{
    auto* graph = plot_->graph(series.graphIndex);
    if (!graph) {
        return;
    }
    if (!series.spillFile || fitFullRun || upperTime - lowerTime >= dataMaxTime_ - dataMinTime_) {
        graph->setData(series.time, series.value, true);
        if (!series.time.isEmpty() && series.lastTime > series.time.back()) {
            graph->addData(series.lastTime, series.lastValue);
        }
        return;
    }

    auto index = std::upper_bound(series.spillIndex.cbegin(), series.spillIndex.cend(), lowerTime,
        [](const double time, const Series::SpillIndexEntry& entry) { return time < entry.timeSeconds; });
    if (index != series.spillIndex.cbegin()) {
        --index;
    }
    if (series.spillIndex.isEmpty() || !series.spillFile->seek(series.spillIndex.isEmpty() ? 0 : index->byteOffset)) {
        graph->setData(series.time, series.value, true);
        if (!series.time.isEmpty() && series.lastTime > series.time.back()) {
            graph->addData(series.lastTime, series.lastValue);
        }
        return;
    }

    QDataStream in(series.spillFile.data());
    in.setByteOrder(QDataStream::LittleEndian);
    QVector<double> time;
    QVector<double> value;
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
            time.push_back(previousTime);
            value.push_back(previousValue);
            hasPrevious = false;
        }
        time.push_back(sampleTime);
        value.push_back(sampleValue);
    }
    series.spillFile->seek(series.spillFile->size());
    while (time.size() > kMaxInMemorySamplesPerSeries) {
        QVector<double> reducedTime;
        QVector<double> reducedValue;
        reducedTime.reserve((time.size() + 1) / 2);
        reducedValue.reserve((value.size() + 1) / 2);
        for (int sample = 0; sample < time.size(); sample += 2) {
            reducedTime.push_back(time[sample]);
            reducedValue.push_back(value[sample]);
        }
        time = std::move(reducedTime);
        value = std::move(reducedValue);
    }
    graph->setData(time, value, true);
}

void ScopeViewWindow::decimatePlotBuffer(Series& series)
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

void ScopeViewWindow::refreshPlot()
{
    if (!dirty_) {
        return;
    }

    dirty_ = false;
    double minTime = std::numeric_limits<double>::max();
    double maxTime = std::numeric_limits<double>::lowest();
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        const auto& series = it.value();
        if (auto* graph = plot_->graph(series.graphIndex)) {
            graph->setName(series.comparisonLabel.isEmpty() ? series.channelName
                : series.comparisonLabel + QStringLiteral(" / ") + series.channelName);
        }
        if (series.hasSamples) {
            minTime = std::min(minTime, series.firstTime);
            maxTime = std::max(maxTime, series.lastTime);
        }
    }
    if (minTime <= maxTime) {
        dataMinTime_ = minTime;
        dataMaxTime_ = maxTime;
        hasTimeRange_ = true;
        if (fitAllTimeCheck_->isChecked()) {
            plot_->xAxis->setRange(minTime, maxTime > minTime ? maxTime : minTime + 1.0);
        }
        updateTimeScrollBar();
        const auto range = plot_->xAxis->range();
        for (auto it = series_.begin(); it != series_.end(); ++it) {
            setVisibleGraphData(it.value(), range.lower, range.upper, fitAllTimeCheck_->isChecked());
        }
    }

    if (autoScaleYCheck_->isChecked()) {
        // Auto Y must preserve the requested time range, including zoom/pan.
        bool onlyEnlarge = false;
        for (int i = 0; i < plot_->graphCount(); ++i) {
            plot_->graph(i)->rescaleValueAxis(onlyEnlarge, true);
            onlyEnlarge = true;
        }
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::updateTimeScrollBar()
{
    if (!hasTimeRange_) {
        timeScrollBar_->setEnabled(false);
        return;
    }
    const double totalSpan = dataMaxTime_ - dataMinTime_;
    const double viewSpan = plot_->xAxis->range().size();
    const bool canScroll = totalSpan > 0.0 && viewSpan < totalSpan;
    timeScrollBar_->setEnabled(canScroll && !fitAllTimeCheck_->isChecked());
    const int pageStep = totalSpan > 0.0
        ? std::clamp(static_cast<int>(std::round(10000.0 * viewSpan / totalSpan)), 1, 10000)
        : 10000;
    updatingTimeScrollBar_ = true;
    timeScrollBar_->setPageStep(pageStep);
    const double scrollableSpan = std::max(0.0, totalSpan - viewSpan);
    const double normalized = scrollableSpan > 0.0
        ? (plot_->xAxis->range().lower - dataMinTime_) / scrollableSpan
        : 0.0;
    timeScrollBar_->setValue(std::clamp(static_cast<int>(std::round(normalized * 10000.0)), 0, 10000));
    updatingTimeScrollBar_ = false;
}

void ScopeViewWindow::scrollToTimePosition(const int position)
{
    if (updatingTimeScrollBar_ || !hasTimeRange_ || fitAllTimeCheck_->isChecked()) {
        return;
    }
    const double viewSpan = std::min(plot_->xAxis->range().size(), dataMaxTime_ - dataMinTime_);
    const double scrollableSpan = std::max(0.0, dataMaxTime_ - dataMinTime_ - viewSpan);
    const double lower = dataMinTime_ + scrollableSpan * static_cast<double>(position) / 10000.0;
    plot_->xAxis->setRange(lower, lower + viewSpan);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::configurePlot()
{
    plot_->legend->setVisible(true);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend | QCP::iSelectPlottables);
    plot_->xAxis->setLabel(QStringLiteral("time (s)"));
    plot_->yAxis->setLabel(probeName_);

}

} // namespace s7studio::ui
