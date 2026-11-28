// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/waterfall_view.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace s7studio::viewers {

class WaterfallView::Plot final : public QWidget {
public:
    explicit Plot(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("waterfall.plot"));
        setAccessibleName(QStringLiteral("Waterfall time-frequency plot"));
        setMinimumSize(420, 210);
    }

    void setSnapshot(WaterfallSnapshot snapshot)
    {
        snapshot_ = std::move(snapshot);
        update();
    }

    const WaterfallSnapshot& snapshot() const { return snapshot_; }

    static void draw(QPainter& painter, const QRect& bounds, const WaterfallSnapshot& snapshot)
    {
        const auto& axes = *snapshot.axes;
        const QRect raster = bounds.adjusted(90, 20, -97, -54);
        painter.drawImage(raster, snapshot.image);
        painter.drawRect(raster.adjusted(0, 0, 1, 1));
        const auto number = [](double value) { return QString::number(value, 'g', 5); };
        const auto label = [&](int bin) {
            if (const auto hz = axes.frequencyHz(bin)) return number(*hz);
            return QString::number(axes.signedBin(bin));
        };
        const int dc = axes.bins / 2;
        const std::array ticks{0, dc / 2, dc, dc + (axes.bins - 1 - dc) / 2, axes.bins - 1};
        int previousX = -100;
        for (std::size_t index = 0; index < ticks.size(); ++index) {
            if (raster.width() < 420 && index % 2 != 0) continue;
            const int bin = ticks[index];
            const int x = raster.x() + static_cast<int>((bin + 0.5) * raster.width() / axes.bins);
            if (x - previousX < 78 && index != 2) continue;
            previousX = x;
            painter.drawLine(x, raster.bottom() + 1, x, raster.bottom() + 5);
            painter.drawText(QRect(x - 39, raster.bottom() + 7, 78, 20), Qt::AlignCenter, label(bin));
        }
        painter.drawText(QRect(raster.x(), raster.bottom() + 30, raster.width(), 20), Qt::AlignCenter,
                         axes.physicalRate ? QStringLiteral("Frequency (Hz)") : QStringLiteral("Signed DFT bin"));

        const auto& lines = snapshot.lines;
        const auto first = std::ranges::find_if(lines, [](const auto& line) { return line.has_value(); });
        const auto firstIndex = static_cast<int>(first - lines.begin());
        const auto lastIndex = static_cast<int>(lines.size()) - 1;
        const auto mid = std::ranges::find_if(lines.begin() + (firstIndex + lastIndex) / 2, lines.end(),
                                              [](const auto& line) { return line.has_value(); });
        int previousY = -100;
        for (const int index : {firstIndex, static_cast<int>(mid - lines.begin()), lastIndex}) {
            const auto y = raster.y() + static_cast<int>((index + 0.5) * raster.height() / static_cast<double>(lines.size()));
            if (y - previousY < 22) continue;
            previousY = y;
            painter.drawLine(raster.x() - 4, y, raster.x(), y);
            painter.drawText(QRect(23, y - 10, raster.x() - 30, 20), Qt::AlignVCenter | Qt::AlignRight,
                             number(axes.modelSeconds(lines[static_cast<std::size_t>(index)]->domainStep)));
        }
        painter.save();
        painter.translate(14, raster.center().y());
        painter.rotate(-90);
        painter.drawText(QRect(-raster.height() / 2, -10, raster.height(), 20), Qt::AlignCenter, QStringLiteral("Model time (s)"));
        painter.restore();

        const auto& scale = snapshot.scale;
        const QRect legend(raster.right() + 14, raster.top(), 12, raster.height());
        QLinearGradient gradient(legend.bottomLeft(), legend.topLeft());
        for (const double position : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0})
            gradient.setColorAt(position, QColor::fromRgb(waterfallColor(position, scale.palette)));
        painter.fillRect(legend, gradient);
        painter.drawText(QRect(legend.right() + 5, legend.top() - 8, 58, 20), Qt::AlignVCenter, number(scale.maximumDb));
        painter.drawText(QRect(legend.right() + 5, legend.bottom() - 10, 58, 20), Qt::AlignVCenter, number(scale.minimumDb));
        painter.drawText(QRect(legend.right() + 5, legend.center().y() - 10, 50, 20), Qt::AlignVCenter, QStringLiteral("dB"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().text().color());
        if (snapshot_.image.isNull()) {
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for spectrum rows"));
            return;
        }
        draw(painter, rect(), snapshot_);
    }

private:
    WaterfallSnapshot snapshot_;
};

WaterfallView::WaterfallView(QWidget* parent)
    : QWidget(parent), plot_(new Plot(this)), minimum_(new QDoubleSpinBox(this)),
      maximum_(new QDoubleSpinBox(this)), reference_(new QDoubleSpinBox(this)),
      palette_(new QComboBox(this)), scaleStatus_(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* form = new QGridLayout;
    const auto field = [&](int index, const QString& name, const QString& label, QWidget* widget) {
        widget->setObjectName(QStringLiteral("waterfall.") + name);
        widget->setAccessibleName(label);
        auto* caption = new QLabel(label, this);
        caption->setBuddy(widget);
        form->addWidget(caption, 0, index);
        form->addWidget(widget, 1, index);
    };
    for (auto* limit : {minimum_, maximum_}) {
        limit->setRange(-600, 600);
        limit->setDecimals(1);
        limit->setKeyboardTracking(false);
    }
    minimum_->setValue(scale_.minimumDb);
    maximum_->setValue(scale_.maximumDb);
    reference_->setDecimals(12);
    reference_->setRange(1e-12, 1e12);
    reference_->setValue(scale_.reference);
    reference_->setKeyboardTracking(false);
    palette_->addItems({QStringLiteral("Heat"), QStringLiteral("Grayscale")});
    field(0, "minimumDb", "Minimum (dB)", minimum_);
    field(1, "maximumDb", "Maximum (dB)", maximum_);
    field(2, "reference", "Power reference", reference_);
    field(3, "palette", "Palette", palette_);
    layout->addLayout(form);
    layout->addWidget(scaleStatus_);
    layout->addWidget(plot_, 1);
    for (auto* control : {minimum_, maximum_, reference_})
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] { updateScale(); });
    connect(palette_, &QComboBox::currentIndexChanged, this, [this] { updateScale(); });
}

