//
// Created by al on 6/1/25.
//

#pragma once

#include <stdexcept>
#include <utility>
#include <vector>

#include "s7.hpp"

namespace s7 {

    // -------------------------------------------------------------------------
    // Constant<T>  —  source element that emits a fixed scalar value
    // -------------------------------------------------------------------------
    template<typename T>
    class Constant : public Element
    {
    public:
        static constexpr bool kIsStateful = false;
        static constexpr bool kHasDirectFeedthrough = false;

        Constant() : Constant("", T{}) {}
        explicit Constant(T val) : Constant("", val) {}

        Constant(const std::string& id, T val)
            : Element(id), value_(val)
        {
            setTraits(kIsStateful, kHasDirectFeedthrough);
            addOutput<T>(value_);
        }

        void tick() override {}

    private:
        T value_;
    };

    // -------------------------------------------------------------------------
    // ImpulseSource  —  emits one-sample impulse then zeros
    // -------------------------------------------------------------------------
    class ImpulseSource : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = false;

        ImpulseSource();
        explicit ImpulseSource(Real amplitude);
        ImpulseSource(const std::string& id, Real amplitude);

        void tick() override;

    private:
        Real amplitude_{1.0};
        bool fired_ = false;
        Real out_{0.0};
    };

    // -------------------------------------------------------------------------
    // Integrator<T>  —  stateful accumulator:  state(n+1) = state(n) + input(n)
    // -------------------------------------------------------------------------
    template<typename T>
    class Integrator : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = false;

        explicit Integrator(T init = T{}) : Integrator("", init) {}

        Integrator(const std::string& id, T init)
            : Element(id), state_(init)
        {
            setTraits(kIsStateful, kHasDirectFeedthrough);
            in_ = addInput<T>();
            addOutput<T>(state_);
        }

        void tick() override
        {
            state_ += in_->value();
        }

    private:
        std::shared_ptr<TypedInput<T>> in_;
        T state_{};
    };

    // -------------------------------------------------------------------------
    // Sum<T>  —  weighted sum: output = sum_i( weight_i * input_i )
    // -------------------------------------------------------------------------
    template<typename T>
    class Sum : public Element
    {
    public:
        static constexpr bool kIsStateful = false;
        static constexpr bool kHasDirectFeedthrough = true;

        Sum() : Sum("", std::vector<T>(2, T{1})) {}
        explicit Sum(std::vector<T> weights) : Sum("", std::move(weights)) {}

        Sum(const std::string& id, std::vector<T> weights)
            : Element(id), weights_(std::move(weights))
        {
            setTraits(kIsStateful, kHasDirectFeedthrough);
            if (weights_.empty())
                throw std::invalid_argument("Sum requires at least one weight");

            typed_inputs_.reserve(weights_.size());
            for (std::size_t i = 0; i < weights_.size(); ++i)
                typed_inputs_.push_back(addInput<T>());

            addOutput<T>(output_);
        }

        void tick() override
        {
            T result{};
            for (std::size_t i = 0; i < typed_inputs_.size(); ++i)
                result += typed_inputs_[i]->value() * weights_[i];
            output_ = result;
        }

    private:
        std::vector<std::shared_ptr<TypedInput<T>>> typed_inputs_;
        std::vector<T> weights_;
        T output_{};
    };

    // -------------------------------------------------------------------------
    // FIRFilter  —  y[k] = sum_i b[i] * x[k-i]
    // Internal x_state_ is explicit z^-1 delay chain.
    // -------------------------------------------------------------------------
    class FIRFilter : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = true;

        struct InitState {
            std::vector<Real> x;
        };

        explicit FIRFilter(std::vector<Real> b);
        FIRFilter(const std::string& id, std::vector<Real> b);
        FIRFilter(const std::string& id, std::vector<Real> b, InitState init_state);

        void tick() override;

    private:
        std::shared_ptr<RealInput> in_;
        std::vector<Real> b_;
        std::vector<Real> x_state_;
        Real out_{};
    };

    // -------------------------------------------------------------------------
    // IIRFilter  —  y[k] = sum_i b[i] * x[k-i] - sum_j a[j] * y[k-j-1]
    // a[] contains feedback coefficients a1..aN (a0 is assumed to be 1).
    // Internal x_state_ and y_state_ are explicit z^-1 delay chains.
    // -------------------------------------------------------------------------
    class IIRFilter : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = true;

        struct InitState {
            std::vector<Real> x;
            std::vector<Real> y;
        };

        IIRFilter(std::vector<Real> b, std::vector<Real> a);
        IIRFilter(const std::string& id, std::vector<Real> b, std::vector<Real> a);
        IIRFilter(const std::string& id,
                  std::vector<Real> b,
                  std::vector<Real> a,
                  InitState init_state);
        IIRFilter(const std::string& id, std::vector<Real> b, std::vector<Real> a, bool create_input);

        void tick() override;

    protected:
        std::shared_ptr<RealInput> in_;
        std::vector<Real> b_;
        std::vector<Real> a_;
        std::vector<Real> x_state_;
        std::vector<Real> y_state_;
        Real out_{};
    };

    // -------------------------------------------------------------------------
    // DigitalOscillator  —  autonomous sinusoid using a 2nd-order IIR form
    // y[k] = 2*r*cos(w)*y[k-1] - r^2*y[k-2], w = 2*pi*f*dt
    // -------------------------------------------------------------------------
    class DigitalOscillator : public IIRFilter
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = false;

        struct Params {
            Real frequency_hz = 440.0;
            Real amplitude = 1.0;
            Real phase_rad = 0.0;
            Real radius = 1.0;
        };

        DigitalOscillator();
        explicit DigitalOscillator(Params params);
        DigitalOscillator(const std::string& id, Params params);

        void calculateParameters() override;
        void tick() override;

    private:
        Real frequency_hz_{};
        Real amplitude_{1.0};
        Real phase_rad_{};
        Real radius_{1.0};
        Real coeff_c1_{};
        Real coeff_c2_{};
        Real omega_{};
        bool initialized_ = false;
    };

    // -------------------------------------------------------------------------
    // EnergyDifference  —  accumulated energy of signal difference
    // E[k] = E[k-1] + (u1[k] - u2[k])^2 * dt
    // -------------------------------------------------------------------------
    class EnergyDifference : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = true;

        EnergyDifference();
        explicit EnergyDifference(const std::string& id);

        void calculateParameters() override;
        void tick() override;

    private:
        std::shared_ptr<RealInput> in_a_;
        std::shared_ptr<RealInput> in_b_;
        Real dt_{1.0};
        Real energy_{};
    };

    // -------------------------------------------------------------------------
    // ToComplex  —  two Real inputs (Re, Im) → one Complex output
    // -------------------------------------------------------------------------
    class ToComplex : public Element
    {
    public:
        static constexpr bool kIsStateful = false;
        static constexpr bool kHasDirectFeedthrough = true;

        ToComplex();
        explicit ToComplex(const std::string& id);

        void tick() override;

    private:
        std::shared_ptr<RealInput> re_in_;
        std::shared_ptr<RealInput> im_in_;
        Complex out_{};
    };

    // -------------------------------------------------------------------------
    // ToReals  —  one Complex input → two Real outputs (Re, Im)
    // -------------------------------------------------------------------------
    class ToReals : public Element
    {
    public:
        static constexpr bool kIsStateful = false;
        static constexpr bool kHasDirectFeedthrough = true;

        ToReals();
        explicit ToReals(const std::string& id);

        void tick() override;

    private:
        std::shared_ptr<ComplexInput> in_;
        Real out_re_{};
        Real out_im_{};
    };

    // -------------------------------------------------------------------------
    // Vca  —  voltage controlled amplifier: out = in * gain
    // in: Real, gain/cv: Real, out: Real
    // -------------------------------------------------------------------------
    class Vca : public Element
    {
    public:
        static constexpr bool kIsStateful = false;
        static constexpr bool kHasDirectFeedthrough = true;

        Vca();
        explicit Vca(const std::string& id);

        void tick() override;

    private:
        std::shared_ptr<RealInput> signal_in_;
        std::shared_ptr<RealInput> gain_in_;
        Real out_{};
    };

    // -------------------------------------------------------------------------
    // Vco  —  voltage controlled oscillator (sine)
    // cv in volts modulates frequency: f = base_hz + sensitivity_hz_per_v * cv
    // out: Real sine wave
    // -------------------------------------------------------------------------
    class Vco : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = false;

        struct Params {
            Real base_hz = 440.0;
            Real sensitivity_hz_per_v = 100.0;
            Real amplitude = 1.0;
            Real initial_phase_rad = 0.0;
        };

        Vco();
        explicit Vco(Params params);
        Vco(const std::string& id, Params params);

        void calculateParameters() override;
        void tick() override;

    private:
        std::shared_ptr<RealInput> cv_in_;
        Real base_hz_{};
        Real sensitivity_hz_per_v_{};
        Real amplitude_{1.0};
        Real phase_rad_{};
        Real base_phase_step_{};
        Real cv_phase_gain_{};
        Real out_{};
    };

    // -------------------------------------------------------------------------
    // HarmonicOscillator  —  simple sinusoidal source
    // out(n) = amplitude * sin(2*pi*frequency_hz*n*dt + phase_rad)
    // -------------------------------------------------------------------------
    class HarmonicOscillator : public Element
    {
    public:
        static constexpr bool kIsStateful = true;
        static constexpr bool kHasDirectFeedthrough = false;

        struct Params {
            Real frequency_hz = 1.0;
            Real phase_rad = 0.0;
            Real amplitude = 1.0;
        };

        HarmonicOscillator();
        explicit HarmonicOscillator(Params params);
        HarmonicOscillator(const std::string& id, Params params);

        void calculateParameters() override;

        void tick() override;

    private:
        Real frequency_hz_{};
        Real phase_rad_{};
        Real amplitude_{1.0};
        Real omega_dt_{};
        std::size_t sample_index_ = 0;
        Real out_{};
    };


} // namespace s7

