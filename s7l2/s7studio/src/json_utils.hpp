// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QFile>
#include <QPointF>
#include <QString>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <s7/json_parse.hpp>

namespace s7studio::json_utils {

inline auto findObjectValue(const boost::json::object& object, const char* key) -> const boost::json::value*
{
    if (const auto it = object.find(key); it != object.end()) {
        return &it->value();
    }

    return nullptr;
}

inline auto stringValue(const boost::json::object& object,
                        const char* key,
                        const std::string& fallback = {}) -> std::string
{
    if (const auto* value = findObjectValue(object, key); value && value->is_string()) {
        return std::string(value->as_string().c_str());
    }

    return fallback;
}

inline auto numberValue(const boost::json::value& value, const double fallback = 0.0) -> double
{
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }

    return fallback;
}

// A number outside int's range (or NaN) yields the fallback: converting it
// would be undefined behaviour.
inline auto intValue(const boost::json::object& object, const char* key, const int fallback = 0) -> int
{
    if (const auto* value = findObjectValue(object, key)) {
        const double number = numberValue(*value, fallback);
        if (!(number >= static_cast<double>(std::numeric_limits<int>::min()) &&
              number <= static_cast<double>(std::numeric_limits<int>::max()))) {
            return fallback;
        }
        return static_cast<int>(number);
    }

    return fallback;
}

inline auto pointValue(const boost::json::object& object, const char* key, QPointF fallback = {}) -> QPointF
{
    const auto* value = findObjectValue(object, key);
    if (!value || !value->is_object()) {
        return fallback;
    }

    const auto& pointObject = value->as_object();
    const auto* xValue = findObjectValue(pointObject, "x");
    const auto* yValue = findObjectValue(pointObject, "y");
    if (!xValue || !yValue) {
        return fallback;
    }

    return QPointF(numberValue(*xValue), numberValue(*yValue));
}

inline auto parseFile(const QString& filePath) -> boost::json::value
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(QString("Failed to open '%1': %2").arg(filePath, file.errorString()).toStdString());
    }

    const QByteArray bytes = file.readAll();
    boost::system::error_code ec;
    const auto parsed = s7::parse_json(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), ec);
    if (ec) {
        throw std::runtime_error(
            QString("Failed to parse '%1': %2").arg(filePath, QString::fromUtf8(ec.message())).toStdString());
    }

    return parsed;
}

} // namespace s7studio::json_utils

