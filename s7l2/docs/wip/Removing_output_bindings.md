# Removing Redundant output_bindings_

## The Problem

The Element class had redundant data:

```cpp
class Element {
private:
    vector<Port> output_ports_;       // Ports with metadata + value_ptr
    vector<OutputBinding> output_bindings_;  // DUPLICATE! Just pointers
};
```

**Why redundant?**
- `Port` already has `void* value_ptr` pointing to the output storage
- `output_bindings_` was just extracting those same pointers into a separate vector
- **The element itself doesn't care what's connected to its outputs!**

## Before (Redundant)

```cpp
void register_output(size_t idx, string_view name, SignalType type, void* ptr) {
    output_ports_.push_back({idx, name, type, ptr});
    output_bindings_.push_back(ptr);  // Storing the same pointer twice!
}

span<const OutputBinding> outputs() const {
    return output_bindings_;  // Return cached copy
}
```

**Memory waste:**
- Every element stores output pointers twice
- Extra vector allocation
- Unnecessary synchronization between two data structures

## After (Clean)

```cpp
void register_output(size_t idx, string_view name, SignalType type, void* ptr) {
    output_ports_.push_back({idx, name, type, ptr});  // Store once!
}

vector<OutputBinding> outputs() const {
    vector<OutputBinding> result;
    result.reserve(output_ports_.size());
    for (const auto& port : output_ports_) {
        result.push_back(port.value_ptr);  // Extract on-demand
    }
    return result;
}
```

**Benefits:**
- ✅ Store pointers only once (in Port structs)
- ✅ Element class is smaller
- ✅ No synchronization needed
- ✅ Source of truth is clear: Port owns the pointer

## Performance Impact

**Q: Isn't building a vector every time slower?**

**A: No, because `outputs()` is only called during schema compilation!**

### Where outputs() is Called

1. **Compiler** (once per schema compilation)
   ```cpp
   const auto source = element->outputs();
   element_inputs[to][port] = source[from_port];
   ```

2. **Runtime observation/debugging** (rare)
   ```cpp
   const auto output = element->outputs()[port];  // For reading values
   ```

3. **Not in element step()!** - Elements just write directly to member variables:
   ```cpp
   void step() {
       output_value_ = calculation;  // No outputs() call!
   }
   ```

### Actual Cost

- **Compilation time**: Building a small vector once is negligible compared to topology validation
- **Runtime**: Only called for observation/debugging (user-facing, not hot path)
- **Element execution**: Zero impact - elements don't call `outputs()`

## Why Element Doesn't Care About Outputs

**Key insight:** An element is a **local computation**:

```cpp
class Gain : public Element {
    RealValue output_value_;  // I own this
    
    void step() {
        output_value_ = read_input<RealValue>(0) * factor_;
    }
    // I just write to my output. I don't care who reads it!
};
```

The element:
- ✅ Writes to its output member variables
- ❌ Doesn't know who's connected downstream
- ❌ Doesn't manage connections
- ❌ Doesn't need output_bindings_

**Compare to inputs:**
- Element DOES need `input_bindings_` because it reads from EXTERNAL sources
- Element doesn't need `output_bindings_` because it writes to its OWN storage

## Memory Savings Example

For a 100-element graph with average 2 outputs per element:

**Before:**
- 100 elements × 2 outputs × 2 copies (port + binding) × 8 bytes = **3.2 KB**

**After:**
- 100 elements × 2 outputs × 1 copy (only in port) × 8 bytes = **1.6 KB**
- Plus: 16 bytes (Port struct) - 8 bytes (OutputBinding) = **800 bytes saved in Port overhead**

**Total savings: ~1.6 KB per 100 elements** (plus cleaner design!)

## Code Clarity

**Before:** 
"Why do we have two vectors with the same pointers?"

**After:**
"Ports contain the metadata AND the pointer. Simple!"

## Lessons Learned

1. **Question duplication** - If you're storing the same data twice, one is probably wrong
2. **Element = local computation** - It doesn't manage its connections
3. **Hot path vs cold path** - Differentiate between runtime (step) and setup (compilation)
4. **Profile before optimizing** - The "cached" version wasn't actually faster

This refactoring follows the principle: **Store data once, in the right place.**

The right place for output pointers: **In the Port struct**, because that's what represents the connection point!

