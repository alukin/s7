// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Opt-in buffered workload for evaluating branch scheduling. S7 ports do not
// yet carry frames, so this is an algorithm prototype, not a runtime element.

#include <fftw3.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct PhaseTimes {
    double clear_ns{};
    double copy_ns{};
    double forward_ns{};
    double multiply_ns{};
    double inverse_ns{};
    double output_ns{};
};

double elapsed_ns(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::nano>(end - start).count();
}

int current_cpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

void pin_current_thread(int cpu) {
    if (cpu < 0) return;
#if defined(__linux__)
    if (cpu >= CPU_SETSIZE) throw std::invalid_argument("CPU index is outside cpu_set_t.");
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0)
        throw std::runtime_error("Could not pin benchmark thread to CPU " + std::to_string(cpu));
#else
    throw std::runtime_error("Thread pinning is supported only on Linux.");
#endif
}

class BufferedFftConvolution {
public:
    using Complex = std::complex<double>;

    BufferedFftConvolution(std::size_t frame_size, std::span<const Complex> impulse)
        : frame_size_(frame_size)
    {
        constexpr std::size_t max_fft_size = 1U << 30;
        if (frame_size == 0 || impulse.empty() || frame_size > max_fft_size ||
            impulse.size() > max_fft_size || frame_size + impulse.size() - 1 > max_fft_size)
            throw std::invalid_argument("FFT convolution needs nonempty buffers within FFTW's size limit.");
        output_.resize(frame_size + impulse.size() - 1);
        fft_size_ = std::bit_ceil(output_.size());
        try {
            input_ = fftw_alloc_complex(fft_size_);
            spectrum_ = fftw_alloc_complex(fft_size_);
            product_ = fftw_alloc_complex(fft_size_);
            inverse_output_ = fftw_alloc_complex(fft_size_);
            if (!input_ || !spectrum_ || !product_ || !inverse_output_) throw std::bad_alloc();
            // FFTW plans are created on the caller thread before workers run.
            forward_ = fftw_plan_dft_1d(static_cast<int>(fft_size_), input_, spectrum_, FFTW_FORWARD, FFTW_ESTIMATE);
            inverse_ = fftw_plan_dft_1d(static_cast<int>(fft_size_), product_, inverse_output_, FFTW_BACKWARD, FFTW_ESTIMATE);
            if (!forward_ || !inverse_) throw std::runtime_error("FFTW could not plan convolution.");
            clear_input();
            for (std::size_t i = 0; i < impulse.size(); ++i) {
                input_[i][0] = impulse[i].real();
                input_[i][1] = impulse[i].imag();
            }
            fftw_execute(forward_);
            kernel_spectrum_.reserve(fft_size_);
            for (std::size_t i = 0; i < fft_size_; ++i)
                kernel_spectrum_.emplace_back(spectrum_[i][0], spectrum_[i][1]);
        } catch (...) {
            release();
            throw;
        }
    }

    ~BufferedFftConvolution() { release(); }
    BufferedFftConvolution(const BufferedFftConvolution&) = delete;
    BufferedFftConvolution& operator=(const BufferedFftConvolution&) = delete;

