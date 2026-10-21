#pragma once
#include <s7/element_metadata.hpp>
#include <QWidget>
#include <optional>
#include <vector>
class QTabWidget;
class QPlainTextEdit;
class QCheckBox;
class QLabel;
class QFormLayout;
namespace s7studio::ui {
// Metadata-based UI only; runtime metadata remains the validation authority.
class ParameterEditor final : public QWidget {
public:
    ParameterEditor(std::optional<s7::ElementMetadata> metadata, boost::json::object params, QWidget* parent = nullptr);
    bool readParameters(boost::json::object& result, QString& error) const;
    void showError(const QString& error);
private:
    struct Field { s7::ParameterSpec spec; QWidget* editor; QCheckBox* enabled; };
    bool readForm(boost::json::object& result, QString& error) const;
    void loadForm(const boost::json::object& params);
    std::optional<s7::ElementMetadata> metadata_;
    boost::json::object original_;
    std::vector<Field> fields_;
    QTabWidget* tabs_;
    QPlainTextEdit* json_;
    QLabel* error_;
    int previousTab_ = 0;
};
} // namespace s7studio::ui
