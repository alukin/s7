// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "observer_presentation.hpp"

namespace s7studio::viewers {
// Bounded recorded-data analysis on the preview worker, never on the GUI thread.
std::optional<PreviewData> prepareAnalysisPreview(const PreviewRequest &request);
}
