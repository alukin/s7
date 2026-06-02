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
 * @brief Multi-probe oscilloscope display widget
 *
 * Displays real-time signal data from multiple Probe or ComplexProbe instances.
 * Each channel from each probe is rendered as a separate synchronized trace.
 * Probes are grouped by color and labeled in the legend.
 *
 * Features:
 * - Multi-probe display (N probes × M channels each)
 * - Real/Complex signal support
 * - Circular buffer scrolling (time-window view)
 * - Pan/zoom interactions
 * - Per-probe color grouping
 * - Live ~30 Hz polling
 *
 * Usage:
 *   auto osc = new OscilloscopeWidget(parent);
 *   osc->attachProbe(probe1, "Probe 1");    // Add first probe
 *   osc->attachProbe(probe2, "Probe 2");    // Add second probe (additive)
 *   // All probes display on same screen with shared time axis
 */
class OscilloscopeWidget : public QWidget
{
	Q_OBJECT

public:
	explicit OscilloscopeWidget(QWidget* parent = nullptr);
	~OscilloscopeWidget() override;

	/// Attach a real-valued probe (additive; does not clear previous probes)
	/// @param probe The probe to attach
	/// @param label Human-readable label for this probe (e.g., "Input Signal", "Output Signal")
	void attachProbe(const std::shared_ptr<s7::Probe>& probe, const QString& label = {});

	/// Attach a complex-valued probe (additive)
	void attachComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe, const QString& label = {});

	/// Remove a specific probe by its label
	void detachProbe(const QString& label);

	/// Clear all attached probes and reset display
	void clearAllProbes();

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
	/// Poll all probe data and update displays (~30 Hz)
	void onPollTimer();

private:
	// Internal structure to track a probe and its graphs
	struct ProbeEntry
	{
		QString label;
		std::weak_ptr<s7::Probe> probe;
		std::weak_ptr<s7::ComplexProbe> complexProbe;
		std::vector<QCPGraph*> graphs;  // graphs for this probe's channels
		bool isComplex = false;
	};

	void setupLayout();
	void updateChannelGraphs();
	void pollAndUpdate();

	// Data display for all attached probes
	void updateAllProbes();

	// Helper to find a probe entry by label
	auto findProbeEntry(const QString& label) -> ProbeEntry*;

	// Helper to rebuild graphs after probe addition/removal
	void rebuildGraphs();

	QCustomPlot* plot_ = nullptr;
	std::vector<ProbeEntry> probes_;  // Multiple probes
	QTimer* pollTimer_ = nullptr;


	double timeWindow_ = 5.0;  // seconds
	std::size_t sampleCount_ = 512;
	double sampleRate_ = 1000.0;  // Hz (= 1/dt); used when useTimeWindow_ is true
	bool useTimeWindow_ = true;
};


