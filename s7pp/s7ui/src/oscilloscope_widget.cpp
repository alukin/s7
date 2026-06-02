#include "oscilloscope_widget.hpp"
#include "render/print_render_service.hpp"

#include <s7probe.hpp>
#include <qcp.h>

#include <QDialog>
#include <QPrinter>
#include <QPrintDialog>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Color palette for multi-channel display
static constexpr std::array<QColor, 8> kChannelColors = {
	QColor(40, 200, 120),
	QColor(255, 100, 100),
	QColor(100, 150, 255),
	QColor(255, 200, 50),
	QColor(150, 100, 255),
	QColor(255, 100, 200),
	QColor(50, 220, 100),
	QColor(200, 200, 50),
};
static constexpr auto kChannelColorCount = kChannelColors.size();

OscilloscopeWidget::OscilloscopeWidget(QWidget* parent)
	: QWidget(parent)
{
	setupLayout();

	// Polling timer for live data refresh (~30 Hz)
	pollTimer_ = new QTimer(this);
	pollTimer_->setInterval(33);  // ~30 Hz
	connect(pollTimer_, &QTimer::timeout, this, &OscilloscopeWidget::onPollTimer);
}

OscilloscopeWidget::~OscilloscopeWidget()
{
	if (pollTimer_)
	{
		pollTimer_->stop();
	}
}

void OscilloscopeWidget::setupLayout()
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	plot_ = new QCustomPlot(this);
	layout->addWidget(plot_);

	// Configure plot appearance
	plot_->xAxis->setLabel("time [s]");
	plot_->yAxis->setLabel("amplitude");
	plot_->xAxis->setRange(0.0, timeWindow_);
	plot_->yAxis->setRange(-1.5, 1.5);
	plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

	// Enable anti-aliasing for nicer rendering
	plot_->setNotAntialiasedElements(QCP::aeAll);
}

void OscilloscopeWidget::attachProbe(const std::shared_ptr<s7::Probe>& probe, const QString& label)
{
	if (!probe) return;

	// Check if label already exists
	if (!label.isEmpty() && findProbeEntry(label))
	{
		// Already attached; skip
		return;
	}

	// Create new entry
	ProbeEntry entry;
	entry.label = label.isEmpty() ? QString("Probe %1").arg(probes_.size() + 1) : label;
	entry.probe = probe;
	entry.isComplex = false;
	probes_.push_back(entry);

	// Rebuild all graphs
	rebuildGraphs();

	// Start polling if not already running
	if (!pollTimer_->isActive())
	{
		pollTimer_->start();
	}

	redraw();
}

void OscilloscopeWidget::attachComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe, const QString& label)
{
	if (!probe) return;

	// Check if label already exists
	if (!label.isEmpty() && findProbeEntry(label))
	{
		// Already attached; skip
		return;
	}

	// Create new entry
	ProbeEntry entry;
	entry.label = label.isEmpty() ? QString("ComplexProbe %1").arg(probes_.size() + 1) : label;
	entry.complexProbe = probe;
	entry.isComplex = true;
	probes_.push_back(entry);

	// Rebuild all graphs
	rebuildGraphs();

	// Start polling if not already running
	if (!pollTimer_->isActive())
	{
		pollTimer_->start();
	}

	redraw();
}

void OscilloscopeWidget::detachProbe(const QString& label)
{
	auto it = std::find_if(probes_.begin(), probes_.end(),
	                        [&label](const ProbeEntry& e) { return e.label == label; });
	if (it != probes_.end())
	{
		probes_.erase(it);
		rebuildGraphs();

		if (probes_.empty())
		{
			pollTimer_->stop();
		}

		redraw();
	}
}

void OscilloscopeWidget::clearAllProbes()
{
	probes_.clear();
	plot_->clearGraphs();
	pollTimer_->stop();
}

void OscilloscopeWidget::setTimeWindow(double seconds)
{
	timeWindow_ = std::max(0.001, seconds);
	useTimeWindow_ = true;
	// Derive sample count from the current sample rate.
	sampleCount_ = std::max(static_cast<std::size_t>(10),
	    static_cast<std::size_t>(timeWindow_ * sampleRate_));
}