    [[nodiscard]] std::span<const Complex> step(std::span<const Complex> frame, PhaseTimes* phases = nullptr) {
        if (frame.size() != frame_size_) throw std::invalid_argument("Convolution frame size changed.");
        auto previous = phases ? Clock::now() : Clock::time_point{};
        const auto record = [&](double PhaseTimes::* field) {
            if (!phases) return;
            const auto now = Clock::now();
            phases->*field += elapsed_ns(previous, now);
            previous = now;
        };
        clear_input();
        record(&PhaseTimes::clear_ns);
        for (std::size_t i = 0; i < frame_size_; ++i) {
            input_[i][0] = frame[i].real();
            input_[i][1] = frame[i].imag();
        }
        record(&PhaseTimes::copy_ns);
        fftw_execute(forward_);
        record(&PhaseTimes::forward_ns);
        for (std::size_t i = 0; i < fft_size_; ++i) {
            const auto [hr, hi] = kernel_spectrum_[i];
            const double xr = spectrum_[i][0];
            const double xi = spectrum_[i][1];
            product_[i][0] = xr * hr - xi * hi;
            product_[i][1] = xr * hi + xi * hr;
        }
        record(&PhaseTimes::multiply_ns);
        fftw_execute(inverse_);
        record(&PhaseTimes::inverse_ns);
        const double scale = 1.0 / static_cast<double>(fft_size_);
        for (std::size_t i = 0; i < output_.size(); ++i)
            output_[i] = {inverse_output_[i][0] * scale, inverse_output_[i][1] * scale};
        record(&PhaseTimes::output_ns);
        ++step_count_;
        return output_;
    }

    [[nodiscard]] std::span<const Complex> output() const { return output_; }
    [[nodiscard]] std::size_t step_count() const { return step_count_; }

private:
    void clear_input() {
        for (std::size_t i = 0; i < fft_size_; ++i) {
            input_[i][0] = 0.0;
            input_[i][1] = 0.0;
        }
    }

    void release() noexcept {
        if (forward_) fftw_destroy_plan(forward_);
        if (inverse_) fftw_destroy_plan(inverse_);
        if (input_) fftw_free(input_);
        if (spectrum_) fftw_free(spectrum_);
        if (product_) fftw_free(product_);
        if (inverse_output_) fftw_free(inverse_output_);
    }

    std::size_t frame_size_{};
    std::size_t fft_size_{};
    std::size_t step_count_{};
    std::vector<Complex> output_;
    std::vector<Complex> kernel_spectrum_;
    fftw_complex* input_{};
    fftw_complex* spectrum_{};
    fftw_complex* product_{};
    fftw_complex* inverse_output_{};
    fftw_plan forward_{};
    fftw_plan inverse_{};
};

using Complex = std::complex<double>;

void require_close(Complex actual, Complex expected, const char* message) {
    if (std::abs(actual - expected) > 1e-9 * std::max({1.0, std::abs(actual), std::abs(expected)}))
        throw std::runtime_error(message);
}

void verify_convolution() {
    const std::vector<Complex> frame{{1.0, 0.5}, {-0.25, 0.2}, {0.75, -0.1}, {0.0, 0.3}, {0.4, 0.0}};
    const std::vector<Complex> impulse{{0.5, -0.2}, {-0.1, 0.4}, {0.3, 0.0}};
    BufferedFftConvolution fft{frame.size(), impulse};
    const auto actual = fft.step(frame);
    for (std::size_t n = 0; n < actual.size(); ++n) {
        Complex expected{};
        for (std::size_t k = 0; k < impulse.size(); ++k)
            if (n >= k && n - k < frame.size()) expected += impulse[k] * frame[n - k];
        require_close(actual[n], expected, "Complex FFT convolution differs from direct reference.");
    }

    // Matched-filter pulse compression: a delayed copy of the transmit pulse
    // peaks at delay + pulse_length - 1 after convolution with its reversed
    // conjugate. The test uses one isolated echo.
    const std::vector<Complex> pulse{{1.0, 0.2}, {0.1, 0.8}, {-0.7, 0.1}, {0.4, -0.5}};
    std::vector<Complex> echo(32);
    constexpr std::size_t delay = 9;
    std::copy(pulse.begin(), pulse.end(), echo.begin() + delay);
    std::vector<Complex> matched(pulse.size());
    for (std::size_t i = 0; i < pulse.size(); ++i)
        matched[i] = std::conj(pulse[pulse.size() - 1 - i]);
    BufferedFftConvolution matched_filter{echo.size(), matched};
    const auto compressed = matched_filter.step(echo);
    const auto peak = std::max_element(compressed.begin(), compressed.end(),
                                       [](Complex a, Complex b) { return std::norm(a) < std::norm(b); });
    if (static_cast<std::size_t>(peak - compressed.begin()) != delay + pulse.size() - 1)
        throw std::runtime_error("Matched-filter peak is at the wrong delay.");
}

