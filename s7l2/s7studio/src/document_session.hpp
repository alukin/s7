// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram_document.hpp"

#include <QString>
#include <boost/json.hpp>
#include <utility>

namespace s7studio {

// The document data shared by editing, local runs and deployment. The scene
// remains authoritative for topology; this class owns the surrounding schema,
// run settings, deployment metadata and save point.
class DocumentSession final
{
public:
    DocumentSession();

    void openSchema(boost::json::value schema, QString sourceLabel);
    void openDiagram(const diagram::DiagramDocument& document, QString sourceLabel);
    void reset();
    void setDeployment(boost::json::array runtimeNodes, boost::json::array streams);
    void markSaved(boost::json::value document);
    [[nodiscard]] bool modified(const boost::json::value& current) const;
    [[nodiscard]] auto complete(diagram::DiagramDocument document) const -> diagram::DiagramDocument;

    [[nodiscard]] auto schema() -> boost::json::value& { return schema_; }
    [[nodiscard]] auto schema() const -> const boost::json::value& { return schema_; }
    [[nodiscard]] auto description() const -> const QString& { return description_; }
    void setDescription(QString value) { description_ = std::move(value); }
    [[nodiscard]] auto sourceLabel() const -> const QString& { return sourceLabel_; }
    void setSourceLabel(QString value) { sourceLabel_ = std::move(value); }
    [[nodiscard]] auto path() const -> const QString& { return path_; }
    void setPath(QString value) { path_ = std::move(value); }
    void clearPath() { path_.clear(); }
    [[nodiscard]] auto runtimeNodes() const -> const boost::json::array& { return runtimeNodes_; }
    [[nodiscard]] auto streams() const -> const boost::json::array& { return streams_; }
    [[nodiscard]] auto runtimeSeconds() const -> double { return runtimeSeconds_; }
    void setRuntimeSeconds(double value) { runtimeSeconds_ = value; }
    [[nodiscard]] auto frequencyHz() const -> double { return frequencyHz_; }
    void setFrequencyHz(double value) { frequencyHz_ = value; }
    [[nodiscard]] auto clearScopesBeforeRun() const -> bool { return clearScopesBeforeRun_; }
    void setClearScopesBeforeRun(bool value) { clearScopesBeforeRun_ = value; }

private:
    boost::json::value schema_;
    QString description_;
    QString sourceLabel_;
    QString path_;
    boost::json::array runtimeNodes_;
    boost::json::array streams_;
    boost::json::value savedDocument_;
    bool hasSavedDocument_ = false;
    double frequencyHz_ = 1000.0;
    double runtimeSeconds_ = 60.0;
    bool clearScopesBeforeRun_ = true;
};

} // namespace s7studio
