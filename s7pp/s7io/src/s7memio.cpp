//
// s7memio.cpp — Shared-memory stream transport (SPSC)
//

#include "s7memio.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace s7 {
namespace {

static constexpr uint32_t kMemMagic = 0x53374d4d; // "S7MM"
static constexpr uint32_t kMemVersion = 2;

struct MemRingHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t num_channels;
	uint32_t block_size;
	uint32_t capacity_blocks;
	float sample_rate_hz;
	uint32_t domain;
	uint32_t encoding;

	std::atomic<uint64_t> write_seq;
	std::atomic<uint64_t> read_seq;

	std::atomic<uint64_t> overflow_count;
	std::atomic<uint64_t> underrun_count;

	std::atomic<uint32_t> ready;
	uint32_t reserved;
};

class IScalarCodec {
public:
	virtual ~IScalarCodec() = default;
	virtual std::size_t bytesPerScalar() const = 0;
	virtual void encode(double value, uint8_t* dst) const = 0;
	virtual double decode(const uint8_t* src) const = 0;
};

class F32Codec final : public IScalarCodec {
public:
	std::size_t bytesPerScalar() const override { return sizeof(float); }
	void encode(double value, uint8_t* dst) const override {
		const float v = static_cast<float>(value);
		std::memcpy(dst, &v, sizeof(float));
	}
	double decode(const uint8_t* src) const override {
		float v;
		std::memcpy(&v, src, sizeof(float));
		return static_cast<double>(v);
	}
};

template <typename TInt>
class IntCodec final : public IScalarCodec {
public:
	std::size_t bytesPerScalar() const override { return sizeof(TInt); }
	void encode(double value, uint8_t* dst) const override {
		constexpr double lo = static_cast<double>(std::numeric_limits<TInt>::lowest());
		constexpr double hi = static_cast<double>(std::numeric_limits<TInt>::max());
		const auto clamped = std::clamp(value, lo, hi);
		const TInt v = static_cast<TInt>(std::llround(clamped));
		std::memcpy(dst, &v, sizeof(TInt));
	}
	double decode(const uint8_t* src) const override {
		TInt v{};
		std::memcpy(&v, src, sizeof(TInt));
		return static_cast<double>(v);
	}
};

template <typename TUInt>
class UIntCodec final : public IScalarCodec {
public:
	std::size_t bytesPerScalar() const override { return sizeof(TUInt); }
	void encode(double value, uint8_t* dst) const override {
		constexpr double lo = 0.0;
		constexpr double hi = static_cast<double>(std::numeric_limits<TUInt>::max());
		const auto clamped = std::clamp(value, lo, hi);
		const TUInt v = static_cast<TUInt>(std::llround(clamped));
		std::memcpy(dst, &v, sizeof(TUInt));
	}
	double decode(const uint8_t* src) const override {
		TUInt v{};
		std::memcpy(&v, src, sizeof(TUInt));
		return static_cast<double>(v);
	}
};

auto makeCodec(MemScalarEncoding encoding) -> std::unique_ptr<IScalarCodec>
{
	switch (encoding) {
		case MemScalarEncoding::F32: return std::make_unique<F32Codec>();
		case MemScalarEncoding::S16: return std::make_unique<IntCodec<int16_t>>();
		case MemScalarEncoding::S8:  return std::make_unique<IntCodec<int8_t>>();
		case MemScalarEncoding::U8:  return std::make_unique<UIntCodec<uint8_t>>();
	}
	throw std::runtime_error("s7memio: unsupported encoding");
}

auto scalarCount(MemSignalDomain domain) -> std::size_t
{
	return (domain == MemSignalDomain::COMPLEX) ? 2u : 1u;
}

class ShmRegion {
public:
	ShmRegion(std::string name, std::size_t total_size, bool create_if_missing, bool unlink_on_destroy)
		: name_(std::move(name)), size_(total_size), unlink_on_destroy_(unlink_on_destroy)
	{
		const int create_flags = O_RDWR | (create_if_missing ? O_CREAT : 0);
		fd_ = ::shm_open(name_.c_str(), create_flags, 0660);
		if (fd_ < 0) {
			throw std::runtime_error("s7memio: shm_open failed for " + name_ + ": " + std::strerror(errno));
		}

		if (create_if_missing) {
			if (::ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
				const auto err = std::string(std::strerror(errno));
				::close(fd_);
				throw std::runtime_error("s7memio: ftruncate failed: " + err);
			}
		}

		mapped_ = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
		if (mapped_ == MAP_FAILED) {
			const auto err = std::string(std::strerror(errno));
			::close(fd_);
			throw std::runtime_error("s7memio: mmap failed: " + err);
		}
	}

