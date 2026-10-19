#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QWidget;

namespace s7studio::viewers {

// One observed value as delivered to viewers: the probe that produced it, the
// runtime step it belongs to and its model time. Viewers only read samples;
// they never reach back into the runtime (ADR-011, ADR-013).
struct ObservedSample {
    QString probeName;
    long step = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
};

// The minimal contract between Studio and one observation viewer instance:
// identity, which probes it consumes, and the lifecycle operations the host
// applies to every viewer. How a viewer stores, aligns or draws its data is
// its own business.
class ObservationViewer {
public:
    virtual ~ObservationViewer() = default;

    // The top-level window showing this viewer. The ViewerManager owns it.
    [[nodiscard]] virtual auto widget() -> QWidget* = 0;
    // Whether samples of this probe belong to this viewer.
    [[nodiscard]] virtual auto subscribes(const QString& probeName) const -> bool = 0;
    virtual void appendObservedSample(const ObservedSample& sample) = 0;
    // Forgets the current run (a new run starts or the user cleared it).
    virtual void clearSamples() = 0;
    // Samples the runtime produced but Studio dropped because the UI fell behind.
    virtual void setDroppedSampleCount(std::uint64_t dropped) = 0;
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
};

} // namespace s7studio::viewers
