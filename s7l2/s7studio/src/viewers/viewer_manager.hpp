#pragma once

#include "viewers/observation_viewer.hpp"

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

// Owns the open viewer instances of one Studio window and routes observed
// samples to them. Instances are keyed by (type, instance id), so a diagram
// can hold any number of viewers, including several of one type. A viewer
// records from its first delivered sample, even while hidden; closing its
// window only hides it, and closeAll() (diagram switch, shutdown) destroys it.
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

    // Routes a batch in production order. observerType maps an observer id
    // (the probe name up to its first '/') to the viewer type that records
    // it; each observer is resolved once per batch and created on demand.
    void deliver(const ScopeSampleBatch& batch, const std::function<QString(const QString&)>& observerType);
    void clearSamples();
    void beginRun(const QString& previousRunLabel, bool preserveScopes);
    void setDroppedSampleCount(std::uint64_t dropped);
    [[nodiscard]] auto droppedSampleCount() const -> std::uint64_t { return dropped_; }
    void closeAll();

    [[nodiscard]] static auto observerIdOf(const QString& probeName) -> QString;

private:
    struct Instance {
        QString typeId;
        QString instanceId;
        QPointer<QWidget> window;
        ObservationViewer* viewer = nullptr;
    };

    [[nodiscard]] static auto keyOf(const QString& typeId, const QString& instanceId) -> QString;
    void forget(const QString& key);

    const ViewerRegistry& registry_;
    QHash<QString, Instance> instances_;
    std::uint64_t dropped_ = 0;
    // Bumped whenever the set of instances changes; invalidates routing caches.
    std::uint64_t generation_ = 0;
};

} // namespace s7studio::viewers
