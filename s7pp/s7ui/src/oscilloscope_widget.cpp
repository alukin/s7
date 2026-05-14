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
	connect(pollTimer_, &QTimer::timeout, this, &OscilloscopeWidget::pollAndUpdate);
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

void OscilloscopeWidget::attachProbe(const std::shared_ptr<s7::Probe>& probe)
{
	complexProbe_.reset();
	probe_ = probe;

	if (!probe)
	{
		graphs_.clear();
		plot_->clearGraphs();
		pollTimer_->stop();
		return;
	}

	// Clear existing graphs
	plot_->clearGraphs();
	graphs_.clear();

	// Create one graph per probe channel
	const int channelCount = probe->channelCount();
	for (int ch = 0; ch < channelCount; ++ch)
	{
		QCPGraph* graph = plot_->addGraph();
		const QColor color = kChannelColors[ch % kChannelColorCount];
		graph->setPen(QPen(color, 1.5));
		graph->setName(QString("Ch %1").arg(ch + 1));
		graphs_.push_back(graph);
	}

	plot_->legend->setVisible(channelCount > 1);

	// Start polling
	pollTimer_->start();
	redraw();
}

void OscilloscopeWidget::attachComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe)
{
	probe_.reset();
	complexProbe_ = probe;

	if (!probe)
	{
		graphs_.clear();
		plot_->clearGraphs();
		pollTimer_->stop();
		return;
	}

	// For complex probe, we display Re/Im (or Magnitude/Phase) for each channel
	// For now: Re/Im pairs
	plot_->clearGraphs();
	graphs_.clear();

	const int channelCount = probe->channelCount();
	for (int ch = 0; ch < channelCount; ++ch)
	{
		// Real part
		{
			QCPGraph* graph = plot_->addGraph();
			const QColor color = kChannelColors[ch % kChannelColorCount];
			graph->setPen(QPen(color, 1.5));
			graph->setName(QString("Ch %1 Real").arg(ch + 1));
			graphs_.push_back(graph);
		}

		// Imaginary part (dashed)
		{
			QCPGraph* graph = plot_->addGraph();
			const QColor color = kChannelColors[ch % kChannelColorCount];
			QPen pen(color, 1.5);
			pen.setStyle(Qt::DashLine);
			graph->setPen(pen);
			graph->setName(QString("Ch %1 Imag").arg(ch + 1));
			graphs_.push_back(graph);
		}
	}

	plot_->legend->setVisible(true);

	// Start polling
	pollTimer_->start();
	redraw();
}

void OscilloscopeWidget::clearProbe()
{
	probe_.reset();
	complexProbe_.reset();
	graphs_.clear();
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

void OscilloscopeWidget::pollAndUpdate()
{
	if (auto probe = probe_.lock())
	{
		updateRealData();
	}
	else if (auto cprobe = complexProbe_.lock())
	{
		updateComplexData();
	}
	else
	{
		pollTimer_->stop();
	}
}

void OscilloscopeWidget::updateRealData()
{
	auto probe = probe_.lock();
	if (!probe || graphs_.empty())
	{
		return;
	}

	const int channelCount = std::min(static_cast<int>(graphs_.size()), probe->channelCount());

	// Determine sample count to read
	const std::size_t maxSamples = sampleCount_;

	double minY = std::numeric_limits<double>::max();
	double maxY = std::numeric_limits<double>::lowest();

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

		const double xScale = useTimeWindow_
		    ? (1.0 / sampleRate_)   // convert sample index → time in seconds
		    : 1.0;                   // display as raw sample index

		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			x.push_back(static_cast<double>(i) * xScale);
			y.push_back(static_cast<double>(samples[i]));
			minY = std::min(minY, static_cast<double>(samples[i]));
			maxY = std::max(maxY, static_cast<double>(samples[i]));
		}

		graphs_[ch]->setData(x, y, true);
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

void OscilloscopeWidget::updateComplexData()
{
	auto cprobe = complexProbe_.lock();
	if (!cprobe || graphs_.empty())
	{
		return;
	}

	const int channelCount = cprobe->channelCount();
	const std::size_t maxSamples = sampleCount_;

	double minY = std::numeric_limits<double>::max();
	double maxY = std::numeric_limits<double>::lowest();

	int graphIdx = 0;
	for (int ch = 0; ch < channelCount && graphIdx + 1 < static_cast<int>(graphs_.size()); ++ch)
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
		graphs_[graphIdx]->setData(x, reData, true);
		++graphIdx;

		// Set Imaginary part (dashed line)
		graphs_[graphIdx]->setData(x, imData, true);
		++graphIdx;
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

void OscilloscopeWidget::redraw()
{
	if (auto probe = probe_.lock())
	{
		updateRealData();
	}
	else if (auto cprobe = complexProbe_.lock())
	{
		updateComplexData();
	}
}



