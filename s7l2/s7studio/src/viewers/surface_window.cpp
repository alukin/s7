// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "surface_window.hpp"
#include "analysis/result_file.hpp"
#include "observation/run_metadata.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace s7studio::viewers {
QStringList SurfaceWindow::roles(bool complex) {
    QStringList result{"x", "y", "index", "columns", "rows", "active"};
    result << (complex ? "re" : "z");
    if (complex)
        result << "im";
    return result;
}
ViewerType SurfaceWindow::viewerType(bool complex) {
    return {complex ? "ComplexSurface3D" : "Surface3D",
            {},
            complex ? "8→surface" : "7→surface",
            {},
            roles(complex),
            [complex](const ViewerInstanceSpec &spec) -> ObservationViewer * {
                if (spec.probeNames.size() != roles(complex).size())
                    return nullptr;
                std::set<QString> names;
                for (const auto &name : spec.probeNames)
                    if (name.isEmpty() || !names.insert(name).second)
                        return nullptr;
                return new SurfaceWindow(complex, spec.instanceId);
            }};
}
SurfaceWindow::SurfaceWindow(bool complex, const QString &name, QWidget *parent)
    : QWidget(parent, Qt::Window), complex_(complex), roles_(roles(complex)),
      plot_(new SurfacePlotView(complex, this)), status_(new QLabel(this)) {
    units_.resize(roles_.size());
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1000, 760);
    setMinimumSize(700, 520);
    setWindowTitle((complex ? "Complex surface — " : "Real surface — ") + name);
    auto *root = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    auto *mode = new QComboBox(this);
    mode->setObjectName("surface.presentation");
    mode->addItems({"Surface", "Wireframe", "Heatmap"});
    controls->addWidget(mode);
    connect(mode, &QComboBox::currentIndexChanged, plot_, &SurfacePlotView::setPresentation);
    if (complex) {
        auto *height = new QComboBox(this);
        height->setObjectName("surface.height");
        height->addItems({"|w|", "Re(w)", "Im(w)", "log(1 + |w|)"});
        controls->addWidget(height);
        connect(height, &QComboBox::currentIndexChanged, this,
                [this](int index) { plot_->setHeightMode(static_cast<SurfaceHeight>(index)); });
    }
    if (!complex) {
        auto *height = new QComboBox(this);
        height->addItems({"Value", "log10(value), positive cells"});
        controls->addWidget(height);
        connect(height, &QComboBox::currentIndexChanged, this, [this](int i) {
            plot_->setHeightMode(i == 0 ? SurfaceHeight::Real : SurfaceHeight::LogMagnitude);
        });
    }
    for (const auto &[text, rotation] : std::array<std::pair<const char *, QVector3D>, 4>{
             {{"3D", {-30, 35, 0}}, {"XY", {-90, 0, 0}}, {"XZ", {0, 0, 0}}, {"YZ", {0, 90, 0}}}}) {
        auto *b = new QPushButton(text, this);
        controls->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, rotation] { plot_->setViewRotation(rotation); });
    }
    auto *clear = new QPushButton("Clear display", this);
    controls->addWidget(clear);
    connect(clear, &QPushButton::clicked, this, &SurfaceWindow::clearSamples);
    auto *exportButton = new QPushButton("Export raw CSV", this);
    controls->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, &SurfaceWindow::exportCsv);
    root->addLayout(controls);
    auto *displayControls = new QHBoxLayout;
    auto *contours = new QCheckBox("Contours (heatmap)", this);
    displayControls->addWidget(contours);
    connect(contours, &QCheckBox::toggled, plot_, &SurfacePlotView::setContours);
    auto *equal = new QCheckBox("Equal numeric aspect", this);
    displayControls->addWidget(equal);
    connect(equal, &QCheckBox::toggled, plot_, &SurfacePlotView::setEqualAspect);
    auto *fixed = new QPushButton("Fixed ranges…", this);
    displayControls->addWidget(fixed);
    connect(fixed, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Fixed axis and height/color ranges");
        QFormLayout layout(&dialog);
        std::array<QDoubleSpinBox *, 6> values{};
        const auto b = plot_->bridge()->geometry.bounds();
        for (int i = 0; i < 6; ++i) {
            values[std::size_t(i)] = new QDoubleSpinBox(&dialog);
            values[std::size_t(i)]->setDecimals(9);
            values[std::size_t(i)]->setRange(-1e100, 1e100);
            values[std::size_t(i)]->setValue(i % 2 ? b.max[std::size_t(i / 2)] : b.min[std::size_t(i / 2)]);
            layout.addRow(
                QString("%1 %2").arg(QStringList{"X", "Y", "Height"}[i / 2], i % 2 ? "maximum" : "minimum"),
                values[std::size_t(i)]);
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            TrajectoryBounds range;
            for (std::size_t a = 0; a < 3; ++a) {
                range.min[a] = values[2 * a]->value();
                range.max[a] = values[2 * a + 1]->value();
            }
            try {
                plot_->setFixedBounds(range);
            } catch (const std::exception &e) {
                QMessageBox::warning(this, "Invalid ranges", e.what());
            }
        }
    });
    auto *automatic = new QPushButton("Auto ranges", this);
    displayControls->addWidget(automatic);
    connect(automatic, &QPushButton::clicked, this, [this] { plot_->setFixedBounds({}); });
    for (bool save : {true, false}) {
        auto *button = new QPushButton(save ? "Save result…" : "Open result…", this);
        displayControls->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, save] {
            const auto path =
                save ? QFileDialog::getSaveFileName(this, "Save surface", "surface.s7analysis",
                                                    "S7 analysis (*.s7analysis)")
                     : QFileDialog::getOpenFileName(this, "Open surface", {}, "S7 analysis (*.s7analysis)");
            if (path.isEmpty())
                return;
            try {
                if (save)
                    saveResult(path);
                else
                    openResult(path);
            } catch (const std::exception &e) {
                QMessageBox::critical(this, "Result file", e.what());
            }
        });
    }
    root->addLayout(displayControls);
    auto *inspect = new QHBoxLayout;
    auto *row = new QSpinBox(this);
    auto *column = new QSpinBox(this);
    row->setRange(0, 512);
    column->setRange(0, 512);
    row->setObjectName("surface.row");
    column->setObjectName("surface.column");
    inspect->addWidget(new QLabel("Inspect row", this));
    inspect->addWidget(row);
    inspect->addWidget(new QLabel("column", this));
    inspect->addWidget(column);
    auto *value = new QLabel("Hover a heatmap cell or choose row/column to inspect source values.", this);
    value->setWordWrap(true);
    value->setTextFormat(Qt::PlainText);
    inspect->addWidget(value, 1);
    root->addLayout(inspect);
    connect(row, &QSpinBox::valueChanged, this,
            [this, row, column] { plot_->selectCell(row->value(), column->value()); });
    connect(column, &QSpinBox::valueChanged, this,
            [this, row, column] { plot_->selectCell(row->value(), column->value()); });
    connect(plot_, &SurfacePlotView::inspected, value, &QLabel::setText);
    status_->setObjectName("viewer.status");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    root->addWidget(status_);
    root->addWidget(plot_, 1);
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &SurfaceWindow::refreshPlot);
    timer->start();
    updateStatus();
}
void SurfaceWindow::appendObservedSample(const ObservedSample &sample) {
    if (imported_ || !error_.isEmpty())
        return;
    const auto axis = roles_.indexOf(sample.role);
    if (axis < 0)
        return;
    units_[axis] = sample.unit;
    if (!complex_)
        (void)aligner_.add(sample.step, 7, 0);
    if (const auto cell = aligner_.add(sample.step, std::size_t(axis), sample.value)) {
        try {
            if (grid_.append(*cell))
                dirty_ = true;
        } catch (const std::exception &e) {
            error_ = QString::fromUtf8(e.what());
            updateStatus();
        }
    }
}
void SurfaceWindow::clearSamples() {
    importedMetadata_ = {};
    imported_ = false;
    discardPresentationHistory();
    grid_.clear();
    aligner_.clear();
    error_.clear();
    dropped_ = 0;
    units_.fill({});
    dirty_ = true;
    plot_->setGrid(std::make_shared<const analysis::SurfaceGrid>(grid_));
    plot_->bridge()->setHead(false, {});
    updateStatus();
}
void SurfaceWindow::setDroppedSampleCount(std::uint64_t count) {
    dropped_ = count;
    updateStatus();
}
void SurfaceWindow::updateStatus() {
    status_->setText(
        QString(imported_ ? "Imported result | %1/%2 cells | %3 | unpaired steps %4 | capture loss %5%6%7"
                          : "%1/%2 cells | %3 | unpaired steps %4 | capture loss %5%6%7")
            .arg(grid_.received)
            .arg(grid_.values.size())
            .arg(grid_.complete() ? "scan complete" : "partial grid; missing cells are holes")
            .arg(aligner_.discardedSteps())
            .arg(dropped_)
            .arg(error_.isEmpty() ? "" : " | INVALID GRID: " + error_)
            .arg(sourceStatusText().isEmpty() ? "" : " | " + sourceStatusText()));
}
void SurfaceWindow::refreshPlot() {
    if (!dirty_ || !isVisible())
        return;
    dirty_ = false;
    auto axis = [this](int i) {
        return roles_[i] + (units_[i].isEmpty() ? " (unit unspecified)" : " (" + units_[i] + ")");
    };
    if (imported_) {
        auto label = [&](const char *name, const char *unit, int role) {
            const auto value = importedMetadata_.value(unit).toString(units_[role]);
            return importedMetadata_.value(name).toString(roles_[role]) + " (" +
                   (value.isEmpty() ? QString("unit unspecified") : value) + ")";
        };
        plot_->setAxes({label("x_label", "x_unit", 0), label("y_label", "y_unit", 1),
                        label("height_label", "height_unit", 6)});
    } else
        plot_->setAxes({axis(0), axis(1), axis(6)});
    plot_->setGrid(std::make_shared<const analysis::SurfaceGrid>(grid_));
    updateStatus();
}
bool SurfaceWindow::refreshSource() { return imported_ ? false : ObservationViewer::refreshSource(); }
void SurfaceWindow::saveResult(const QString &path) {
    analysis::SurfaceGrid captured;
    auto units = units_;
    if (source() && !imported_) {
        observation::StepAligner<8> aligner;
        std::array<observation::ChannelId, 8> channels{};
        const auto descriptors = source()->channels();
        for (qsizetype role = 0; role < roles_.size(); ++role) {
            const auto ids = selectedChannels({roles_[role]});
            if (ids.size() != 1)
                throw std::invalid_argument("All surface inputs must be bound before saving");
            channels[std::size_t(role)] = ids.front();
            units[role] = observation::probeUnit(source()->provenance(), descriptors[ids.front()].name);
        }
        std::uint64_t cursor = 0;
        const auto end = source()->status().samples;
        while (cursor < end) {
            const auto page = source()->read(cursor, qsizetype(std::min<std::uint64_t>(4096, end - cursor)));
            if (!page.error.isEmpty())
                throw std::runtime_error(page.error.toStdString());
            for (const auto &sample : page.samples)
                for (std::size_t role = 0; role < std::size_t(roles_.size()); ++role)
                    if (sample.channel == channels[role]) {
                        if (!complex_)
                            (void)aligner.add(sample.step, 7, 0);
                        if (auto cell = aligner.add(sample.step, role, sample.value))
                            captured.append(*cell);
                    }
            if (page.next == cursor || page.next >= end)
                break;
            cursor = page.next;
        }
    }
    const auto &grid = source() && !imported_ ? captured : grid_;
    analysis::ResultFile file;
    file.kind = complex_ ? "complex_surface" : "surface";
    file.x = grid.x;
    file.y = grid.y;
    file.values = grid.values;
    file.valid = grid.present;
    for (std::size_t i = 0; i < file.valid.size(); ++i)
        file.valid[i] = grid.finite(i);
    file.metadata = {{"axes", QJsonArray::fromStringList(units)},
                     {"provenance", source() ? source()->provenance() : QJsonObject{}},
                     {"capture_complete", grid.complete()},
                     {"run_id", source() ? QString::number(source()->runId()) : QString{}}};
    if (source() && !imported_) {
        const auto state = source()->status();
        file.metadata["grid_complete"] = grid.complete();
        file.metadata["capture_complete"] = state.finished && !state.stopped && !state.incomplete();
        file.metadata["capture_stopped"] = state.stopped;
        file.metadata["capture_loss"] = QString::number(state.captureDropped);
        file.metadata["capture_error"] = state.storageError + state.runError;
    }
    if (imported_)
        file.metadata = importedMetadata_;
    file.save(path);
}
void SurfaceWindow::openResult(const QString &path) {
    auto file = analysis::ResultFile::open(path);
    if (file.kind != (complex_ ? "complex_surface" : "surface") || file.x.size() < 2 || file.y.size() < 2 ||
        file.x.size() > 513 || file.y.size() > 513)
        throw std::invalid_argument("Surface kind/dimensions do not match this viewer");
    analysis::SurfaceGrid staged;
    staged.x = std::move(file.x);
    staged.y = std::move(file.y);
    staged.values = std::move(file.values);
    staged.present = std::move(file.valid);
    staged.received = std::count(staged.present.begin(), staged.present.end(), true);
    units_.fill({});
    importedMetadata_ = file.metadata;
    const auto units = file.metadata["axes"].toArray();
    for (qsizetype i = 0; i < units_.size() && i < units.size(); ++i)
        units_[i] = units[i].toString();
    grid_ = std::move(staged);
    imported_ = true;
    error_.clear();
    dirty_ = true;
    refreshPlot();
    status_->setText("Imported result — " + path);
}
void SurfaceWindow::exportCsv() {
    if (!source())
        return;
    const auto path =
        QFileDialog::getSaveFileName(this, "Export surface source", "surface.csv", "CSV files (*.csv)");
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    QString error;
    if (!file.open(QIODevice::WriteOnly) ||
        !source()->writeAlignedCsv(file, selectedChannels(roles_), &error) || !file.commit())
        QMessageBox::critical(this, "Export failed", error.isEmpty() ? file.errorString() : error);
}
} // namespace s7studio::viewers
