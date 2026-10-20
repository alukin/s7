#include "viewers/viewer_manager.hpp"

#include "scope_sample_mailbox.hpp"
#include "viewers/viewer_registry.hpp"

#include <QSet>

namespace s7studio::viewers {

ViewerManager::ViewerManager(const ViewerRegistry& registry, QObject* parent)
    : QObject(parent)
    , registry_(registry)
{
}

ViewerManager::~ViewerManager()
{
    closeAll();
}

auto ViewerManager::keyOf(const QString& typeId, const QString& instanceId) -> QString
{
    return typeId + QLatin1Char('\n') + instanceId;
}

auto ViewerManager::observerIdOf(const QString& probeName) -> QString
{
    const auto separator = probeName.indexOf(QLatin1Char('/'));
    return separator < 0 ? probeName : probeName.left(separator);
}

void ViewerManager::forget(const QString& key)
{
    if (instances_.remove(key) > 0) ++generation_;
}

auto ViewerManager::find(const QString& typeId, const QString& instanceId) const -> ObservationViewer*
{
    const auto it = instances_.constFind(keyOf(typeId, instanceId));
    return it != instances_.cend() && it->window ? it->viewer : nullptr;
}

auto ViewerManager::viewers() const -> QList<ObservationViewer*>
{
    QList<ObservationViewer*> result;
    for (const auto& instance : instances_) {
        if (instance.window) result.push_back(instance.viewer);
    }
    return result;
}

auto ViewerManager::ensure(const QString& typeId, const ViewerInstanceSpec& spec) -> ObservationViewer*
{
    if (spec.instanceId.isEmpty()) return nullptr;
    const auto key = keyOf(typeId, spec.instanceId);
    if (const auto it = instances_.constFind(key); it != instances_.cend()) {
        if (it->window) return it->viewer;
        forget(key); // destroyed behind our back; recreate below
    }
    const auto* type = registry_.find(typeId);
    if (!type || !type->create) return nullptr;
    auto* viewer = type->create(spec);
    if (!viewer) return nullptr;
    auto* window = viewer->widget();
    // An instance destroyed elsewhere must never receive another sample.
    connect(window, &QObject::destroyed, this, [this, key] { forget(key); });
    viewer->setDroppedSampleCount(dropped_);
    instances_.insert(key, Instance{typeId, spec.instanceId, window, viewer});
    ++generation_;
    return viewer;
}

auto ViewerManager::ensureObserver(const QString& typeId, const QString& observerId) -> ObservationViewer*
{
    const auto* type = registry_.find(typeId);
    if (!type) return nullptr;
    ViewerInstanceSpec spec{observerId, {}};
    for (const auto& suffix : type->inputSuffixes) {
        spec.probeNames.push_back(observerId + QLatin1Char('/') + suffix);
    }
    return ensure(typeId, spec);
}

auto ViewerManager::open(const QString& typeId, const ViewerInstanceSpec& spec) -> ObservationViewer*
{
    auto* viewer = ensure(typeId, spec);
    if (viewer) {
        auto* window = viewer->widget();
        window->show();
        window->raise();
        window->activateWindow();
    }
    return viewer;
}

auto ViewerManager::openObserver(const QString& typeId, const QString& observerId) -> ObservationViewer*
{
    auto* viewer = ensureObserver(typeId, observerId);
    if (viewer) static_cast<void>(open(typeId, ViewerInstanceSpec{observerId, {}}));
    return viewer;
}

void ViewerManager::deliver(const ScopeSampleBatch& batch,
                            const std::function<QString(const QString&)>& observerType)
{
    QSet<QString> resolvedObservers;
    QHash<QString, QList<ObservationViewer*>> routes;
    QHash<QString, QString> observerTypes;
    std::uint64_t routedGeneration = generation_;
    for (const auto& raw : batch.samples) {
        ObservedSample sample{QString::fromStdString(raw.probeName), raw.step, raw.timeSeconds, raw.value};
        const auto observerId = observerIdOf(sample.probeName);
        if (!resolvedObservers.contains(observerId)) {
            resolvedObservers.insert(observerId);
            const auto type = observerType(observerId);
            observerTypes.insert(observerId, type);
            static_cast<void>(ensureObserver(type, observerId));
        }
        if (routedGeneration != generation_) {
            routes.clear();
            routedGeneration = generation_;
        }
        auto route = routes.find(sample.probeName);
        if (route == routes.end()) {
            QList<ObservationViewer*> targets;
            for (const auto& instance : std::as_const(instances_)) {
                // Editing/replacing a diagram observer with the same ID must
                // not route its old run's samples into a different viewer type.
                if (instance.instanceId == observerId && !observerTypes.value(observerId).isEmpty()
                    && instance.typeId != observerTypes.value(observerId)) continue;
                if (instance.window && instance.viewer->subscribes(sample.probeName)) targets.push_back(instance.viewer);
            }
            route = routes.insert(sample.probeName, targets);
        }
        for (auto* viewer : std::as_const(*route)) viewer->appendObservedSample(sample);
    }
    if (batch.dropped > 0) setDroppedSampleCount(dropped_ + batch.dropped);
}

void ViewerManager::beginRun(const QString& previousRunLabel, bool preserveScopes)
{
    dropped_ = 0;
    for (auto* viewer : viewers()) viewer->beginRun(previousRunLabel, preserveScopes);
}

void ViewerManager::clearSamples()
{
    dropped_ = 0;
    for (auto* viewer : viewers()) viewer->clearSamples();
}

void ViewerManager::setDroppedSampleCount(const std::uint64_t dropped)
{
    dropped_ = dropped;
    for (auto* viewer : viewers()) viewer->setDroppedSampleCount(dropped_);
}

void ViewerManager::closeAll()
{
    const auto instances = std::exchange(instances_, {});
    ++generation_;
    for (const auto& instance : instances) {
        if (instance.window) {
            disconnect(instance.window, nullptr, this, nullptr);
            delete instance.window.data();
        }
    }
}

} // namespace s7studio::viewers
