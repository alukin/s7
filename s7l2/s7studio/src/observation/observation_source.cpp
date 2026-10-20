#include "observation/observation_source.hpp"
#include "observation/step_aligner.hpp"
#include <QDir>
#include <QIODevice>
#include <QTextStream>
#include <algorithm>
#include <limits>

namespace s7studio::observation {
ObservationSource::ObservationSource(std::uint64_t runId, qsizetype memoryLimit, QString temporaryDirectory)
    : runId_(runId), memoryLimit_(std::max(qsizetype(1), memoryLimit)),
      temporaryDirectory_(temporaryDirectory.isEmpty() ? QDir::tempPath() : std::move(temporaryDirectory)) {}

ChannelId ObservationSource::channel(const QString& name)
{
    std::lock_guard lock(mutex_);
    if (auto it = ids_.constFind(name); it != ids_.cend()) return *it;
    const auto id = static_cast<ChannelId>(channels_.size());
    channels_.push_back({id, name}); ids_.insert(name, id); return id;
}
QVector<Channel> ObservationSource::channels() const { std::lock_guard lock(mutex_); return channels_; }
SourceStatus ObservationSource::status() const { std::lock_guard lock(mutex_); return status_; }
void ObservationSource::addDeliveryLoss(std::uint64_t n) { std::lock_guard lock(mutex_); status_.deliveryDropped += n; }

bool ObservationSource::spill()
{
    file_ = std::make_unique<QTemporaryFile>(temporaryDirectory_ + QStringLiteral("/s7-observation-XXXXXX"));
    if (!file_->open()) { status_.storageError = file_->errorString(); file_.reset(); return false; }
    const auto bytes = memory_.size() * static_cast<qint64>(sizeof(Sample));
    if (file_->write(reinterpret_cast<const char*>(memory_.constData()), bytes) != bytes) {
        status_.storageError = QStringLiteral("Observation history write failed: ") + file_->errorString();
        file_.reset(); return false;
    }
    memory_.clear(); memory_.squeeze(); return true;
}
bool ObservationSource::append(const Sample& sample)
{
    std::lock_guard lock(mutex_);
    if (!status_.storageError.isEmpty()) return false;
    if (sample.channel >= static_cast<ChannelId>(channels_.size())) {
        status_.storageError = QStringLiteral("Unknown observation channel."); return false;
    }
    if (!file_ && memory_.size() >= memoryLimit_ && !spill()) return false;
    if (file_) {
        const auto offset = static_cast<qint64>(status_.samples) * static_cast<qint64>(sizeof(Sample));
        if ((file_->pos() != offset && !file_->seek(offset)) || file_->write(reinterpret_cast<const char*>(&sample), sizeof(Sample)) != sizeof(Sample)) {
            status_.storageError = QStringLiteral("Observation history write failed: ") + file_->errorString(); return false;
        }
    } else memory_.push_back(sample);
    ++status_.samples; return true;
}
Page ObservationSource::read(std::uint64_t cursor, qsizetype limit) const
{
    std::lock_guard lock(mutex_);
    Page page; page.next = std::min(cursor, status_.samples);
    // Both the caller and the source bound each query, even for hostile limits.
    const auto count = static_cast<qsizetype>(std::min<std::uint64_t>(status_.samples - page.next,
        static_cast<std::uint64_t>(std::clamp(limit, qsizetype(0), qsizetype(16'384)))));
    if (count == 0) return page;
    if (file_) {
        page.samples.resize(count);
        const auto bytes = count * static_cast<qint64>(sizeof(Sample));
        if (!file_->seek(static_cast<qint64>(page.next) * static_cast<qint64>(sizeof(Sample))) ||
            file_->read(reinterpret_cast<char*>(page.samples.data()), bytes) != bytes) {
            page.samples.clear(); page.error = QStringLiteral("Cannot read observation history: ") + file_->errorString(); return page;
        }
    } else page.samples = memory_.mid(static_cast<qsizetype>(page.next), count);
    page.next += static_cast<std::uint64_t>(count); return page;
}
bool ObservationSource::writeCsv(QIODevice& output, const QVector<ChannelId>& selection, QString* error) const
{
    const auto snapshot = status();
    const auto metadata = channels();
    QTextStream stream(&output); stream.setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
    stream << "run,channel,step,time,value\n";
    std::uint64_t cursor = 0;
    while (cursor < snapshot.samples) {
        const auto page = read(cursor, static_cast<qsizetype>(std::min<std::uint64_t>(4096, snapshot.samples - cursor)));
        if (!page.error.isEmpty() || page.next == cursor) { if (error) *error = page.error; return false; }
        for (const auto& sample : page.samples) {
            if (!selection.isEmpty() && !selection.contains(sample.channel)) continue;
            auto name = metadata[sample.channel].name; name.replace('"', "\"\"");
            stream << runId_ << ",\"" << name << "\"," << sample.step << ',' << sample.timeSeconds << ',' << sample.value << '\n';
        }
        cursor = page.next;
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) { if (error) *error = QStringLiteral("Observation CSV write failed."); return false; }
    return true;
}
namespace {
template <std::size_t N>
bool alignedCsv(const ObservationSource& source, QIODevice& output, const QVector<ChannelId>& inputs, QString* error)
{
    const auto snapshot = source.status();
    const auto channels = source.channels();
    for (auto channel : inputs) if (channel >= static_cast<ChannelId>(channels.size())) {
        if (error) *error = QStringLiteral("Unknown aligned input channel.");
        return false;
    }
    QTextStream stream(&output); stream.setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
    stream << "run,step,time";
    for (auto channel : inputs) {
        auto name = channels[channel].name; name.replace('"', "\"\""); stream << ",\"" << name << "\"";
    }
    stream << '\n';
    StepAligner<N> aligner;
    std::uint64_t cursor = 0;
    while (cursor < snapshot.samples) {
        const auto page = source.read(cursor, static_cast<qsizetype>(std::min<std::uint64_t>(4096, snapshot.samples - cursor)));
        if (!page.error.isEmpty() || page.next == cursor) { if (error) *error = page.error; return false; }
        for (const auto& sample : page.samples) {
            const auto index = inputs.indexOf(sample.channel);
            if (index < 0) continue;
            if (const auto point = aligner.add(static_cast<long>(sample.step), static_cast<std::size_t>(index), sample.value)) {
                stream << source.runId() << ',' << sample.step << ',' << sample.timeSeconds;
                for (auto value : *point) stream << ',' << value;
                stream << '\n';
            }
        }
        cursor = page.next;
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) { if (error) *error = QStringLiteral("Aligned CSV write failed."); return false; }
    return true;
}
}
bool ObservationSource::writeAlignedCsv(QIODevice& output, const QVector<ChannelId>& inputs, QString* error) const
{
    if (inputs.size() == 2 && inputs[0] != inputs[1]) return alignedCsv<2>(*this, output, inputs, error);
    if (inputs.size() == 3 && inputs[0] != inputs[1] && inputs[0] != inputs[2] && inputs[1] != inputs[2])
        return alignedCsv<3>(*this, output, inputs, error);
    if (error) *error = QStringLiteral("Aligned export requires two or three distinct inputs.");
    return false;
}
} // namespace s7studio::observation
