# Template-Based Element Design

## Overview

The S7 element system now uses **C++ templates** to eliminate code duplication for elements that have identical logic but work with different signal types (real vs complex).

## Before: Code Duplication

Previously, we had separate implementations for real and complex variants:

```cpp
// Real version
class Constant : public Element {
public:
    explicit Constant(RealValue value);
    void step() override;
private:
    RealValue value_;
};

void Constant::step() {
    write_real(0, value_);
}

// Complex version - DUPLICATED LOGIC!
class ComplexConstant : public Element {
public:
    explicit ComplexConstant(ComplexValue value);
    void step() override;
private:
    ComplexValue value_;
};

void ComplexConstant::step() {
    write_complex(0, value_);
}
```

Problems:
- ❌ Duplicate code for each element type
- ❌ Maintenance burden (fix bugs in two places)
- ❌ No `ComplexDelay` (would need another duplicate)
- ❌ Adding new signal types requires full duplication

## After: Clean Templates

Now we have a single template implementation:

```cpp
template <typename T>
class ConstantT final : public Element {
public:
    explicit ConstantT(T value)
        : Element("Constant", {}, {{0, "out", SignalTypeTraits<T>::type}}),
          value_(value) {}

    void step() override {
        SignalTypeTraits<T>::write(this, 0, value_);
    }

private:
    T value_;
};

// Type aliases for convenience
using Constant = ConstantT<RealValue>;
using ComplexConstant = ConstantT<ComplexValue>;
```

Benefits:
- ✅ Single implementation for all types
- ✅ Fix once, works everywhere
- ✅ Instant `ComplexDelay` support
- ✅ Easy to add new types (float, int16, etc.)

## Type Traits System

The `SignalTypeTraits` template maps C++ types to SignalType and provides type-safe I/O:

```cpp
template <typename T>
struct SignalTypeTraits;

template <>
struct SignalTypeTraits<RealValue> {
    static constexpr SignalType type = SignalType::DOUBLE;
    
    static RealValue read(const Element* elem, std::size_t port) {
        return elem->read_real(port);
    }
    
    static void write(Element* elem, std::size_t port, RealValue value) {
        elem->write_real(port, value);
    }
};

template <>
struct SignalTypeTraits<ComplexValue> {
    static constexpr SignalType type = SignalType::COMPLEX;
    
    static ComplexValue read(const Element* elem, std::size_t port) {
        return elem->read_complex(port);
    }
    
    static void write(Element* elem, std::size_t port, ComplexValue value) {
        elem->write_complex(port, value);
    }
};
```

## Templated Elements

### Currently Implemented

| Template | Real Alias | Complex Alias |  
|----------|------------|---------------|
| `ConstantT<T>` | `Constant` | `ComplexConstant` |
| `GainT<T>` | `Gain` | `ComplexGain` |
| `AdderT<T>` | `Adder` | `ComplexAdder` |
| `DelayT<T>` | `Delay` | `ComplexDelay` |

### Usage Examples

```cpp
// Using type aliases (recommended)
auto real_gain = std::make_unique<Gain>(2.0);
auto complex_gain = std::make_unique<ComplexGain>(ComplexValue{2.0, 0.0});

// Using templates directly (also valid)
auto real_adder = std::make_unique<AdderT<RealValue>>();
auto complex_adder = std::make_unique<AdderT<ComplexValue>>();

// NEW: ComplexDelay now available!
auto complex_delay = std::make_unique<ComplexDelay>(ComplexValue{0.0, 0.0});
```

## Type-Specific Elements

Not all elements are templated. Type-specific elements remain as regular classes:

**Real-only elements:**
- `FirFilter` - tap delay line structure
- `IirFilter` - recursive filter with feedback
- `PreviousValue` - testing utility

**Type converters:**
- `ReImToComplex` - 2 DOUBLE → 1 COMPLEX
- `ComplexToReIm` - 1 COMPLEX → 2 DOUBLE
- `ComplexToMagnitudePhase` - 1 COMPLEX → 2 DOUBLE
- `Int12ToDouble`, `DoubleToInt12` - ADC/DAC simulation

These elements have type-specific logic that can't be generalized.

## Extending with New Signal Types

Adding support for a new signal type (e.g., `float`) is straightforward:

```cpp
// 1. Define the type
using FloatValue = float;

// 2. Add SignalType enum value
enum class SignalType {
    // ...existing...
    FLOAT,
};

// 3. Specialize SignalTypeTraits
template <>
struct SignalTypeTraits<FloatValue> {
    static constexpr SignalType type = SignalType::FLOAT;
    static FloatValue read(const Element* elem, std::size_t port) {
        return elem->read_float(port);  // Add to Element class
    }
    static void write(Element* elem, std::size_t port, FloatValue value) {
        elem->write_float(port, value);  // Add to Element class
    }
};

// 4. Use existing templates!
using FloatConstant = ConstantT<FloatValue>;
using FloatGain = GainT<FloatValue>;
using FloatDelay = DelayT<FloatValue>;
```

## Implementation Details

### Header vs. Source

- **Header file (`elements.hpp`)**: Template definitions (must be in header)
- **Source file (`elements.cpp`)**: Non-templated, type-specific elements

### Why read_real/read_complex are Public

Templates need access to Element's read/write methods, so they're `public`:

```cpp
class Element {
public:
    // Public for use by templated elements
    RealValue read_real(std::size_t port_idx);
    void write_real(std::size_t port_idx, RealValue value);
    ComplexValue read_complex(std::size_t port_idx);
    void write_complex(std::size_t port_idx, ComplexValue value);
    // ...
};
```

Non-templated elements can still call these directly if needed.

## Code Metrics

### Lines of Code Reduction

| Element | Before (Real + Complex) | After (Template) | Reduction |
|---------|-------------------------|------------------|-----------|
| Constant | 24 lines | 12 lines | 50% |
| Gain | 24 lines | 12 lines | 50% |
| Adder | 18 lines | 12 lines  | 33% |
| Delay | 28 lines | 16 lines | 43% |
| **Total** | **94 lines** | **52 lines** | **45%** |

Plus we get `ComplexDelay` for free!

## Summary

The template-based design:

✅ **Eliminates duplication** - Single implementation per algorithm  
✅ **Type-safe** - Compiler enforces correctness  
✅ **Extensible** - Easy to add new signal types  
✅ **Maintainable** - Fix once, benefit everywhere  
✅ **Bonus features** - ComplexDelay comes automatically  
✅ **Clean API** - Type aliases preserve familiar names  
✅ **Zero runtime cost** - Templates compile to efficient code  

See `examples/template_demo.cpp` for a working demonstration.