	~ShmRegion()
	{
		if (mapped_ && mapped_ != MAP_FAILED) {
			::msync(mapped_, size_, MS_SYNC);
			::munmap(mapped_, size_);
		}
		if (fd_ >= 0) {
			::close(fd_);
		}
		if (unlink_on_destroy_) {
			::shm_unlink(name_.c_str());
		}
	}

	void* mapped() const { return mapped_; }

private:
	std::string name_;
	std::size_t size_{};
	bool unlink_on_destroy_{};
	int fd_ = -1;
	void* mapped_ = nullptr;
};

auto blockBytes(const MemIOConfig& cfg, MemSignalDomain domain, std::size_t scalar_bytes) -> std::size_t
{
	return static_cast<std::size_t>(cfg.num_channels) * cfg.block_size * scalarCount(domain) * scalar_bytes;
}

auto computeTotalBytes(const MemIOConfig& cfg, MemSignalDomain domain, std::size_t scalar_bytes) -> std::size_t
{
	return sizeof(MemRingHeader) + blockBytes(cfg, domain, scalar_bytes) * cfg.capacity_blocks;
}

class RingSinkImpl {
public:
	RingSinkImpl(const MemIOConfig& cfg, MemSignalDomain domain)
		: cfg_(cfg), domain_(domain), codec_(makeCodec(cfg.encoding)),
		  region_(cfg.shm_name,
				  computeTotalBytes(cfg, domain, codec_->bytesPerScalar()),
				  cfg.create_if_missing,
				  cfg.unlink_on_destroy)
	{
		header_ = reinterpret_cast<MemRingHeader*>(region_.mapped());

		if (header_->magic != kMemMagic || header_->version != kMemVersion) {
			std::memset(header_, 0, sizeof(MemRingHeader));
			header_->magic = kMemMagic;
			header_->version = kMemVersion;
			header_->num_channels = cfg.num_channels;
			header_->block_size = cfg.block_size;
			header_->capacity_blocks = cfg.capacity_blocks;
			header_->sample_rate_hz = static_cast<float>(cfg.sample_rate_hz);
			header_->domain = static_cast<uint32_t>(domain_);
			header_->encoding = static_cast<uint32_t>(cfg.encoding);
			header_->write_seq.store(0, std::memory_order_relaxed);
			header_->read_seq.store(0, std::memory_order_relaxed);
			header_->overflow_count.store(0, std::memory_order_relaxed);
			header_->underrun_count.store(0, std::memory_order_relaxed);
			header_->ready.store(1, std::memory_order_release);
		} else {
			validateShape();
			header_->ready.store(1, std::memory_order_release);
		}

		payload_ = reinterpret_cast<uint8_t*>(region_.mapped()) + sizeof(MemRingHeader);
		block_bytes_ = blockBytes(cfg_, domain_, codec_->bytesPerScalar());
	}

	auto codec() const -> const IScalarCodec& { return *codec_; }

	auto pushBlock(const uint8_t* block) -> bool
	{
		const uint64_t w = header_->write_seq.load(std::memory_order_acquire);
		const uint64_t r = header_->read_seq.load(std::memory_order_acquire);
		const uint64_t used = w - r;
		if (used >= cfg_.capacity_blocks) {
			header_->read_seq.fetch_add(1, std::memory_order_release);
			header_->overflow_count.fetch_add(1, std::memory_order_relaxed);
		}

		const uint32_t slot = static_cast<uint32_t>(w % cfg_.capacity_blocks);
		std::memcpy(payload_ + static_cast<std::size_t>(slot) * block_bytes_, block, block_bytes_);

		std::atomic_thread_fence(std::memory_order_release);
		header_->write_seq.store(w + 1, std::memory_order_release);
		return used < cfg_.capacity_blocks;
	}

private:
	void validateShape() const
	{
		if (header_->num_channels != cfg_.num_channels ||
			header_->block_size != cfg_.block_size ||
			header_->capacity_blocks != cfg_.capacity_blocks ||
			header_->domain != static_cast<uint32_t>(domain_) ||
			header_->encoding != static_cast<uint32_t>(cfg_.encoding)) {
			throw std::runtime_error("s7memio: shared segment shape/domain/encoding mismatch");
		}
	}

public:
	MemIOConfig cfg_;
	MemSignalDomain domain_;
	std::unique_ptr<IScalarCodec> codec_;
	ShmRegion region_;
	MemRingHeader* header_ = nullptr;
	uint8_t* payload_ = nullptr;
	std::size_t block_bytes_ = 0;
};