void OscilloscopeWidget::setSampleCount(std::size_t count)
{
	sampleCount_ = std::max(static_cast<std::size_t>(10), count);
	useTimeWindow_ = false;
}

void OscilloscopeWidget::setSampleRate(double hz)
{
	sampleRate_ = std::max(0.001, hz);
	// If already in time-window mode, re-derive sampleCount_ immediately.
	if (useTimeWindow_)
	{
		sampleCount_ = std::max(static_cast<std::size_t>(10),
		    static_cast<std::size_t>(timeWindow_ * sampleRate_));
	}
}

auto OscilloscopeWidget::exportPdf(const QString& fileName,
                                   const s7ui::printing::PageSpec& pageSpec) const -> bool
{
	if (!plot_) return false;
	return s7ui::printing::renderPlotPdf(*plot_, fileName, pageSpec, true,
	                                     QStringLiteral("s7ui"), QStringLiteral("Oscilloscope"));
}

auto OscilloscopeWidget::print(QWidget* parent,
                               const s7ui::printing::PageSpec& pageSpec) const -> bool
{
	if (!plot_) return false;

	QPrinter printer(QPrinter::HighResolution);
	s7ui::printing::configurePrinter(printer, pageSpec);
	QPrintDialog dialog(&printer, parent);
	if (dialog.exec() != QDialog::Accepted)
	{
		return false;
	}

	return s7ui::printing::renderPlotToPrinter(*plot_, printer, true);
}

void OscilloscopeWidget::redraw()
{
	updateAllProbes();
}

auto OscilloscopeWidget::findProbeEntry(const QString& label) -> ProbeEntry*
{
	auto it = std::find_if(probes_.begin(), probes_.end(),
	                        [&label](const ProbeEntry& e) { return e.label == label; });
	return (it != probes_.end()) ? &(*it) : nullptr;
}

void OscilloscopeWidget::rebuildGraphs()
{
	// Clear all existing graphs
	plot_->clearGraphs();

	// Rebuild based on current probes
	for (auto& entry : probes_)
	{
		if (entry.isComplex)
		{
			if (auto cprobe = entry.complexProbe.lock())
			{
				const int channelCount = cprobe->channelCount();
				for (int ch = 0; ch < channelCount; ++ch)
				{
					// Real part
					{
						QCPGraph* graph = plot_->addGraph();
						const QColor color = kChannelColors[(entry.graphs.size() / 2) % kChannelColorCount];
						graph->setPen(QPen(color, 1.5));
						graph->setName(QString("%1 Ch%2 Real").arg(entry.label).arg(ch + 1));
						entry.graphs.push_back(graph);
					}

					// Imaginary part (dashed)
					{
						QCPGraph* graph = plot_->addGraph();
						const QColor color = kChannelColors[(entry.graphs.size() / 2) % kChannelColorCount];
						QPen pen(color, 1.5);
						pen.setStyle(Qt::DashLine);
						graph->setPen(pen);
						graph->setName(QString("%1 Ch%2 Imag").arg(entry.label).arg(ch + 1));
						entry.graphs.push_back(graph);
					}
				}
			}
		}
		else
		{
			if (auto probe = entry.probe.lock())
			{
				const int channelCount = probe->channelCount();
				for (int ch = 0; ch < channelCount; ++ch)
				{
					QCPGraph* graph = plot_->addGraph();
					const QColor color = kChannelColors[entry.graphs.size() % kChannelColorCount];
					graph->setPen(QPen(color, 1.5));
					graph->setName(QString("%1 Ch%2").arg(entry.label).arg(ch + 1));
					entry.graphs.push_back(graph);
				}
			}
		}
	}

	// Only show legend if multiple probes or multiple channels
	int totalCharts = 0;
	for (const auto& entry : probes_)
	{
		totalCharts += entry.graphs.size();
	}
	plot_->legend->setVisible(totalCharts > 1);
}

