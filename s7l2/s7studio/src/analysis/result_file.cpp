// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "result_file.hpp"
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <stdexcept>
namespace s7studio::analysis {
namespace {
constexpr quint32 magic = 0x53374152, version = 1;
void checked(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void axis(const std::vector<double> &values) {
    double previous = -INFINITY;
    for (double v : values) {
        checked(std::isfinite(v) && v > previous, "Result axes must be finite and increasing");
        previous = v;
    }
}
} // namespace
void ResultFile::validate() const {
    checked(!kind.isEmpty() && !x.empty() && !y.empty() && x.size() <= 65536 && y.size() <= 65536 &&
                x.size() <= 8388608 / y.size(),
            "Invalid result dimensions/type");
    checked(values.size() == x.size() * y.size() && valid.size() == values.size(), "Result shape mismatch");
    axis(x);
    axis(y);
    checked(input.size() <= 65536, "Result input frame exceeds limit");
    for (auto v : input)
        checked(std::isfinite(v), "Input frame must be finite");
    for (std::size_t i = 0; i < values.size(); ++i)
        if (valid[i])
            checked(std::isfinite(values[i].real()) && std::isfinite(values[i].imag()),
                    "Valid result cell is nonfinite");
}
void ResultFile::save(const QString &path) const {
    validate();
    auto meta = metadata;
    meta["kind"] = kind;
    const auto header = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    checked(header.size() <= 1048576, "Result metadata exceeds limit");
    QSaveFile file(path);
    checked(file.open(QIODevice::WriteOnly), "Cannot open result destination");
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);
    out << magic << version << quint32(header.size());
    out.writeRawData(header.constData(), header.size());
    out << quint32(x.size()) << quint32(y.size()) << quint32(input.size());
    for (auto v : x)
        out << v;
    for (auto v : y)
        out << v;
    for (auto v : input)
        out << v;
    for (std::size_t i = 0; i < values.size(); ++i)
        out << values[i].real() << values[i].imag() << quint8(valid[i]);
    checked(out.status() == QDataStream::Ok && file.commit(), "Result save failed");
}
ResultFile ResultFile::open(const QString &path) {
    QFile file(path);
    checked(file.open(QIODevice::ReadOnly), "Cannot open result");
    checked(file.size() <= 160 * 1024 * 1024, "Result file exceeds limit");
    if (file.peek(1) == "#") {
        const auto header = file.readLine(1048577);
        checked(header.size() <= 1048576, "CSV metadata exceeds limit");
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(header.mid(1).trimmed(), &error);
        checked(error.error == QJsonParseError::NoError && json.isObject(), "Invalid CSV metadata");
        ResultFile result;
        result.metadata = json.object();
        result.kind = result.metadata.take("kind").toString();
        const auto nx = result.metadata["columns"].toInt(), ny = result.metadata["rows"].toInt();
        checked(nx > 0 && ny > 0 && nx <= 65536 && ny <= 65536 && nx <= 8388608 / ny,
                "Invalid CSV dimensions");
        result.x.assign(std::size_t(nx), NAN);
        result.y.assign(std::size_t(ny), NAN);
        result.values.resize(std::size_t(nx) * std::size_t(ny));
        result.valid.resize(result.values.size());
        std::vector<bool> seen(result.values.size(), false);
        checked(file.readLine().trimmed() == "column,row,x,y,re,im,valid", "Unsupported CSV columns");
        std::size_t count = 0;
        while (!file.atEnd()) {
            const auto line = file.readLine(4096).trimmed();
            if (line.isEmpty())
                continue;
            const auto fields = line.split(',');
            checked(fields.size() == 7, "Malformed result CSV row");
            bool ok = false;
            const int c = fields[0].toInt(&ok);
            checked(ok && c >= 0 && c < nx, "Invalid CSV column");
            const int r = fields[1].toInt(&ok);
            checked(ok && r >= 0 && r < ny, "Invalid CSV row");
            const auto i = std::size_t(r) * std::size_t(nx) + std::size_t(c);
            checked(!seen[i], "Duplicate CSV cell");
            seen[i] = true;
            ++count;
            auto number = [&](int field) {
                bool valid = false;
                const double v = fields[field].toDouble(&valid);
                checked(valid, "Invalid CSV number");
                return v;
            };
            const auto x = number(2), y = number(3);
            checked(!std::isfinite(result.x[std::size_t(c)]) || result.x[std::size_t(c)] == x,
                    "Inconsistent CSV X axis");
            checked(!std::isfinite(result.y[std::size_t(r)]) || result.y[std::size_t(r)] == y,
                    "Inconsistent CSV Y axis");
            result.x[std::size_t(c)] = x;
            result.y[std::size_t(r)] = y;
            result.values[i] = {number(4), number(5)};
            const int valid = fields[6].toInt(&ok);
            checked(ok && (valid == 0 || valid == 1), "Invalid CSV mask");
            result.valid[i] = valid;
        }
        checked(count == result.values.size(), "Missing CSV cells");
        result.validate();
        return result;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 m = 0, v = 0, bytes = 0, nx = 0, ny = 0, ni = 0;
    in >> m >> v >> bytes;
    checked(m == magic && v == version && bytes <= 1048576, "Invalid result header/version");
    QByteArray header(bytes, Qt::Uninitialized);
    checked(in.readRawData(header.data(), bytes) == qint64(bytes), "Truncated result metadata");
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(header, &error);
    checked(error.error == QJsonParseError::NoError && json.isObject(), "Invalid result metadata");
    in >> nx >> ny >> ni;
    checked(ni <= 65536, "Invalid input frame size");
    checked(nx > 0 && ny > 0 && nx <= 65536 && ny <= 65536 && nx <= 8388608 / ny,
            "Invalid result dimensions");
    checked(file.bytesAvailable() == qint64((quint64(nx) + ny + ni) * 8 + quint64(nx) * ny * 17),
            "Truncated or trailing result data");
    ResultFile result;
    result.metadata = json.object();
    result.kind = result.metadata.take("kind").toString();
    result.x.resize(nx);
    result.y.resize(ny);
    result.input.resize(ni);
    result.values.resize(std::size_t(nx) * ny);
    result.valid.resize(result.values.size());
    for (auto &a : result.x)
        in >> a;
    for (auto &a : result.y)
        in >> a;
    for (auto &a : result.input)
        in >> a;
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        double re, im;
        quint8 valid;
        in >> re >> im >> valid;
        checked(valid <= 1, "Invalid result mask");
        result.values[i] = {re, im};
        result.valid[i] = valid;
    }
    checked(in.status() == QDataStream::Ok, "Result read failed");
    result.validate();
    return result;
}
void ResultFile::csv(const QString &path) const {
    validate();
    QSaveFile file(path);
    checked(file.open(QIODevice::WriteOnly), "Cannot open CSV destination");
    QTextStream out(&file);
    out.setRealNumberPrecision(17);
    auto meta = metadata;
    meta["kind"] = kind;
    meta["version"] = 1;
    meta["columns"] = int(x.size());
    meta["rows"] = int(y.size());
    out << "# " << QJsonDocument(meta).toJson(QJsonDocument::Compact) << "\ncolumn,row,x,y,re,im,valid\n";
    for (std::size_t i = 0; i < values.size(); ++i)
        out << i % x.size() << ',' << i / x.size() << ',' << x[i % x.size()] << ',' << y[i / x.size()] << ','
            << values[i].real() << ',' << values[i].imag() << ',' << int(valid[i]) << '\n';
    out.flush();
    checked(out.status() == QTextStream::Ok && file.commit(), "CSV save failed");
}
} // namespace s7studio::analysis
