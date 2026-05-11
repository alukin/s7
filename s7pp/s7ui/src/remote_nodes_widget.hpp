#pragma once

#include <QWidget>

#include <QString>

class QListWidget;
class QPushButton;

class RemoteNodesWidget : public QWidget
{
	Q_OBJECT

public:
	explicit RemoteNodesWidget(QWidget* parent = nullptr);

Q_SIGNALS:
	void logMessage(const QString& message);

private:
	void refreshNodes();
	void connectSelected();
	void disconnectSelected();
	QListWidget* nodeList_ = nullptr;
	QPushButton* refreshButton_ = nullptr;
	QPushButton* connectButton_ = nullptr;
	QPushButton* disconnectButton_ = nullptr;
};


