// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/array_viewer.hpp"
#include "viewers/video_frame.hpp"
#include "viewers/waterfall_view.hpp"

#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScopedValueRollback>
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

    std::optional<ArrayPrintSnapshot> printSnapshot() const
    {
        if (!frame_) return std::nullopt;
        const auto& frame = *frame_;
        ArrayPrintSnapshot page;
        page.details = {
            QStringLiteral("Committed domain step %1 · Domain rate %2")
                .arg(frame.domainStep).arg(QString::fromStdString(frame.domainRate.to_string())),
            QStringLiteral("%1 × %2 pixels · %3 · Source row stride %4 bytes")
                .arg(frame.image.width()).arg(frame.image.height())
                .arg(frame.format == s7::ImagePixelFormat::Gray8 ? QStringLiteral("Gray8") : QStringLiteral("RGB8"))
                .arg(frame.sourceStride)
        };
        if (frame.origin) {
            page.details.append(QStringLiteral("Source %1 / epoch %2 / sequence %3")
                .arg(frame.origin->source_id).arg(frame.origin->epoch).arg(frame.origin->sequence));
        }
        if (frame.timing && frame.timing->presentation_time) {
            const auto& pts = *frame.timing->presentation_time;
            page.details.append(QStringLiteral("Presentation time %1 ticks at %2/%3 ticks/s")
                .arg(pts.ticks).arg(pts.ticks_per_second.numerator).arg(pts.ticks_per_second.denominator));
        }
        if (frame.timing && frame.timing->discontinuity_before) {
            page.details.append(QStringLiteral("Media discontinuity before this frame"));
        }
        page.draw = [image = frame.image](QPainter& painter, const QRect& bounds) {
            painter.drawImage(fitVideoFrame(image.size(), bounds), image);
        };
        return page;
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
        auto* print = new QPushButton(QStringLiteral("Print…"), this);
        print->setObjectName(QStringLiteral("arrayViewer.print"));
        auto* printMenu = new QMenu(print);
        auto* pdf = printMenu->addAction(QStringLiteral("Save as PDF…"), this, [this] { printDisplayed(true); });
        pdf->setObjectName(QStringLiteral("arrayViewer.savePdf"));
        printMenu->addAction(QStringLiteral("Print to printer…"), this, [this] { printDisplayed(false); });
        print->setMenu(printMenu);
        footer->addWidget(print);
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

    void printDisplayed(bool toPdf)
    {
        // Viewer images already own their pixels. Pin at most one extra display
        // snapshot across nested dialogs without retaining a graph buffer.
        static bool printInProgress = false;
        if (printInProgress) return;
        const QScopedValueRollback guard(printInProgress, true);
        auto snapshot = video_ ? video_->printSnapshot() : waterfall_->printSnapshot();
        if (!snapshot) {
            QMessageBox::information(this, QStringLiteral("Print viewer"),
                QStringLiteral("A committed frame or spectrum must be displayed first."));
            return;
        }

        snapshot->title = windowTitle();
        const QPointer<QWidget> owner(this);
        QString error;
        if (toPdf) {
            auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save viewer as PDF"), {},
                QStringLiteral("PDF files (*.pdf)"), nullptr, QFileDialog::DontUseNativeDialog);
            if (!owner || path.isEmpty()) return;
            if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
            error = saveArrayPdf(*snapshot, path);
        } else {
            QPrinter printer(QPrinter::HighResolution);
            QPrintDialog dialog(&printer);
            dialog.setWindowTitle(QStringLiteral("Print viewer"));
            dialog.setOptions(QAbstractPrintDialog::PrintCollateCopies);
            if (dialog.exec() != QDialog::Accepted || !owner) return;
            error = printArraySnapshot(*snapshot, printer);
        }
        if (owner && !error.isEmpty()) QMessageBox::critical(owner, QStringLiteral("Print viewer"), error);
    }

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
