#pragma once

#include "viewers/observation_viewer.hpp"
#include "observation/observation_session.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <functional>

namespace s7studio {
struct ScopeSampleBatch;
}

namespace s7studio::viewers {

class ViewerRegistry;

// Hosts read-only viewer instances and an independent observation session.
// Data delivery never creates windows. View lifetime and display clearing do
// not control retention; a newly opened viewer replays bounded source pages.
class ViewerManager : public QObject
{
    Q_OBJECT

public:
    explicit ViewerManager(const ViewerRegistry& registry, QObject* parent = nullptr);
    ~ViewerManager() override;

    // The instance for (typeId, instanceId), created hidden on first use.
    // Returns nullptr for an unknown type or a spec the type rejects.
    auto ensure(const QString& typeId, const ViewerInstanceSpec& spec) -> ObservationViewer*;
    // ensure() for a diagram observer: its probes follow from the type's inputs.
    auto ensureObserver(const QString& typeId, const QString& observerId) -> ObservationViewer*;
    // ensure(), then shows, raises and activates the window.
    auto open(const QString& typeId, const ViewerInstanceSpec& spec) -> ObservationViewer*;
    auto openObserver(const QString& typeId, const QString& observerId) -> ObservationViewer*;
    [[nodiscard]] auto find(const QString& typeId, const QString& instanceId) const -> ObservationViewer*;
    [[nodiscard]] auto viewers() const -> QList<ObservationViewer*>;

    // Retains delivered samples independently of windows, resolves diagram
    // bindings, then refreshes existing presentations with bounded pages.
    void deliver(const ScopeSampleBatch& batch, const std::function<QString(const QString&)>& observerType);
    void clearSamples();
    void beginRun(const QString& previousRunLabel, bool preserveScopes);
    void setDroppedSampleCount(std::uint64_t dropped);
    [[nodiscard]] auto droppedSampleCount() const -> std::uint64_t { return dropped_; }
    void closeAll();
    [[nodiscard]] auto source() const { return session_.source(); }
    void refreshViews();

    [[nodiscard]] static auto observerIdOf(const QString& probeName) -> QString;

private:
    struct Instance {
        QString typeId;
        QString instanceId;
        QPointer<QWidget> window;
        ObservationViewer* viewer = nullptr;
        ViewerInstanceSpec spec;
    };

    [[nodiscard]] static auto keyOf(const QString& typeId, const QString& instanceId) -> QString;
    void forget(const QString& key);

    QVector<InputBinding> bindingsFor(const Instance& instance) const;
    void bindViews(bool clear);
    observation::ObservationSession session_;
    QHash<QString, QString> recordedTypes_;
    const ViewerRegistry& registry_;
    QHash<QString, Instance> instances_;
    std::uint64_t dropped_ = 0;
    // Bumped whenever the set of instances changes; invalidates routing caches.
    std::uint64_t generation_ = 0;
};

} // namespace s7studio::viewers
