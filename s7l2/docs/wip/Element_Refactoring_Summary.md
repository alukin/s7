# Element Refactoring Summary

## Key Principle: Ports Have Types, Not Elements

The S7 element system has been refactored to correctly reflect that **port types are fixed properties of element types**, not runtime configuration. Elements with identical logic but different signal types are implemented as **class templates**.

## Design Principles

1. **Elements are computation units** - they don't have a "type" (real vs complex)
2. **Ports have types** - each input/output port has a fixed `SignalType` (DOUBLE, COMPLEX, INT12, etc.)
3. **Port types are static** - defined at compile time in each element's constructor, never changed at runtime
4. **Templates eliminate duplication** - generic elements (Constant, Gain, Adder, Delay) are implemented once as templates
5. **Type conversion is explicit** - use converter elements like `ComplexToReIm` or `ComplexToMagnitudePhase`

## Architecture

### Single Element Base Class

```cpp
class Element {
    // Unified base - no separate ComplexElement hierarchy
    // Ports defined with their types in constructor
public:
    // Type-safe read/write (public for use by templates)
    RealValue read_real(size_t port_idx);
    void write_real(size_t port_idx, RealValue value);
    ComplexValue read_complex(size_t port_idx);
    void write_complex(size_t port_idx, ComplexValue value);
    
protected:
    // Simple constructor for all-DOUBLE ports
    Element(string_view type_name, size_t num_inputs, size_t num_outputs, bool has_state);
    
    // Explicit constructor with port type specification
    Element(string_view type_name, vector<Port> inputs, vector<Port> outputs, bool has_state);
};
```

### Type Traits for Templates

```cpp
template <typename T>
struct SignalTypeTraits;

template <>
struct SignalTypeTraits<RealValue> {
    static constexpr SignalType type = SignalType::DOUBLE;
    static RealValue read(const Element* elem, size_t port);
    static void write(Element* elem, size_t port, RealValue value);
};

template <>
struct SignalTypeTraits<ComplexValue> {
    static constexpr SignalType type = SignalType::COMPLEX;
    static ComplexValue read(const Element* elem, size_t port);
    static void write(Element* elem, size_t port, ComplexValue value);
};
```

### Templated Elements

Elements with identical logic for different types are implemented as templates:

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

Similarly for:
- **ConstantT<T>** → `Constant`, `ComplexConstant`
- **GainT<T>** → `Gain`, `ComplexGain`
- **AdderT<T>** → `Adder`, `ComplexAdder`
- **DelayT<T>** → `Delay`, `ComplexDelay` (NEW!)

### Example Elements

#### Using Template (Generic)
```cpp
// Single implementation works for both real and complex
template <typename T>
class GainT : public Element {
public:
    GainT(T factor) 
        : Element("Gain",
                  {{0, "in", SignalTypeTraits<T>::type}},
                  {{0, "out", SignalTypeTraits<T>::type}}),
          factor_(factor) {}
    
    void step() override {
        SignalTypeTraits<T>::write(this, 0, 
            SignalTypeTraits<T>::read(this, 0) * factor_);
    }
private:
    T factor_;
};

// Usage
auto real_gain = std::make_unique<Gain>(2.0);
auto complex_gain = std::make_unique<ComplexGain>(ComplexValue{2.0, 0.0});
```

#### Type-Specific Element
```cpp
class ComplexToReIm : public Element {
public:
    ComplexToReIm()
        : Element("ComplexToReIm",
                  {{0, "in", SignalType::COMPLEX}},
                  {{0, "re", SignalType::DOUBLE}, {1, "im", SignalType::DOUBLE}}) {}
    
    void step() override {
        ComplexValue c = read_complex(0);
        write_real(0, c.real());
        write_real(1, c.imag());
    }
};
```

## Element Catalog

### Templated Elements (in header)
- `ConstantT<T>` → `Constant`, `ComplexConstant`
- `GainT<T>` → `Gain`, `ComplexGain`
- `AdderT<T>` → `Adder`, `ComplexAdder`
- `DelayT<T>` → `Delay`, `ComplexDelay`

### Real-Valued Specific (in .cpp)
- `FirFilter` - FIR filter with taps
- `IirFilter` - 1st order IIR filter
- `PreviousValue` - Unit delay for testing

### Type Converters (in .cpp)
- `ReImToComplex` - 2 DOUBLE → 1 COMPLEX
- `ComplexToReIm` - 1 COMPLEX → 2 DOUBLE
- `ComplexToMagnitudePhase` - 1 COMPLEX → 2 DOUBLE (magnitude, phase)
- `Int12ToDouble` - ADC simulation
- `DoubleToInt12` - DAC simulation

## Benefits

1. **No code duplication** - Templates eliminate repeated implementations
2. **Type safety** - Compile-time checking via SignalTypeTraits
3. **Extensible** - Easy to add new signal types (e.g., float, int16)
4. **Clear separation** - Template elements in header, type-specific in .cpp
5. **Efficient storage** - Only allocates space for types actually used

## New Feature: ComplexDelay

The templated approach automatically gives us `ComplexDelay`:

```cpp
using ComplexDelay = DelayT<ComplexValue>;

// Usage
auto delay = std::make_unique<ComplexDelay>(ComplexValue{0.0, 0.0});
```

## Migration Notes

- All existing code continues to work - type aliases preserve naming
- `Constant`, `Gain`, `Adder`, `Delay` are now aliases to templates
- Add `ComplexDelay` for complex-valued delay elements
- Template implementations are in header file for inlining

## Testing

All existing tests pass, including:
- Basic element operations (Constant, Gain, Adder)
- Complex elements (ComplexGain, ComplexAdder)
- Mixed-type converters (ComplexToMagnitudePhase, Int12ToDouble)
- Stateful elements (Delay, FirFilter, IirFilter)
- Feedback loops with memory elements
- Full schema compilation and runtime execution

