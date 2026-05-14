#pragma once

#include <QWidget>
#include <memory>
#include <vector>

#include <s7probe.hpp>

#include "render/print_render_service.hpp"

class QCustomPlot;
class QCPGraph;
class QTimer;

/**
 * @class OscilloscopeWidget
 * @brief Multi-channel oscilloscope display widget
 *
 * Displays real-time signal data from a connected Probe or ComplexProbe.
 * Each channel is rendered as a separate trace with distinct color.
 *
 * Features:
 * - N-channel display from Probe inputs
 * - Real/Complex signal support
 * - Circular buffer scrolling (time-window view)
 * - Pan/zoom interactions
 * - Color-coded channel traces
 *
 * Usage:
 *   auto osc = new OscilloscopeWidget(parent);
 *   osc->attachProbe(my_probe);  // Probes can have 1..N channels
 *   showUI will poll probe data at ~30 Hz
 */
class OscilloscopeWidget : public QWidget
{
	Q_OBJECT

public:
	explicit OscilloscopeWidget(QWidget* parent = nullptr);
	~OscilloscopeWidget() override;

	/// Attach a real-valued probe with N channels
	void attachProbe(const std::shared_ptr<s7::Probe>& probe);

	/// Attach a complex-valued probe
	void attachComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe);

	/// Clear any attached probe and reset display
	void clearProbe();

	/// Set the time window in seconds to display
	void setTimeWindow(double seconds);

	/// Set the number of samples to display (alternative to time window)
	void setSampleCount(std::size_t count);

	/// Set the sample rate in Hz (= 1/dt) so setTimeWindow() converts seconds
	/// to the correct sample count.  Default: 1000 Hz (dt = 0.001 s).
	void setSampleRate(double hz);

										/// Export the current plot to a PDF file using the shared print/export pipeline.
										[[nodiscard]] auto exportPdf(const QString& fileName,
										                             const s7ui::printing::PageSpec& pageSpec = {}) const -> bool;

			 /// Print the current plot using Qt's printer dialog.
										[[nodiscard]] auto print(QWidget* parent,
										                        const s7ui::printing::PageSpec& pageSpec = {}) const -> bool;

public slots:
	/// Force a redraw immediately (normally done automatically via timer)
	void redraw();

private slots:
	/// Poll probe data and update displays (~30 Hz)
	void pollAndUpdate();

private:
	void setupLayout();
	void updateChannelGraphs();

	// Real Probe data display
	void updateRealData();

	// Complex Probe data display
	void updateComplexData();

	QCustomPlot* plot_ = nullptr;
	std::vector<QCPGraph*> graphs_;
	QTimer* pollTimer_ = nullptr;

	std::weak_ptr<s7::Probe> probe_;
	std::weak_ptr<s7::ComplexProbe> complexProbe_;

	double timeWindow_ = 5.0;  // seconds
	std::size_t sampleCount_ = 512;
	double sampleRate_ = 1000.0;  // Hz (= 1/dt); used when useTimeWindow_ is true
	bool useTimeWindow_ = true;
};


