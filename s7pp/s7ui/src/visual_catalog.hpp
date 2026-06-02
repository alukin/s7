#pragma once

#include <QString>

#include <QRectF>
#include <QSizeF>

#include <functional>
#include <optional>
#include <unordered_map>

namespace s7ui::visual {

struct VisualSpec {
    QString type;
    QString displayName;
    QString category;
    QString shape = "rect";
    QString color = "#4f4f4f";
    QString description;
    std::optional<QSizeF> size;
    std::optional<QRectF> previewArea;
};

class Catalog {
public:
    void loadFromDir(const QString& dirPath, const std::function<void(const QString&)>& logWarning);
    [[nodiscard]] auto specForType(const QString& typeName) const -> std::optional<VisualSpec>;

private:
    std::unordered_map<std::string, VisualSpec> byType_;
};

} // namespace s7ui::visual

