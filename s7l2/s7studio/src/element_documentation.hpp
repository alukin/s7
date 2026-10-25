// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <s7/element_metadata.hpp>
#include <QString>

class QWidget;
class QPlainTextEdit;
namespace s7 { class ElementRegistry; }

namespace s7studio::ui {
// One plain-text presentation for registry, schema and observer documentation.
QString elementDocumentation(const s7::ElementMetadata& metadata);
QString elementDocumentation(const s7::ElementRegistry& registry, const QString& type,
                             const boost::json::value& nestedSchema = {});
QPlainTextEdit* createElementDocumentation(const QString& text, QWidget* parent);
void showElementInformation(const s7::ElementRegistry& registry, const QString& type,
                            QWidget* parent, const boost::json::value& nestedSchema = {});
}