std::vector<Complex> make_frame(std::size_t size) {
    std::vector<Complex> frame(size);
    for (std::size_t i = 0; i < size; ++i)
        frame[i] = {std::sin(0.017 * static_cast<double>(i)), std::cos(0.031 * static_cast<double>(i))};
    return frame;
}

std::vector<Complex> make_impulse(std::size_t size, double phase) {
    std::vector<Complex> impulse(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double x = static_cast<double>(i);
        impulse[i] = std::exp(-0.0004 * x) * Complex{std::cos(phase * x), std::sin(phase * x)};
    }
    return impulse;
}

struct TimedOutputs {
    double ns_per_frame{};
    std::vector<std::pair<Complex, Complex>> probes;
    double left_work_ns{};
    double right_work_ns{};
    double start_wait_ns{};
    double done_wait_ns{};
    PhaseTimes left_phases;
    PhaseTimes right_phases;
    int main_cpu{-1};
    int worker_cpu{-1};
    std::size_t same_core_steps{};
};

TimedOutputs run_serial(std::size_t frame_size, std::span<const Complex> h1,
                        std::span<const Complex> h2, std::size_t steps, bool diagnose) {
    auto frame = make_frame(frame_size);
    BufferedFftConvolution left{frame_size, h1};
    BufferedFftConvolution right{frame_size, h2};
    TimedOutputs result;
    result.probes.reserve(steps);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < steps; ++i) {
        frame[0] = {static_cast<double>(i) / steps, -static_cast<double>(i) / steps};
        const auto left_start = diagnose ? Clock::now() : Clock::time_point{};
        const auto a = left.step(frame, diagnose ? &result.left_phases : nullptr);
        const auto right_start = diagnose ? Clock::now() : Clock::time_point{};
        const auto b = right.step(frame, diagnose ? &result.right_phases : nullptr);
        if (diagnose) {
            const auto end = Clock::now();
            result.left_work_ns += elapsed_ns(left_start, right_start);
            result.right_work_ns += elapsed_ns(right_start, end);
        }
        result.probes.emplace_back(a[frame_size / 2], b[frame_size / 2]);
    }
    result.ns_per_frame = elapsed_ns(start, Clock::now()) / steps;
    result.left_work_ns /= steps;
    result.right_work_ns /= steps;
    if (left.step_count() != steps || right.step_count() != steps)
        throw std::runtime_error("Serial branch executed an unexpected number of frames.");
    return result;
}

