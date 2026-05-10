#include "s7stdlib.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace s7 {

ImpulseSource::ImpulseSource() : ImpulseSource("", 1.0) {}

ImpulseSource::ImpulseSource(const Real amplitude) : ImpulseSource("", amplitude) {}

ImpulseSource::ImpulseSource(const std::string& id, const Real amplitude)
	: Element(id), amplitude_(amplitude)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	addOutput<Real>(out_);
}

void ImpulseSource::tick()
{
	out_ = fired_ ? 0.0 : amplitude_;
	fired_ = true;
}

ToComplex::ToComplex() : ToComplex("") {}

ToComplex::ToComplex(const std::string& id) : Element(id)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	re_in_ = addInput<Real>();
	im_in_ = addInput<Real>();
	addOutput<Complex>(out_);
}

void ToComplex::tick()
{
	out_ = Complex{re_in_->value(), im_in_->value()};
}

ToReals::ToReals() : ToReals("") {}

ToReals::ToReals(const std::string& id) : Element(id)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	in_ = addInput<Complex>();
	addOutput<Real>(out_re_);
	addOutput<Real>(out_im_);
}

void ToReals::tick()
{
	const auto v = in_->value();
	out_re_ = std::real(v);
	out_im_ = std::imag(v);
}

Vca::Vca() : Vca("") {}

Vca::Vca(const std::string& id) : Element(id)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	signal_in_ = addInput<Real>();
	gain_in_ = addInput<Real>();
	addOutput<Real>(out_);
}

void Vca::tick()
{
	out_ = signal_in_->value() * gain_in_->value();
}

FIRFilter::FIRFilter(std::vector<Real> b)
	: FIRFilter("", std::move(b))
{
}

FIRFilter::FIRFilter(const std::string& id, std::vector<Real> b)
	: FIRFilter(id, std::move(b), InitState{})
{
}

FIRFilter::FIRFilter(const std::string& id, std::vector<Real> b, InitState init_state)
	: Element(id), b_(std::move(b))
{
	if (b_.empty())
	{
		throw std::invalid_argument("FIRFilter requires at least one coefficient in b");
	}

	setTraits(kIsStateful, kHasDirectFeedthrough);
	in_ = addInput<Real>();
	x_state_.assign(b_.size(), 0.0);
	for (std::size_t i = 0; i < std::min(x_state_.size(), init_state.x.size()); ++i)
	{
		x_state_[i] = init_state.x[i];
	}
	addOutput<Real>(out_);
}

void FIRFilter::tick()
{
	for (std::size_t i = x_state_.size(); i > 1; --i)
	{
		x_state_[i - 1] = x_state_[i - 2];
	}
	x_state_[0] = in_->value();

	Real y = 0.0;
	for (std::size_t i = 0; i < b_.size(); ++i)
	{
		y += b_[i] * x_state_[i];
	}
	out_ = y;
}

IIRFilter::IIRFilter(std::vector<Real> b, std::vector<Real> a)
	: IIRFilter("", std::move(b), std::move(a))
{
}

IIRFilter::IIRFilter(const std::string& id, std::vector<Real> b, std::vector<Real> a)
	: IIRFilter(id, std::move(b), std::move(a), InitState{})
{
}

IIRFilter::IIRFilter(const std::string& id,
					 std::vector<Real> b,
					 std::vector<Real> a,
					 InitState init_state)
	: IIRFilter(id, std::move(b), std::move(a), true)
{
	for (std::size_t i = 0; i < std::min(x_state_.size(), init_state.x.size()); ++i)
	{
		x_state_[i] = init_state.x[i];
	}
	for (std::size_t i = 0; i < std::min(y_state_.size(), init_state.y.size()); ++i)
	{
		y_state_[i] = init_state.y[i];
	}
}

IIRFilter::IIRFilter(const std::string& id,
					 std::vector<Real> b,
					 std::vector<Real> a,
					 bool create_input)
	: Element(id), b_(std::move(b)), a_(std::move(a))
{
	if (b_.empty())
	{
		throw std::invalid_argument("IIRFilter requires at least one coefficient in b");
	}

	setTraits(kIsStateful, kHasDirectFeedthrough);
	if (create_input)
	{
		in_ = addInput<Real>();
	}
	x_state_.assign(b_.size(), 0.0);
	y_state_.assign(a_.size(), 0.0);
	addOutput<Real>(out_);
}

void IIRFilter::tick()
{
	for (std::size_t i = x_state_.size(); i > 1; --i)
	{
		x_state_[i - 1] = x_state_[i - 2];
	}
	if (in_)
	{
		x_state_[0] = in_->value();
	}
	else
	{
		x_state_[0] = 0.0;
	}

	Real y = 0.0;
	for (std::size_t i = 0; i < b_.size(); ++i)
	{
		y += b_[i] * x_state_[i];
	}
	for (std::size_t j = 0; j < a_.size(); ++j)
	{
		y -= a_[j] * y_state_[j];
	}

	for (std::size_t j = y_state_.size(); j > 1; --j)
	{
		y_state_[j - 1] = y_state_[j - 2];
	}
	if (!y_state_.empty())
	{
		y_state_[0] = y;
	}

	out_ = y;
}

