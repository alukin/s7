// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "observation/observation_source.hpp"
#include "observation/live_acquisition.hpp"
#include <s7/element_metadata.hpp>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QWidget;

namespace s7studio::viewers {
class ObserverPresentation;

// One observed value as delivered to viewers: the probe that produced it, the
// runtime step it belongs to and its model time. Viewers only read samples;
// they never reach back into the runtime (ADR-011, ADR-013).
struct ObservedSample {
    QString probeName;
    std::int64_t step = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
    std::uint64_t runId = 0;
    observation::ChannelId channel = 0;
    QString role;
    QString unit{};
};

struct InputBinding {
    QString role;
    observation::ChannelId channel;
};

struct ComparisonBinding {
    QString label;
    std::shared_ptr<const observation::ObservationSource> source;
    QVector<InputBinding> inputs;
};

// Presentation only. The host supplies a const data source and explicit input
// bindings. Windows do not own capture and never receive producer callbacks.
class ObservationViewer {
public:
    virtual ~ObservationViewer() = default;
    [[nodiscard]] virtual auto widget() -> QWidget* = 0;
    void attachSource(std::shared_ptr<const observation::ObservationSource> source,
                      QVector<InputBinding> bindings, bool clear = true);
    void setBindings(QVector<InputBinding> bindings) { bindings_ = std::move(bindings); }
    // One bounded page per UI turn. Returns whether more history remains.
    virtual bool refreshSource();
    virtual void attachLiveSource(std::shared_ptr<observation::LiveAcquisition>) {}
    virtual void setComparisonSources(const QVector<ComparisonBinding>&) {}
    void setPreviewDroppedCount(std::uint64_t dropped) { previewDropped_ = dropped; }
    [[nodiscard]] std::uint64_t previewDroppedCount() const { return previewDropped_; }
    [[nodiscard]] auto source() const { return source_; }
    // Presentation operations: never erase recorded data.
    virtual void clearSamples() = 0;
    virtual void beginRun(const QString&, bool) { clearSamples(); setDroppedSampleCount(0); }
    virtual void setDroppedSampleCount(std::uint64_t dropped) = 0;
protected:
    virtual void appendObservedSample(const ObservedSample& sample) = 0;
    // Called by a user clear; future updates start at the current source end.
    void discardPresentationHistory();
    [[nodiscard]] QVector<observation::ChannelId> selectedChannels(const QStringList& roles = {}) const {
        QVector<observation::ChannelId> result;
        if (roles.isEmpty()) {
            for (const auto& binding : bindings_) result.push_back(binding.channel);
        } else for (const auto& role : roles)
            for (const auto& binding : bindings_) if (binding.role == role) result.push_back(binding.channel);
        return result;
    }
    [[nodiscard]] QString sourceStatusText() const;
private:
    std::shared_ptr<const observation::ObservationSource> source_;
    QVector<InputBinding> bindings_;
    std::uint64_t cursor_ = 0;
    QHash<observation::ChannelId, QString> units_;
    QString readError_;
    std::uint64_t previewDropped_ = 0;
};

// What the host asks a viewer type to create: a stable instance id (the
// diagram observer's element id, or a key for ad-hoc viewers) and the probes
// feeding its fixed inputs, in input order.
struct ViewerInstanceSpec {
    QString instanceId;
    QStringList probeNames;
};

// A registered kind of viewer. typeId is the diagram observer element type
// ("Scope", "PhasePortrait", ...); see diagram/element_ports.cpp for its ports.
struct ViewerType {
    QString typeId;
    QString displayName;
    QString portSummary;   // shown in the element picker, e.g. "2→plot"
    QString description;
    // Probe-name suffixes of a diagram observer's fixed inputs ("x", "y"):
    // input i of observer <id> is observed as "<id>/<inputSuffixes[i]>".
    // Empty: the viewer takes every "<id>/..." probe as its own channel.
    QStringList inputSuffixes;
    // Creates a hidden, unparented viewer window; the caller owns it.
    std::function<ObservationViewer*(const ViewerInstanceSpec&)> create;
    std::optional<s7::ElementMetadata> documentation{};
    std::function<ObserverPresentation*(QWidget*)> createPresentation{};
};

} // namespace s7studio::viewers
