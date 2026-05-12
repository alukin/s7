#include "visual_catalog.hpp"

#include <boost/json.hpp>

#include <QDir>
#include <QFile>

namespace s7ui::visual {

void Catalog::loadFromDir(const QString& dirPath, const std::function<void(const QString&)>& logWarning)
{
    byType_.clear();

    QDir dir(dirPath);
    if (!dir.exists()) {
        if (logWarning) {
            logWarning(QString("[visuals] catalog directory not found: %1").arg(dirPath));
        }
        return;
    }

    const auto files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& fileName : files) {
        QFile f(dir.filePath(fileName));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (logWarning) {
                logWarning(QString("[visuals] cannot open %1").arg(f.fileName()));
            }
            continue;
        }

        try {
            const auto data = f.readAll();
            const auto root = boost::json::parse(std::string(data.constData(), static_cast<std::size_t>(data.size())));
            if (!root.is_object()) {
                if (logWarning) {
                    logWarning(QString("[visuals] %1 root must be object").arg(fileName));
                }
                continue;
            }

            const auto& obj = root.as_object();
            const auto* typeVal = obj.if_contains("type");
            if (!typeVal || !typeVal->is_string()) {
                if (logWarning) {
                    logWarning(QString("[visuals] %1 missing string field 'type'").arg(fileName));
                }
                continue;
            }

            VisualSpec spec;
            spec.type = QString::fromStdString(boost::json::value_to<std::string>(*typeVal));
            spec.displayName = spec.type;
            if (const auto* v = obj.if_contains("display_name"); v && v->is_string()) {
                spec.displayName = QString::fromStdString(boost::json::value_to<std::string>(*v));
            }
            if (const auto* v = obj.if_contains("category"); v && v->is_string()) {
                spec.category = QString::fromStdString(boost::json::value_to<std::string>(*v));
            }
            if (const auto* v = obj.if_contains("shape"); v && v->is_string()) {
                spec.shape = QString::fromStdString(boost::json::value_to<std::string>(*v));
            }
            if (const auto* v = obj.if_contains("description"); v && v->is_string()) {
                spec.description = QString::fromStdString(boost::json::value_to<std::string>(*v));
            }
            if (const auto* styleVal = obj.if_contains("node_style"); styleVal && styleVal->is_object()) {
                const auto& styleObj = styleVal->as_object();
                if (const auto* colorVal = styleObj.if_contains("color"); colorVal && colorVal->is_string()) {
                    spec.color = QString::fromStdString(boost::json::value_to<std::string>(*colorVal));
                }
            }

            byType_[spec.type.toStdString()] = std::move(spec);
        } catch (const std::exception& ex) {
            if (logWarning) {
                logWarning(QString("[visuals] parse failed for %1: %2").arg(fileName).arg(QString::fromUtf8(ex.what())));
            }
        }
    }
}

auto Catalog::specForType(const QString& typeName) const -> std::optional<VisualSpec>
{
    const auto it = byType_.find(typeName.toStdString());
    if (it == byType_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace s7ui::visual

