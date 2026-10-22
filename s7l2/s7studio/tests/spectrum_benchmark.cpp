// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/spectrum.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numbers>
#include <cmath>
int main()
{
    for (std::size_t n : {4096, 65536}) {
        s7studio::observation::LiveFrame frame;
        frame.runId = 1; frame.rateNumerator = 100000; frame.channels = {{"tone", "V", "f64"}};
        for (std::size_t i = 0; i < n; ++i) {
            frame.steps.push_back(static_cast<std::int64_t>(i+1));
            frame.times.push_back(static_cast<double>(i+1)/100000);
            frame.values.push_back(std::sin(2*std::numbers::pi*64*static_cast<double>(i)/static_cast<double>(n)));
        }
        s7studio::analysis::SpectrumAnalyzer analyzer;
        s7studio::analysis::SpectrumSettings settings; settings.size = n;
        std::vector<double> times;
        for (int i = 0; i < 330; ++i) {
            frame.generation = static_cast<std::uint64_t>(i+1);
            const auto start = std::chrono::steady_clock::now();
            const auto result = analyzer.analyze(frame, settings);
            if (result.state != s7studio::analysis::SpectrumResult::State::Ready || std::abs(result.values[64]-1) > 1e-10) return 1;
            if (i >= 30) times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        std::sort(times.begin(), times.end());
        std::cout << "N=" << n << " count=" << times.size() << " p50_ms=" << times[times.size()/2]
                  << " p95_ms=" << times[times.size()*95/100] << " max_ms=" << times.back() << '\n';
    }
}