class RingSourceImpl {
public:
	RingSourceImpl(const MemIOConfig& cfg, MemSignalDomain expected_domain)
		: cfg_(cfg), expected_domain_(expected_domain),
		  region_(cfg.shm_name,
				  computeTotalBytes(cfg, expected_domain, makeCodec(cfg.encoding)->bytesPerScalar()),
				  cfg.create_if_missing,
				  cfg.unlink_on_destroy)
	{
		header_ = reinterpret_cast<MemRingHeader*>(region_.mapped());
		if (header_->magic != kMemMagic || header_->version != kMemVersion)
			throw std::runtime_error("s7memio: segment is not initialized by a valid producer");

		if (header_->domain != static_cast<uint32_t>(expected_domain_))
			throw std::runtime_error("s7memio: domain mismatch between source and segment");

		if (header_->encoding > static_cast<uint32_t>(MemScalarEncoding::U8))
			throw std::runtime_error("s7memio: invalid encoding in segment header");

		cfg_.encoding = static_cast<MemScalarEncoding>(header_->encoding);
		codec_ = makeCodec(cfg_.encoding);
		validateShape();

		payload_ = reinterpret_cast<uint8_t*>(region_.mapped()) + sizeof(MemRingHeader);
		block_bytes_ = blockBytes(cfg_, expected_domain_, codec_->bytesPerScalar());
	}

	auto codec() const -> const IScalarCodec& { return *codec_; }

	auto pullBlock(std::vector<uint8_t>& dst) -> bool
	{
		const uint64_t r = header_->read_seq.load(std::memory_order_acquire);
		const uint64_t w = header_->write_seq.load(std::memory_order_acquire);

		if (w == r) {
			header_->underrun_count.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		const uint32_t slot = static_cast<uint32_t>(r % cfg_.capacity_blocks);
		dst.resize(block_bytes_);
		std::memcpy(dst.data(), payload_ + static_cast<std::size_t>(slot) * block_bytes_, block_bytes_);

		std::atomic_thread_fence(std::memory_order_acquire);
		header_->read_seq.store(r + 1, std::memory_order_release);
		return true;
	}

private:
	void validateShape() const
	{
		if (header_->num_channels != cfg_.num_channels ||
			header_->block_size != cfg_.block_size ||
			header_->capacity_blocks != cfg_.capacity_blocks ||
			header_->domain != static_cast<uint32_t>(expected_domain_)) {
			throw std::runtime_error("s7memio: shared segment shape/domain mismatch");
		}
	}

public:
	MemIOConfig cfg_;
	MemSignalDomain expected_domain_;
	ShmRegion region_;
	MemRingHeader* header_ = nullptr;
	uint8_t* payload_ = nullptr;
	std::unique_ptr<IScalarCodec> codec_;
	std::size_t block_bytes_ = 0;
};

} // namespace

class RealMemSink::Impl : public RingSinkImpl {
public:
	explicit Impl(const MemIOConfig& cfg) : RingSinkImpl(cfg, MemSignalDomain::REAL) {}
};

class RealMemSource::Impl : public RingSourceImpl {
public:
	explicit Impl(const MemIOConfig& cfg) : RingSourceImpl(cfg, MemSignalDomain::REAL) {}
};

class ComplexMemSink::Impl : public RingSinkImpl {
public:
	explicit Impl(const MemIOConfig& cfg) : RingSinkImpl(cfg, MemSignalDomain::COMPLEX) {}
};

class ComplexMemSource::Impl : public RingSourceImpl {
public:
	explicit Impl(const MemIOConfig& cfg) : RingSourceImpl(cfg, MemSignalDomain::COMPLEX) {}
};

RealMemSink::RealMemSink(const std::string& id, const MemIOConfig& cfg)
	: Element(id), impl_(std::make_unique<Impl>(cfg))
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	inputs_.reserve(cfg.num_channels);
	for (uint32_t i = 0; i < cfg.num_channels; ++i) inputs_.push_back(addInput<Real>());
	staging_.assign(blockBytes(cfg, MemSignalDomain::REAL, impl_->codec().bytesPerScalar()), 0);
}

RealMemSink::~RealMemSink() = default;

void RealMemSink::tick()
{
	const auto channels = impl_->cfg_.num_channels;
	const auto block_size = impl_->cfg_.block_size;
	const auto bps = impl_->codec().bytesPerScalar();
	const auto sample_offset = static_cast<std::size_t>(staging_pos_) * channels * bps;

	for (uint32_t ch = 0; ch < channels; ++ch) {
		impl_->codec().encode(inputs_[ch]->value(), staging_.data() + sample_offset + static_cast<std::size_t>(ch) * bps);
	}

	++staging_pos_;
	if (staging_pos_ >= block_size) {
		const bool not_dropped = impl_->pushBlock(staging_.data());
		++pushed_blocks_;
		if (!not_dropped) ++dropped_blocks_;
		staging_pos_ = 0;
	}
}