TimedOutputs run_parallel(std::size_t frame_size, std::span<const Complex> h1,
                          std::span<const Complex> h2, std::size_t steps, bool diagnose, int worker_cpu) {
    auto frame = make_frame(frame_size);
    BufferedFftConvolution left{frame_size, h1};
    BufferedFftConvolution right{frame_size, h2};
    std::barrier start_gate{2};
    std::barrier done_gate{2};
    std::atomic<bool> stopping{false};
    double worker_work_ns = 0;
    PhaseTimes worker_phases;
    std::vector<int> worker_cpus(diagnose ? steps : 0);
    std::jthread worker([&] {
        pin_current_thread(worker_cpu);
        std::size_t frame_index = 0;
        for (;;) {
            start_gate.arrive_and_wait();
            if (stopping.load()) break;
            if (diagnose) worker_cpus[frame_index] = current_cpu();
            const auto start = diagnose ? Clock::now() : Clock::time_point{};
            static_cast<void>(right.step(frame, diagnose ? &worker_phases : nullptr));
            if (diagnose) worker_work_ns += elapsed_ns(start, Clock::now());
            done_gate.arrive_and_wait();
            ++frame_index;
        }
    });
    TimedOutputs result;
    result.probes.reserve(steps);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < steps; ++i) {
        frame[0] = {static_cast<double>(i) / steps, -static_cast<double>(i) / steps};
        const auto gate_start = diagnose ? Clock::now() : Clock::time_point{};
        start_gate.arrive_and_wait();
        const auto cpu = diagnose ? current_cpu() : -1;
        const auto left_start = diagnose ? Clock::now() : Clock::time_point{};
        const auto a = left.step(frame, diagnose ? &result.left_phases : nullptr);
        const auto left_end = diagnose ? Clock::now() : Clock::time_point{};
        done_gate.arrive_and_wait();
        if (diagnose) {
            const auto end = Clock::now();
            result.start_wait_ns += elapsed_ns(gate_start, left_start);
            result.left_work_ns += elapsed_ns(left_start, left_end);
            result.done_wait_ns += elapsed_ns(left_end, end);
            if (i == 0) {
                result.main_cpu = cpu;
                result.worker_cpu = worker_cpus[i];
            }
            result.same_core_steps += cpu >= 0 && worker_cpus[i] >= 0 && cpu == worker_cpus[i];
        }
        // The done barrier publishes the worker's output. It cannot write the
        // next frame until the coordinator releases the following start gate.
        const auto b = right.output();
        result.probes.emplace_back(a[frame_size / 2], b[frame_size / 2]);
    }
    result.ns_per_frame = elapsed_ns(start, Clock::now()) / steps;
    stopping.store(true);
    start_gate.arrive_and_wait();
    worker.join();
    result.right_work_ns = worker_work_ns / steps;
    result.start_wait_ns /= steps;
    result.done_wait_ns /= steps;
    result.left_work_ns /= steps;
    result.right_phases = worker_phases;
    if (left.step_count() != steps || right.step_count() != steps)
        throw std::runtime_error("Parallel branch executed an unexpected number of frames.");
    return result;
}

struct WorkerOnlyResult {
    double work_ns{};
    PhaseTimes phases;
    std::vector<Complex> probes;
};

WorkerOnlyResult run_worker_only(std::size_t frame_size, std::span<const Complex> impulse,
                                 std::size_t steps, int worker_cpu) {
    auto frame = make_frame(frame_size);
    BufferedFftConvolution right{frame_size, impulse};
    std::barrier start_gate{2};
    std::barrier done_gate{2};
    std::atomic<bool> stopping{false};
    WorkerOnlyResult result;
    result.probes.reserve(steps);
    std::jthread worker([&] {
        pin_current_thread(worker_cpu);
        for (;;) {
            start_gate.arrive_and_wait();
            if (stopping.load()) break;
            const auto start = Clock::now();
            static_cast<void>(right.step(frame, &result.phases));
            result.work_ns += elapsed_ns(start, Clock::now());
            done_gate.arrive_and_wait();
        }
    });
    for (std::size_t i = 0; i < steps; ++i) {
        frame[0] = {static_cast<double>(i) / steps, -static_cast<double>(i) / steps};
        start_gate.arrive_and_wait();
        done_gate.arrive_and_wait();
        result.probes.push_back(right.output()[frame_size / 2]);
    }
    stopping.store(true);
    start_gate.arrive_and_wait();
    worker.join();
    if (right.step_count() != steps)
        throw std::runtime_error("Worker-only branch executed an unexpected number of frames.");
    result.work_ns /= steps;
    return result;
}

void print_phases(const char* label, const PhaseTimes& phases, std::size_t steps) {
    std::cout << "  " << label << " clear=" << phases.clear_ns / steps
              << " copy=" << phases.copy_ns / steps
              << " forward=" << phases.forward_ns / steps
              << " multiply=" << phases.multiply_ns / steps
              << " inverse=" << phases.inverse_ns / steps
              << " output=" << phases.output_ns / steps << " ns\n";
}

