// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "printing/print_document.hpp"

#include <optional>

class QWidget;

namespace s7studio::printing {

// What happened to an interactive print job. A cancelled dialog is not a
// success and not an error.
struct PrintResult {
    enum class Outcome { Done, Cancelled, Failed };
    Outcome outcome = Outcome::Cancelled;
    QString message;  // the file written, or the error
    [[nodiscard]] bool done() const { return outcome == Outcome::Done; }
};

// Asks for a file name (suggested: `suggestedPath`) and writes the PDF.
PrintResult exportPdf(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout, const QString& suggestedPath);
// The system print dialog, starting from `layout`; prints with the paper and
// orientation chosen there.
PrintResult printWithDialog(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout);
// Print preview; the preview's Print button prints with its page setup.
PrintResult preview(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout);
// Page setup dialog; empty when cancelled.
std::optional<QPageLayout> pageSetup(QWidget* parent, const QPageLayout& layout);

// A short description, e.g. "A4 landscape".
[[nodiscard]] QString describe(const QPageLayout& layout);
// The default paper of the system's default printer (A4 or Letter by locale
// when there is none), portrait, 10 mm margins.
[[nodiscard]] QPageLayout defaultLayout();
// A layout for a sheet of the given size, with the standard 10 mm margins.
[[nodiscard]] QPageLayout sheetLayout(double widthMm, double heightMm, bool landscape);

} // namespace s7studio::printing