QString WaterfallView::present(const s7::CommittedArraySample& sample)
{
    const auto outcome = history_.append(sample);
    renderHistory();
    return outcome == WaterfallHistory::Append::AxesChanged ? QStringLiteral(" · Frequency or time axis changed") : QString{};
}

void WaterfallView::clear()
{
    history_.clear();
    plot_->setSnapshot({});
}

std::optional<ViewerPrintSnapshot> WaterfallView::printSnapshot() const
{
    const auto& snapshot = plot_->snapshot();
    if (snapshot.image.isNull() || !snapshot.axes) return std::nullopt;
    const auto& axes = *snapshot.axes;
    const auto first = std::ranges::find_if(snapshot.lines, [](const auto& row) { return row.has_value(); });
    const auto& scale = snapshot.scale;
    ViewerPrintSnapshot page;
    page.appendixTitle = QStringLiteral("Waterfall row details");
    page.notes = {
        QStringLiteral("Domain steps %1–%2 · %3 bins · Domain rate %4")
            .arg((*first)->domainStep).arg(snapshot.lines.back()->domainStep).arg(axes.bins)
            .arg(QString::fromStdString(axes.domainRate.to_string())),
        axes.physicalRate ? QStringLiteral("Physical sample rate %1")
            .arg(QString::fromStdString(axes.physicalRate->to_string()))
            : QStringLiteral("Physical sample rate unknown"),
        QStringLiteral("%1 to %2 dB re %3 %4 · %5 palette")
            .arg(scale.minimumDb).arg(scale.maximumDb).arg(scale.reference)
            .arg(axes.units == s7::SpectrumUnits::PowerPerHz ? QStringLiteral("power/Hz") : QStringLiteral("power/bin"))
            .arg(scale.palette == WaterfallPalette::Heat ? QStringLiteral("Heat") : QStringLiteral("Grayscale")),
        QStringLiteral("Blue stripes mark gaps. Row positions and source identity appear on the following pages.")
    };
    for (const auto& row : snapshot.lines) {
        if (!row) continue;
        auto line = QStringLiteral("Step %1").arg(row->domainStep);
        if (row->origin) {
            line += QStringLiteral(" · Source %1 / epoch %2 / sequence %3")
                .arg(row->origin->source_id).arg(row->origin->epoch).arg(row->origin->sequence);
        }
        page.appendix.append(line);
        if (row->skippedDomainSteps || row->sourceDiscontinuity) {
            page.appendix.append(QStringLiteral("     Gap before row · Skipped steps %1 · Source discontinuity %2")
                .arg(row->skippedDomainSteps).arg(row->sourceDiscontinuity ? QStringLiteral("yes") : QStringLiteral("no")));
        }
        if (row->sampleWindow) {
            const auto number = [](std::optional<std::uint64_t> value) {
                return value ? QString::number(*value) : QStringLiteral("unknown");
            };
            page.appendix.append(QStringLiteral("     First sample %1 · Valid samples %2 · Missing samples %3")
                .arg(number(row->sampleWindow->first_sample), number(row->sampleWindow->valid_sample_count),
                     row->sampleWindow->missing_samples ? QStringLiteral("yes") : QStringLiteral("no")));
        }
    }
    page.draw = [snapshot](QPainter& painter, const QRectF& bounds) {
        Plot::draw(painter, bounds.toAlignedRect(), snapshot);
    };
    return page;
}

void WaterfallView::updateScale()
{
    if (minimum_->value() >= maximum_->value()) {
        scaleStatus_->setText(QStringLiteral("Minimum dB must be below maximum dB."));
        return;
    }
    scale_ = {minimum_->value(), maximum_->value(), reference_->value(),
              palette_->currentIndex() == 0 ? WaterfallPalette::Heat : WaterfallPalette::Gray};
    try {
        renderHistory();
    } catch (const std::exception& error) {
        scaleStatus_->setText(QString::fromUtf8(error.what()));
    }
}

void WaterfallView::renderHistory()
{
    auto snapshot = history_.snapshot(scale_, 1024);
    if (minimum_->value() < maximum_->value()) {
        const auto units = snapshot.axes && snapshot.axes->units == s7::SpectrumUnits::PowerPerHz
            ? QStringLiteral("power/Hz") : QStringLiteral("power/bin");
        scaleStatus_->setText(QStringLiteral("dB re %1 %2 · Blue stripes mark gaps")
            .arg(scale_.reference, 0, 'g', 5).arg(units));
    }
    plot_->setSnapshot(std::move(snapshot));
}

} // namespace s7studio::viewers
