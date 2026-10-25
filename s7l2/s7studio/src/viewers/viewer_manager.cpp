// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/viewer_manager.hpp"

#include "scope_sample_mailbox.hpp"
#include "viewers/viewer_registry.hpp"
#include "live_oscilloscope_window.hpp"

#include <QTimer>

namespace s7studio::viewers {

ViewerManager::ViewerManager(const ViewerRegistry& registry, QObject* parent)
    : QObject(parent)
    , registry_(registry)
{
    auto* timer = new QTimer(this);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, &ViewerManager::refreshViews);
    timer->start();
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
    instances_.insert(key, Instance{typeId, spec.instanceId, window, viewer, spec});
    if (auto* live = dynamic_cast<ui::LiveOscilloscopeWindow*>(viewer)) {
        live->setTriggerController([this, id = spec.instanceId](const observation::TriggerSettings& settings, QString* error) { return setObserverTrigger(id, settings, error, false); });
        live->setTriggerSettings(triggers_.value(spec.instanceId));
    }
    emit viewerCreated(typeId, spec.instanceId, viewer);
    if (typeId == QStringLiteral("LiveOscilloscope")) viewer->attachLiveSource(liveSources_.value(spec.instanceId));
    else viewer->attachSource(source(), bindingsFor(instances_[key], source()));
    viewer->setPreviewDroppedCount(externalSource_ ? dropped_ : 0);
    bindComparisons(instances_[key]);
    viewer->refreshSource();
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
    if (externalSource_ || !session_.append(batch)) return;
    for (const auto& sample : batch.samples) {
        const auto id = observerIdOf(QString::fromStdString(sample.probeName));
        if (!recordedTypes_.contains(id)) recordedTypes_.insert(id, observerType(id));
    }
    bindViews(false);
    refreshViews();
    dropped_ = source()->status().captureDropped;
}

void ViewerManager::present(std::shared_ptr<const observation::ObservationSource> data,
                            std::uint64_t previewDropped,
                            const std::function<QString(const QString&)>& observerType)
{
    if (!data) return;
    if (externalSource_ != data) {
        externalSource_ = std::move(data);
        dropped_ = 0;
        recordedTypes_.clear();
    }
    for (const auto& channel : source()->channels()) {
        const auto id = observerIdOf(channel.name);
        if (!recordedTypes_.contains(id)) recordedTypes_.insert(id, observerType(id));
    }
    dropped_ += previewDropped;
    bindViews(false);
    for (auto* viewer : viewers()) viewer->setPreviewDroppedCount(dropped_);
    refreshViews();
}

QVector<InputBinding> ViewerManager::bindingsFor(const Instance& instance,
    const std::shared_ptr<const observation::ObservationSource>& data) const
{
    QVector<InputBinding> bindings;
    if (data == source() && recordedTypes_.contains(instance.instanceId) && recordedTypes_.value(instance.instanceId) != instance.typeId)
        return bindings;
    const auto* type = registry_.find(instance.typeId);
    if (!type) return bindings;
    const auto channels = data->channels();
    for (const auto& channel : channels) {
        if (instance.spec.probeNames.isEmpty()) {
            if (observerIdOf(channel.name) != instance.instanceId) continue;
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            bindings.push_back({separator < 0 ? channel.name : channel.name.mid(separator + 1), channel.id});
        } else {
            const auto index = instance.spec.probeNames.indexOf(channel.name);
            if (index < 0) continue;
            bindings.push_back({type->inputSuffixes.value(index, channel.name), channel.id});
        }
    }
    return bindings;
}
void ViewerManager::bindComparisons(const Instance& instance)
{
    QVector<ComparisonBinding> comparisons;
    for (const auto& run : session_.retainedRuns())
        comparisons.push_back({run.label, run.source, bindingsFor(instance, run.source)});
    instance.viewer->setComparisonSources(comparisons);
}
void ViewerManager::bindViews(bool clear)
{
    for (const auto& instance : std::as_const(instances_)) {
        if (!instance.window) continue;
        if (instance.typeId == QStringLiteral("LiveOscilloscope")) continue;
        if (instance.viewer->source() != source())
            instance.viewer->attachSource(source(), bindingsFor(instance, source()), clear);
        else instance.viewer->setBindings(bindingsFor(instance, source()));
        bindComparisons(instance);
    }
}
void ViewerManager::refreshViews()
{
    for (auto* viewer : viewers()) viewer->refreshSource();
}

void ViewerManager::beginRun(const QString& previousRunLabel, bool preserveScopes)
{
    dropped_ = 0;
    if (preserveScopes) session_.retain(source(), previousRunLabel);
    for (auto* viewer : viewers()) viewer->beginRun(previousRunLabel, preserveScopes);
    session_.beginRun();
    externalSource_.reset();
    recordedTypes_.clear();
    presentLive({});
    bindViews(false);
}

void ViewerManager::presentLive(observation::LiveSources sources)
{
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        if (liveSources_.value(it.key()) != it.value()) {
            auto settings = triggers_.value(it.key());
            if (settings.channel >= static_cast<std::size_t>(it.value()->channels().size())) settings.channel = 0;
            it.value()->configureTrigger(settings); triggers_[it.key()] = settings;
        }
    liveSources_ = std::move(sources);
    for (const auto& instance : std::as_const(instances_))
        if (instance.window && instance.typeId == QStringLiteral("LiveOscilloscope")) {
            if (auto* live = dynamic_cast<ui::LiveOscilloscopeWindow*>(instance.viewer)) live->setTriggerSettings(triggers_.value(instance.instanceId));
            instance.viewer->attachLiveSource(liveSources_.value(instance.instanceId));
        }
}
bool ViewerManager::setObserverTrigger(const QString& id, const observation::TriggerSettings& settings, QString* error, bool updateViewer)
{
    if (const auto source = liveSources_.value(id); source && triggers_.value(id) != settings && !source->configureTrigger(settings, error)) return false;
    const bool changed = triggers_.value(id) != settings;
    triggers_[id] = settings;
    if (changed) emit observerSettingsChanged(id);
    if (auto* live = dynamic_cast<ui::LiveOscilloscopeWindow*>(find("LiveOscilloscope", id)); updateViewer && live) live->setTriggerSettings(settings);
    return true;
}

void ViewerManager::clearSamples()
{
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
    session_.beginRun();
    session_.clearHistory();
    externalSource_.reset();
    recordedTypes_.clear();
    liveSources_.clear();
    triggers_.clear();
    dropped_ = 0;
    ++generation_;
    for (const auto& instance : instances) {
        if (instance.window) {
            disconnect(instance.window, nullptr, this, nullptr);
            delete instance.window.data();
        }
    }
}

} // namespace s7studio::viewers
