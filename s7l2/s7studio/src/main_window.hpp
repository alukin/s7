#pragma once

#include "demo_schema.hpp"
#include "local_schema_runner.hpp"
#include "model_run_controller.hpp"

#include <QMainWindow>

#include <memory>

class QAction;
class QLabel;
class QPlainTextEdit;
class QDoubleSpinBox;
class QSpinBox;

namespace s7studio {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void appendLog(const QString& message);
    void onStatusChanged(const QString& status);
    void onRunCompleted(bool success, const QString& error);
    void onTickProgress(long completed, long total);

private:
    void buildUi();
    void buildActions();
    void refreshActions();

    boost::json::value runtimeSchema_;
    LocalSchemaRunner* runner_ = nullptr;
    std::unique_ptr<ui::ModelRunController> controller_;

    QAction* runAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* resumeAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* resetAction_ = nullptr;

    QDoubleSpinBox* dtSpin_ = nullptr;
    QSpinBox* hoursSpin_ = nullptr;
    QSpinBox* minutesSpin_ = nullptr;
    QSpinBox* secondsSpin_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
};

} // namespace s7studio
