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

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().text().color());
        if (!snapshot_.axes) {
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for spectrum rows"));
            return;
        }
        const auto& axes = *snapshot_.axes;
        const QRect raster = rect().adjusted(90, 20, -97, -54);
        painter.drawImage(raster, snapshot_.image);
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

        const auto& lines = snapshot_.lines;
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

        const auto& scale = snapshot_.scale;
        const QRect legend(raster.right() + 14, raster.top(), 12, raster.height());
        QLinearGradient gradient(legend.bottomLeft(), legend.topLeft());
        for (const double position : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0})
            gradient.setColorAt(position, QColor::fromRgb(waterfallColor(position, scale.palette)));
        painter.fillRect(legend, gradient);
        painter.drawText(QRect(legend.right() + 5, legend.top() - 8, 58, 20), Qt::AlignVCenter, number(scale.maximumDb));
        painter.drawText(QRect(legend.right() + 5, legend.bottom() - 10, 58, 20), Qt::AlignVCenter, number(scale.minimumDb));
        painter.drawText(QRect(legend.right() + 5, legend.center().y() - 10, 50, 20), Qt::AlignVCenter, QStringLiteral("dB"));
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