RealMemSource::RealMemSource(const std::string& id, const MemIOConfig& cfg)
	: Element(id), impl_(std::make_unique<Impl>(cfg)), cfg_(cfg)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	outputs_values_.assign(cfg.num_channels, 0.0);
	for (uint32_t i = 0; i < cfg.num_channels; ++i) addOutput<Real>(outputs_values_[i]);
	block_buffer_.assign(blockBytes(cfg, MemSignalDomain::REAL, impl_->codec().bytesPerScalar()), 0);
	block_pos_ = cfg.block_size;
}

RealMemSource::~RealMemSource() = default;

void RealMemSource::tick()
{
	if (block_pos_ >= cfg_.block_size) {
		if (!impl_->pullBlock(block_buffer_)) {
			++underruns_;
			if (cfg_.zero_on_underrun) std::fill(outputs_values_.begin(), outputs_values_.end(), 0.0);
			return;
		}
		++pulled_blocks_;
		block_pos_ = 0;
	}

	const auto channels = cfg_.num_channels;
	const auto bps = impl_->codec().bytesPerScalar();
	const auto sample_offset = static_cast<std::size_t>(block_pos_) * channels * bps;
	for (uint32_t ch = 0; ch < channels; ++ch) {
		outputs_values_[ch] = impl_->codec().decode(block_buffer_.data() + sample_offset + static_cast<std::size_t>(ch) * bps);
	}
	++block_pos_;
}

ComplexMemSink::ComplexMemSink(const std::string& id, const MemIOConfig& cfg)
	: Element(id), impl_(std::make_unique<Impl>(cfg))
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	inputs_.reserve(cfg.num_channels);
	for (uint32_t i = 0; i < cfg.num_channels; ++i) inputs_.push_back(addInput<Complex>());
	staging_.assign(blockBytes(cfg, MemSignalDomain::COMPLEX, impl_->codec().bytesPerScalar()), 0);
}

ComplexMemSink::~ComplexMemSink() = default;

void ComplexMemSink::tick()
{
	const auto channels = impl_->cfg_.num_channels;
	const auto block_size = impl_->cfg_.block_size;
	const auto bps = impl_->codec().bytesPerScalar();
	const auto sample_offset = static_cast<std::size_t>(staging_pos_) * channels * 2u * bps;

	for (uint32_t ch = 0; ch < channels; ++ch) {
		const auto base = staging_.data() + sample_offset + static_cast<std::size_t>(ch) * 2u * bps;
		const auto v = inputs_[ch]->value();
		impl_->codec().encode(v.real(), base);
		impl_->codec().encode(v.imag(), base + bps);
	}

	++staging_pos_;
	if (staging_pos_ >= block_size) {
		const bool not_dropped = impl_->pushBlock(staging_.data());
		++pushed_blocks_;
		if (!not_dropped) ++dropped_blocks_;
		staging_pos_ = 0;
	}
}

ComplexMemSource::ComplexMemSource(const std::string& id, const MemIOConfig& cfg)
	: Element(id), impl_(std::make_unique<Impl>(cfg)), cfg_(cfg)
{
	setTraits(kIsStateful, kHasDirectFeedthrough);
	outputs_values_.assign(cfg.num_channels, Complex{0.0, 0.0});
	for (uint32_t i = 0; i < cfg.num_channels; ++i) addOutput<Complex>(outputs_values_[i]);
	block_buffer_.assign(blockBytes(cfg, MemSignalDomain::COMPLEX, impl_->codec().bytesPerScalar()), 0);
	block_pos_ = cfg.block_size;
}

ComplexMemSource::~ComplexMemSource() = default;

void ComplexMemSource::tick()
{
	if (block_pos_ >= cfg_.block_size) {
		if (!impl_->pullBlock(block_buffer_)) {
			++underruns_;
			if (cfg_.zero_on_underrun) std::fill(outputs_values_.begin(), outputs_values_.end(), Complex{0.0, 0.0});
			return;
		}
		++pulled_blocks_;
		block_pos_ = 0;
	}

	const auto channels = cfg_.num_channels;
	const auto bps = impl_->codec().bytesPerScalar();
	const auto sample_offset = static_cast<std::size_t>(block_pos_) * channels * 2u * bps;
	for (uint32_t ch = 0; ch < channels; ++ch) {
		const auto base = block_buffer_.data() + sample_offset + static_cast<std::size_t>(ch) * 2u * bps;
		const double i = impl_->codec().decode(base);
		const double q = impl_->codec().decode(base + bps);
		outputs_values_[ch] = Complex{i, q};
	}
	++block_pos_;
}

} // namespace s7


