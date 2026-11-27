// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/array_viewer.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace s7studio::viewers {
namespace {
class ArrayViewer final : public QWidget, public ObservationViewer, public ArrayObservationViewer {
public:
    ArrayViewer(diagram::ArrayObserverKind kind, const QString& title)
        : kind_(kind), status_(new QLabel(this))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title);
        auto* layout = new QVBoxLayout(this);
        status_->setObjectName(QStringLiteral("arrayViewer.status"));
        status_->setWordWrap(true);
        layout->addWidget(status_);
        clearSamples();
        resize(440, 120);
    }

    QWidget* widget() override { return this; }

    void attachArraySource(std::shared_ptr<s7::ArrayMailbox> mailbox) override
    {
        if (mailbox_ == mailbox) return;
        mailbox_ = std::move(mailbox);
        clearSamples();
    }

    void clearSamples() override
    {
        summary_ = mailbox_ ? QStringLiteral("Waiting for a committed array.") : QStringLiteral("No array source is attached.");
        displayedStep_.reset();
        status_->setText(summary_);
    }

    void setDroppedSampleCount(std::uint64_t) override {}

    bool refreshSource() override
    {
        if (!mailbox_) return false;
        if (const auto sample = mailbox_->latest(); sample && sample->domain_step != displayedStep_) {
            displayedStep_ = sample->domain_step;
            const auto& value = sample->value;
            if (kind_ == diagram::ArrayObserverKind::Waterfall && !value.spectrum) {
                summary_ = QStringLiteral("The committed vector has no spectrum metadata.");
            } else if (kind_ == diagram::ArrayObserverKind::VideoPlayer && !value.image) {
                summary_ = QStringLiteral("The committed frame has no pixel format metadata.");
            } else {
                summary_ = QStringLiteral("Committed domain step %1").arg(sample->domain_step);
                if (value.origin) summary_ += QStringLiteral(" · Source sequence %1").arg(value.origin->sequence);
            }
        }
        const auto counts = mailbox_->counters();
        status_->setText(QStringLiteral("%1\nReplaced %2 · Admission skips %3")
            .arg(summary_).arg(counts.replaced).arg(counts.admission_skips));
        return false;
    }

private:
    void appendObservedSample(const ObservedSample&) override {}

    const diagram::ArrayObserverKind kind_;
    QLabel* status_;
    QString summary_;
    std::optional<std::uint64_t> displayedStep_;
    std::shared_ptr<s7::ArrayMailbox> mailbox_;
};
} // namespace

ViewerType arrayViewerType(const diagram::ArrayObserverKind kind)
{
    const bool waterfall = kind == diagram::ArrayObserverKind::Waterfall;
    const auto id = waterfall ? QStringLiteral("Waterfall") : QStringLiteral("VideoPlayer");
    const auto name = waterfall ? QStringLiteral("Waterfall") : QStringLiteral("Video Player");
    const auto role = waterfall ? QStringLiteral("spectrum") : QStringLiteral("frame");
    ViewerType type{id, name, role, {}, {role}, [kind, name](const ViewerInstanceSpec& spec) {
        return new ArrayViewer(kind, QStringLiteral("%1 · %2").arg(name, spec.instanceId));
    }};
    type.arrayKind = kind;
    return type;
}

} // namespace s7studio::viewers
