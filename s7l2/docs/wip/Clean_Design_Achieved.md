# Clean Element Design - Best of s7pp + Modern C++

## What We Took from s7pp

### ✅ Element Owns Storage
```cpp
class Gain : public Element {
    RealValue factor_;
    RealValue output_value_;  // Element owns this!
    
    Gain() {
        register_output(0, "out", SignalType::DOUBLE, &output_value_);
    }
    
    void step() {
        output_value_ = read_input<RealValue>(0) * factor_;  // Direct assignment!
    }
};
```

Like s7pp's `TypedOutput<T>` holding `T& val_`, our Ports reference the element's owned storage.

### ✅ No Separate Storage Arrays
**Before (Ugly):**
```cpp
class Element {
    vector<Port> outputs_;
    vector<RealValue> real_outputs_;      // DUPLICATE!
    vector<ComplexValue> complex_outputs_; // DUPLICATE!
    
    void write_output(size_t idx, RealValue val) {
        // Map idx to real_outputs_ index... UGLY!
    }
};
```

**After (Clean - s7pp inspired):**
```cpp
class Element {
    // Base class only has port METADATA
    vector<Port> output_ports_;  // Just references
    // Storage owned by subclass!
};

template <typename T>
class GainT : public Element {
    T output_value_;  // Subclass owns storage
    
    GainT() {
        register_output(0, "out", signal_type_for<T>(), &output_value_);
    }
    
    void step() {
        output_value_ = ...;  // Direct! No mapping!
    }
};
```

### ✅ Direct Assignment (No write_real/write_complex)
**Before (Ugly):**
```cpp
void step() override {
    write_real(0, value);  // What is this function doing?
}
```

**After (Clean):**
```cpp
void step() override {
    output_value_ = value;  // Direct member assignment!
}
```

## What We Improved Over s7pp

### ✅ No `shared_ptr` Overhead
**s7pp:**
```cpp
vector<shared_ptr<Output>> outputs;  // Heap allocation per port!
```

**Us:**
```cpp
struct Port {
    void* value_ptr;  // Just a pointer
};
```

### ✅ No RTTI / `dynamic_cast`
**s7pp:**
```cpp
auto typed = dynamic_pointer_cast<TypedOutput<T>>(output); // Runtime check!
if (!typed) throw runtime_error("Type mismatch");
```

**Us:**
```cpp
SignalType type;  // Static enum - compile-time safe!
```

### ✅ Templates Work Cleanly
**s7pp:** Hard to template because of Pin hierarchy

**Us:**
```cpp
template <typename T>
class ConstantT : public Element {
    T output_value_;
    // Just works!
};
```

## Complete Example: Before vs After

### BEFORE (Ugly - my mistake!)
```cpp
class Gain : public Element {
    Gain() : Element("Gain", 1, 1) {}
    
    void step() {
        write_real(0, read_real(0) * 2.0);  // Ugly helper functions!
    }
};

// Where is the storage? Hidden in Element base class!
// separate real_outputs_ and complex_outputs_ arrays - YUCK!
```

### AFTER (Clean - s7pp inspired!)
```cpp
template <typename T>
class GainT : public Element {
    T factor_;
    T output_value_;  // I own this!
    
    GainT(T factor) 
        : Element("Gain"),
          factor_(factor),
          output_value_{}
    {
        register_input(0, "in", signal_type_for<T>(), nullptr);
        register_output(0, "out", signal_type_for<T>(), &output_value_);
    }
    
    void step() {
        output_value_ = read_input<T>(0) * factor_;  // Clear and direct!
    }
};

using Gain = GainT<RealValue>;
using ComplexGain = GainT<ComplexValue>;
```

## Benefits Summary

| Aspect | s7pp | Old Attempt | New (Best of Both) |
|--------|------|-------------|-------------------|
| **Element owns storage** | ✅ | ❌ (base class) | ✅ |
| **Direct assignment** | ✅ | ❌ (write_real) | ✅ |
| **No `shared_ptr` overhead** | ❌ | ✅ | ✅ |
| **No RTTI** | ❌ | ✅ | ✅ |
| **Templates** | ❌ | ✅ | ✅ |
| **Code clarity** | ✅ | ❌ | ✅ |

## What Makes It Clean

1. **No helper functions** - Just assign to members: `output_value_ = ...`
2. **No mapping logic** - Ports directly reference member variables
3. **No duplicate storage** - Only one `output_value_` per output
4. **Clear ownership** - Element owns its data, Port just references it
5. **Templates work** - Same code for Real and Complex

This is the **simplicity of s7pp** with the **efficiency and type safety of modern C++**!