void OscilloscopeWidget::updateAllProbes()
{
	if (probes_.empty())
	{
		return;
	}

	double minY = std::numeric_limits<double>::max();
	double maxY = std::numeric_limits<double>::lowest();

	const std::size_t maxSamples = sampleCount_;

	for (auto& entry : probes_)
	{
		if (entry.isComplex)
		{
			if (auto cprobe = entry.complexProbe.lock())
			{
				const int channelCount = cprobe->channelCount();
				int graphIdx = 0;

				for (int ch = 0; ch < channelCount; ++ch)
				{
					const auto samples = cprobe->readChannel(ch, maxSamples);
					if (samples.empty())
					{
						continue;
					}

					QVector<double> x;
					QVector<double> reData, imData;
					x.reserve(static_cast<qsizetype>(samples.size()));
					reData.reserve(static_cast<qsizetype>(samples.size()));
					imData.reserve(static_cast<qsizetype>(samples.size()));

					const double xScale = useTimeWindow_ ? (1.0 / sampleRate_) : 1.0;

					for (std::size_t i = 0; i < samples.size(); ++i)
					{
						x.push_back(static_cast<double>(i) * xScale);
						const double re = std::real(samples[i]);
						const double im = std::imag(samples[i]);
						reData.push_back(re);
						imData.push_back(im);
						minY = std::min(minY, re);
						minY = std::min(minY, im);
						maxY = std::max(maxY, re);
						maxY = std::max(maxY, im);
					}

					// Set Real part (solid line)
					if (graphIdx < static_cast<int>(entry.graphs.size()))
					{
						entry.graphs[graphIdx]->setData(x, reData, true);
						++graphIdx;
					}

					// Set Imaginary part (dashed line)
					if (graphIdx < static_cast<int>(entry.graphs.size()))
					{
						entry.graphs[graphIdx]->setData(x, imData, true);
						++graphIdx;
					}
				}
			}
		}
		else
		{
			if (auto probe = entry.probe.lock())
			{
				const int channelCount = std::min(static_cast<int>(entry.graphs.size()), probe->channelCount());

				for (int ch = 0; ch < channelCount; ++ch)
				{
					const auto samples = probe->readChannel(ch, maxSamples);
					if (samples.empty())
					{
						continue;
					}

					QVector<double> x, y;
					x.reserve(static_cast<qsizetype>(samples.size()));
					y.reserve(static_cast<qsizetype>(samples.size()));

					const double xScale = useTimeWindow_ ? (1.0 / sampleRate_) : 1.0;

					for (std::size_t i = 0; i < samples.size(); ++i)
					{
						x.push_back(static_cast<double>(i) * xScale);
						y.push_back(static_cast<double>(samples[i]));
						minY = std::min(minY, static_cast<double>(samples[i]));
						maxY = std::max(maxY, static_cast<double>(samples[i]));
					}

					entry.graphs[ch]->setData(x, y, true);
				}
			}
		}
	}

	// Auto-scale Y if needed
	if (minY != std::numeric_limits<double>::max())
	{
		const double margin = (maxY - minY) * 0.1;
		plot_->yAxis->setRange(minY - margin, maxY + margin);
	}

	if (useTimeWindow_)
	{
		plot_->xAxis->setLabel("time [s]");
		plot_->xAxis->setRange(0.0, timeWindow_);
	}
	else
	{
		plot_->xAxis->setLabel("samples");
		plot_->xAxis->setRange(0.0, static_cast<double>(sampleCount_));
	}

	plot_->replot(QCustomPlot::rpQueuedReplot);
}

void OscilloscopeWidget::onPollTimer()
{
	bool hasActiveProbe = false;
	for (const auto& entry : probes_)
	{
		if (!entry.isComplex && entry.probe.lock())
		{
			hasActiveProbe = true;
			break;
		}
		if (entry.isComplex && entry.complexProbe.lock())
		{
			hasActiveProbe = true;
			break;
		}
	}

	if (hasActiveProbe)
	{
		updateAllProbes();
	}
	else
	{
		pollTimer_->stop();
	}
}



