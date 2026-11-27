// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/array_viewer.hpp"
#include "viewers/video_frame.hpp"
#include "viewers/waterfall_view.hpp"

#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace s7studio::viewers {
namespace {
class VideoView final : public QWidget {
public:
    explicit VideoView(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("videoPlayer.frame"));
        setAccessibleName(QStringLiteral("Video frame"));
        setMinimumSize(240, 135);
    }

    QString present(const s7::CommittedArraySample& sample)
    {
        frame_ = makeVideoFrame(sample);
        update();
        return QStringLiteral(" · %1 × %2 %3").arg(frame_->image.width()).arg(frame_->image.height())
            .arg(frame_->format == s7::ImagePixelFormat::Gray8 ? QStringLiteral("Gray8") : QStringLiteral("RGB8"));
    }

    void clear()
    {
        frame_.reset();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (frame_) painter.drawImage(fitVideoFrame(frame_->image.size(), rect()), frame_->image);
        else {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for a video frame"));
        }
    }

private:
    std::optional<VideoFrame> frame_;
};

class ArrayViewer final : public QWidget, public ObservationViewer, public ArrayObservationViewer {
public:
    ArrayViewer(diagram::ArrayObserverKind kind, const QString& title)
        : status_(new QLabel(this)), pause_(new QPushButton(this))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title);
        auto* layout = new QVBoxLayout(this);
        if (kind == diagram::ArrayObserverKind::Waterfall) {
            waterfall_ = new WaterfallView(this);
            layout->addWidget(waterfall_, 1);
        } else {
            video_ = new VideoView(this);
            layout->addWidget(video_, 1);
        }
        auto* footer = new QHBoxLayout;
        status_->setObjectName(QStringLiteral("arrayViewer.status"));
        status_->setWordWrap(true);
        footer->addWidget(status_, 1);
        pause_->setObjectName(QStringLiteral("arrayViewer.pause"));
        pause_->setText(QStringLiteral("Pause display"));
        pause_->setCheckable(true);
        footer->addWidget(pause_);
        layout->addLayout(footer);
        connect(pause_, &QPushButton::toggled, this, [this](bool paused) {
            pause_->setText(paused ? QStringLiteral("Resume display") : QStringLiteral("Pause display"));
            refreshSource();
        });
        clearSamples();
        resize(waterfall_ ? QSize(760, 465) : QSize(640, 410));
    }

    QWidget* widget() override { return this; }

    void attachArraySource(std::shared_ptr<s7::ArrayMailbox> mailbox) override
    {
        if (mailbox_ == mailbox) return;
        mailbox_ = std::move(mailbox);
        clearSamples();
        pause_->setChecked(false);
    }

    void clearSamples() override
    {
        summary_ = mailbox_ ? QStringLiteral("Waiting for a committed array.") : QStringLiteral("No array source is attached.");
        displayedStep_.reset();
        if (waterfall_) waterfall_->clear();
        if (video_) video_->clear();
        status_->setText(summary_);
    }

    void setDroppedSampleCount(std::uint64_t) override {}

    bool refreshSource() override
    {
        if (!mailbox_) return false;
        const auto sample = pause_->isChecked() ? std::nullopt : mailbox_->latest();
        if (sample && sample->domain_step != displayedStep_) {
            displayedStep_ = sample->domain_step;
            const auto& value = sample->value;
            try {
                summary_ = QStringLiteral("Committed domain step %1").arg(sample->domain_step);
                if (value.origin) summary_ += QStringLiteral(" · Source sequence %1").arg(value.origin->sequence);
                if (waterfall_) {
                    if (value.spectrum) summary_ += waterfall_->present(*sample);
                    else summary_ = QStringLiteral("The committed vector has no spectrum metadata.");
                } else summary_ += video_->present(*sample);
            } catch (const std::exception& error) {
                summary_ = QString::fromUtf8(error.what());
            }
        }
        const auto counts = mailbox_->counters();
        status_->setText(QStringLiteral("%1\nReplaced %2 · Admission skips %3")
            .arg(summary_).arg(counts.replaced).arg(counts.admission_skips));
        return false;
    }

private:
    void appendObservedSample(const ObservedSample&) override {}

    QLabel* status_;
    QPushButton* pause_;
    WaterfallView* waterfall_{};
    VideoView* video_{};
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