void benchmark(std::size_t frame_size, std::size_t impulse_size, std::size_t steps,
               bool diagnose, int worker_cpu) {
    const auto h1 = make_impulse(impulse_size, 0.021);
    const auto h2 = make_impulse(impulse_size, 0.043);
    const auto serial = run_serial(frame_size, h1, h2, steps, diagnose);
    const auto worker_only = diagnose ? std::optional{run_worker_only(frame_size, h2, steps, worker_cpu)}
                                      : std::nullopt;
    const auto parallel = run_parallel(frame_size, h1, h2, steps, diagnose, worker_cpu);
    for (std::size_t i = 0; i < steps; ++i) {
        require_close(parallel.probes[i].first, serial.probes[i].first, "First parallel FFT branch differs from serial.");
        require_close(parallel.probes[i].second, serial.probes[i].second, "Second parallel FFT branch differs from serial.");
        if (worker_only)
            require_close(worker_only->probes[i], serial.probes[i].second, "Worker-only FFT branch differs from serial.");
    }
    std::cout << "frame=" << frame_size << " impulse=" << impulse_size << " steps=" << steps
              << " serial_ns_per_frame=" << serial.ns_per_frame
              << " parallel_ns_per_frame=" << parallel.ns_per_frame
              << " speedup=" << serial.ns_per_frame / parallel.ns_per_frame << '\n';
    if (diagnose) {
        std::cout << "  serial left=" << serial.left_work_ns << " right=" << serial.right_work_ns
                  << " parallel left=" << parallel.left_work_ns << " right=" << parallel.right_work_ns
                  << " start_wait=" << parallel.start_wait_ns << " done_wait=" << parallel.done_wait_ns << " ns\n";
        std::cout << "  first main_cpu=" << parallel.main_cpu << " worker_cpu=" << parallel.worker_cpu
                  << " same_core_steps=" << parallel.same_core_steps << '/' << steps << '\n';
        print_phases("serial_left", serial.left_phases, steps);
        print_phases("serial_right", serial.right_phases, steps);
        std::cout << "  worker_only right=" << worker_only->work_ns << " ns\n";
        print_phases("worker_only_right", worker_only->phases, steps);
        print_phases("parallel_left", parallel.left_phases, steps);
        print_phases("parallel_right", parallel.right_phases, steps);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        bool diagnose = false;
        bool pin_requested = false;
        int main_cpu = -1;
        int worker_cpu = -1;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--diagnose") diagnose = true;
            else if (std::string_view(argv[i]) == "--pin" && i + 2 < argc) {
                pin_requested = true;
                main_cpu = std::stoi(argv[++i]);
                worker_cpu = std::stoi(argv[++i]);
            } else throw std::invalid_argument("Usage: s7core_fft_convolution_benchmark [--diagnose] [--pin MAIN_CPU WORKER_CPU]");
        }
        if (pin_requested) {
            if (main_cpu < 0 || worker_cpu < 0)
                throw std::invalid_argument("Pinned CPU indices must be nonnegative.");
#if defined(__linux__)
            cpu_set_t allowed;
            if (main_cpu >= CPU_SETSIZE || worker_cpu >= CPU_SETSIZE ||
                sched_getaffinity(0, sizeof(allowed), &allowed) != 0 ||
                !CPU_ISSET(main_cpu, &allowed) || !CPU_ISSET(worker_cpu, &allowed))
                throw std::invalid_argument("Requested CPU is outside the process affinity mask.");
#endif
            pin_current_thread(main_cpu);
        }
        verify_convolution();
        std::cout << "complex convolution and matched-filter checks passed\n";
        benchmark(2048, 1024, 128, diagnose, worker_cpu);
        benchmark(8192, 4096, 96, diagnose, worker_cpu);
        benchmark(16384, 8192, 64, diagnose, worker_cpu);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