DigitalOscillator::DigitalOscillator() : DigitalOscillator("", Params{}) {}

DigitalOscillator::DigitalOscillator(Params params) : DigitalOscillator("", params) {}

DigitalOscillator::DigitalOscillator(const std::string& id, Params params)
	: IIRFilter(id, std::vector<Real>{0.0}, std::vector<Real>{0.0, 0.0}, false),
	  frequency_hz_(params.frequency_hz),
	  amplitude_(params.amplitude),
	  phase_rad_(params.phase_rad),
	  radius_(params.radius)
{
	if (radius_ <= 0.0)
	{
		throw std::invalid_argument("DigitalOscillator radius must be positive");
	}

	setTraits(kIsStateful, kHasDirectFeedthrough);
}

void DigitalOscillator::calculateParameters()
{
	const auto schema = owner.lock();
	assert(schema && "DigitalOscillator::calculateParameters expects element to be inserted into a Schema");

	const Real dt = schema->get_TD();
	omega_ = 2.0 * std::numbers::pi_v<Real> * frequency_hz_ * dt;
	coeff_c1_ = 2.0 * radius_ * std::cos(omega_);
	coeff_c2_ = radius_ * radius_;
	a_[0] = -coeff_c1_;
	a_[1] = coeff_c2_;

	if (!initialized_)
	{
		const Real y_m1 = amplitude_ * std::sin(phase_rad_ - omega_);
		const Real y_m2 = amplitude_ * std::sin(phase_rad_ - (2.0 * omega_));
		y_state_[0] = y_m1;
		y_state_[1] = y_m2;
		initialized_ = true;
	}
}

void DigitalOscillator::tick()
{
	IIRFilter::tick();
}

EnergyDifference::EnergyDifference() : EnergyDifference("") {}

EnergyDifference::EnergyDifference(const std::string& id) : Element(id)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	in_a_ = addInput<Real>();
	in_b_ = addInput<Real>();
	addOutput<Real>(energy_);
}

void EnergyDifference::calculateParameters()
{
	const auto schema = owner.lock();
	assert(schema && "EnergyDifference::calculateParameters expects element to be inserted into a Schema");
	dt_ = schema->get_TD();
}

void EnergyDifference::tick()
{
	const Real diff = in_a_->value() - in_b_->value();
	energy_ += diff * diff * dt_;
}

Vco::Vco() : Vco("", Params{}) {}

Vco::Vco(Params params) : Vco("", params) {}

Vco::Vco(const std::string& id, Params params)
	: Element(id),
	  base_hz_(params.base_hz),
	  sensitivity_hz_per_v_(params.sensitivity_hz_per_v),
	  amplitude_(params.amplitude),
	  phase_rad_(params.initial_phase_rad)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	cv_in_ = addInput<Real>();
	addOutput<Real>(out_);
	out_ = amplitude_ * std::sin(phase_rad_);
}

void Vco::calculateParameters()
{
	const auto schema = owner.lock();
	assert(schema && "Vco::calculateParameters expects element to be inserted into a Schema");

	const Real dt = schema->get_TD();
	base_phase_step_ = 2.0 * std::numbers::pi_v<Real> * base_hz_ * dt;
	cv_phase_gain_ = 2.0 * std::numbers::pi_v<Real> * sensitivity_hz_per_v_ * dt;
}

void Vco::tick()
{
	const Real cv = cv_in_->value();
	const Real dphi = base_phase_step_ + cv_phase_gain_ * cv;
	phase_rad_ += dphi;
	out_ = amplitude_ * std::sin(phase_rad_);
}

HarmonicOscillator::HarmonicOscillator() : HarmonicOscillator("", Params{}) {}

HarmonicOscillator::HarmonicOscillator(Params params) : HarmonicOscillator("", params) {}

HarmonicOscillator::HarmonicOscillator(const std::string& id, Params params)
	: Element(id),
	  frequency_hz_(params.frequency_hz),
	  phase_rad_(params.phase_rad),
	  amplitude_(params.amplitude)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	addOutput<Real>(out_);
	out_ = amplitude_ * std::sin(phase_rad_);
}

void HarmonicOscillator::calculateParameters()
{
	const auto schema = owner.lock();
	assert(schema && "HarmonicOscillator::calculateParameters expects element to be inserted into a Schema");

	const Real dt = schema->get_TD();
	omega_dt_ = 2.0 * std::numbers::pi_v<Real> * frequency_hz_ * dt;
}


void HarmonicOscillator::tick()
{
	const Real sample_n = static_cast<Real>(sample_index_);
	const Real angle = (omega_dt_ * sample_n) + phase_rad_;
	out_ = amplitude_ * std::sin(angle);
	++sample_index_;
}

} // namespace s7

