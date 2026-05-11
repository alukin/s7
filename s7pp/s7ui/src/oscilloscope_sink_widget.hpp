#pragma once

#include <QWidget>

class QCustomPlot;
class QCPGraph;
class QTimer;

class OscilloscopeSinkWidget : public QWidget
{
	Q_OBJECT

public:
	explicit OscilloscopeSinkWidget(QWidget* parent = nullptr);

private:
	void appendSample();
	QCustomPlot* plot_ = nullptr;
	QCPGraph* graph_ = nullptr;
	QTimer* timer_ = nullptr;
	double timeSec_ = 0.0;
};


