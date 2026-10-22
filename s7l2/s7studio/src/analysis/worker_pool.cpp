// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/worker_pool.hpp"
#include "analysis/fft.hpp"
#include <QThreadPool>
namespace s7studio::analysis {
QThreadPool& analysisPool()
{
    Fft::initialize(); // Planner lock must outlive all jobs during static teardown.
    static QThreadPool pool;
    static const bool initialized = [] { pool.setMaxThreadCount(2); return true; }();
    (void)initialized;
    return pool;
}
}
