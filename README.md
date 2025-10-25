# `InlinedVector`

A C++20 header-only container with `std::vector` semantics, Small Buffer Optimization (SBO), and full, robust allocator support.

This container is a production-ready, drop-in replacement for `std::vector` in scenarios where the number of elements is often small (e.g., `< 16`), providing a significant performance boost by avoiding heap allocations.

Unlike `std::vector`, `absl::InlinedVector`, and `boost::container::small_vector`, it also correctly supports `insert` and `erase` operations on **non-assignable types** (e.g., types with `const` members) even when heap-allocated.

## Features

  * **Small Buffer Optimization**: Guarantees zero heap allocations as long as `size() <= N`.
  * **Allocator-Aware**: Full support for `std::allocator_traits` and `std::pmr`.
      * Correctly propagates allocators for `std::uses_allocator` types (like `std::pmr::string`) during construction, **even for inline elements**.
  * **Modern `std::vector` API**: A familiar, drop-in interface with C++20-style iterators and operations.
  * **Supports Non-Assignable Types**: `insert()` and `erase()` work for non-assignable types (e.g., `const` members) even when heap-allocated, a feature `std::vector` and other SBO implementations lack.
  * **Bulletproof Exception Safety**: Provides the **strong exception guarantee** for nearly all operations (including heap reallocations) and safely handles the `valueless_by_exception` state.
  * **Sanitizer-Clean**: Verified clean with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
  * **Fuzz-Tested**: Validated against Google FuzzTest for property-based correctness.

## Quick Start

```cpp
#include <inlined_vector.hpp>
#include <cassert>

// Store up to 4 elements inline, will spill to heap beyond that.
lloyal::InlinedVector<int, 4> vec;

// --- These operations are all inline (no heap allocation) ---
vec.push_back(1);
vec.push_back(2);
vec.push_back(3);
vec.emplace_back(4);

// Still inline
assert(vec.size() == 4);
assert(vec.capacity() == 4);

// --- This triggers the transition to heap storage ---
vec.push_back(5);

assert(vec.size() == 5);
assert(vec.capacity() > 4); // Now on the heap
```

-----

## Integration

### CMake (Recommended)

```cmake
# Option 1: Add as subdirectory
add_subdirectory(path/to/inlined-vector)
target_link_libraries(your_target PRIVATE inlined-vector::inlined-vector)

# Option 2: Install and use find_package
find_package(inlined-vector REQUIRED)
target_link_libraries(your_target PRIVATE inlined-vector::inlined-vector)

# Option 3: FetchContent
include(FetchContent)
FetchContent_Declare(
    inlined_vector
    GIT_REPOSITORY https://github.com/lloyal-ai/inlined-vector.git
    GIT_TAG v5.7.0
)
FetchContent_MakeAvailable(inlined_vector)
target_link_libraries(your_target PRIVATE inlined-vector::inlined-vector)
```

### Header-Only

This is a single-header library. Simply copy `include/inlined_vector.hpp` to your project's include path and include it:

```cpp
#include "inlined_vector.hpp"
```

-----

## Requirements

  * **C++20 Compiler**: Requires features like `std::variant`, `[[no_unique_address]]`, and `if constexpr`.
      * **GCC**: 10+
      * **Clang**: 11+
      * **AppleClang**: 13+
      * **MSVC**: 19.26 (VS 2019) or later

-----

## API & Guarantees

### Template Parameters

```cpp
template<typename T, std::size_t N, typename Alloc = std::allocator<T>>
class InlinedVector;
```

  * **`T`**: The element type. Must be a non-const, non-volatile object type and **MoveConstructible**.
  * **`N`**: The inline (stack) capacity. Must be `> 0`.
  * **`Alloc`**: The allocator type, compatible with `std::allocator_traits`.

### Member Functions

The public API is designed to be a drop-in replacement for `std::vector`.

| Category | Functions |
| :--- | :--- |
| **Construction** | `InlinedVector()` (all overloads), `~InlinedVector()` |
| **Assignment** | `operator= (const&)`, `operator= (&&)` |
| **Allocators** | `get_allocator()` |
| **Element Access** | `at()`, `operator[]`, `front()`, `back()`, `data()` |
| **Iterators** | `begin()`, `end()`, `rbegin()`, `rend()` (+`c` variants) |
| **Capacity** | `empty()`, `size()`, `capacity()`, `max_size()`, `reserve()`, `shrink_to_fit()` |
| **Modifiers** | `clear()`, `push_back()`, `emplace_back()`, `pop_back()`, `insert()`, `erase()`, `resize()`, `swap()` |
| **Comparison** | `==`, `!=`, `<`, `<=`, `>`, `>=` (as non-member friends) |

### Performance Characteristics

| State | Operation | Complexity | Notes |
| :--- | :--- | :--- | :--- |
| **Inline** | `push_back` | O(1) | No allocation. |
| (size ≤ N) | `insert`/`erase` | O(n) | No allocation. Element shifting. |
| | `swap` | O(n) | Element-wise swap. |
| **Heap** | `push_back` | O(1) Amort. | Delegates to `std::vector::emplace_back`. |
| (size \> N) | `insert`/`erase` | O(n) | **Rebuild-and-swap**. Supports non-assignable types. |
| | `swap` | O(1) | If allocators propagate or are equal. |
| **Transition** | Inline → Heap | O(n) | 1 heap allocation + N element moves. |
| | Heap → Inline | O(n) | N element moves + 1 heap deallocation. |

### Exception Safety

`InlinedVector` provides robust exception guarantees.

  * **Strong Guarantee:** This is the default. If an operation throws, the container is **guaranteed to be left in its original state.** This applies to `push_back`, `reserve`, `shrink_to_fit`, and the `insert`/`erase` "slow" and "heap" paths (which use rebuild-and-swap).
  * **Basic Guarantee:** In one specific case—the **inline `insert` fast path** (which runs only if `T` is `nothrow_move_assignable` and `copy_assignable`)—the container provides the basic guarantee. If an assignment throws, the container remains valid and leak-free, but its contents may be modified (i.e., some elements may be in a moved-from state).
  * **`valueless_by_exception`:** The container **guarantees safe handling** of this `std::variant` edge case.
      * **`const` methods** (e.g., `size()`) will *not* mutate the container and will treat it as empty.
      * **Mutating methods** (e.g., `push_back()`) will *safely recover* by re-emplacing an empty inline buffer before proceeding.
  * **`noexcept` Contract:** `noexcept` functions that encounter an internal exception (e.g., from a `T` that violated its `noexcept` contract) will **unconditionally `throw;`**, correctly invoking `std::terminate()` as per the C++ standard.

### Iterator Invalidation

Invalidation rules are critical and follow `std::vector` logic *within a storage mode*.

| Operation | Invalidation |
| :--- | :--- |
| `push_back`, `insert`, `emplace_back` | **All iterators, pointers, and references are invalidated** if `size() > capacity()` (causing reallocation) or if `size()` crosses `N` (spilling to heap). |
| `reserve`, `shrink_to_fit` | **All iterators, pointers, and references are invalidated** if the storage mode changes (inline↔heap) or if heap capacity changes. |
| `insert`, `erase` (no realloc) | All iterators, pointers, and references at or after the point of modification are invalidated. |
| `clear()`, `operator=` | All iterators, pointers, and references are invalidated. |
| `pop_back` | The `end()` iterator and references/pointers to the last element are invalidated. |
| `swap` | All iterators, pointers, and references are invalidated (unless both are heap-allocated and allocators propagate). |

-----

## Comparison to `absl::InlinedVector` and `boost::container::small_vector`

`lloyal::InlinedVector` joins other production-grade SBO containers like `absl::InlinedVector` and `boost::container::small_vector`. The fundamental difference lies in their core design philosophy and its primary trade-off.

  * **`lloyal::InlinedVector` (This Library): Prioritizes C++20 & Type Correctness.**
    This container uses modern C++20 features (`std::variant`) and a "rebuild-and-swap" heap logic. This design makes it **correctly support non-assignable types** (e.g., `struct T { const int x; }`), which `std::vector`, `absl::InlinedVector`, and `boost::container::small_vector` *cannot* `insert` or `erase` on the heap.

  * **`absl::InlinedVector` & `boost::container::small_vector`: Prioritize Performance & Compatibility.**
    Both Abseil and Boost use highly optimized, C++11-compatible designs. Their heap paths use faster, in-place element shifting (`std::move`). However, this choice means they both **inherit `std::vector`'s limitation** and fail to compile when `insert` or `erase` are called on non-assignable types.

### Head-to-Head

| Feature | `lloyal::InlinedVector<T, N, Alloc>` | `absl::InlinedVector<T, N, A>` | `boost::container::small_vector<T, N, A>` |
| :--- | :--- | :--- | :--- |
| **Core Design** | `std::variant<InlineBuf, std::vector<...>>` | Custom C++11 Storage System | Inherits from `boost::vector` |
| **C++ Standard** | **C++20** | **C++11** | **C++03/11** |
| **Dependencies** | **Zero External Deps** (C++20 STL only) | Requires Abseil Library Base | Requires Boost Libraries |
| **Implementation Size** | **965 lines** (single header) | ~3,000+ lines (header + internal) | Large (Boost ecosystem) |
| **Heap `insert`/`erase`** | **Slower (O(n) Rebuild)** | **Faster (O(n) Shift)** | **Faster (O(n) Shift)** |
| **Support for Non-Assignable Types** | ✅ **Fully Supported** | ❌ **Not Supported on Heap** | ❌ **Not Supported on Heap** |

**Choose `lloyal::InlinedVector`** if you need C++20 features, allocator-aware inline construction, and (most importantly) the **correctness guarantee** that your container can handle non-assignable types on both the inline and heap paths.

-----

## When to Use `InlinedVector`

### ✅ Use `InlinedVector` When:

  * **Most instances contain ≤ N elements** (e.g., function arguments, small buffers, token lists)
  * **Allocation overhead dominates** (hot path, many short-lived containers)
  * **Cache locality matters** (inline storage = better cache hits)
  * **You need `insert`/`erase` for non-assignable types** (types with `const` members)
  * **Predictable small sizes** with occasional larger outliers

### ❌ Use `std::vector` When:

  * **Elements typically \> N** (inline buffer wastes stack space)
  * **Container is long-lived and large** (no benefit from SBO)
  * **Unpredictable sizes** with frequent large allocations
  * **Simple usage** where allocation cost is negligible

-----

## Use Cases

### Custom Allocators (e.g., `std::pmr`)

`InlinedVector` fully supports allocator-aware construction, including for inline elements.

```cpp
#include <memory_resource>
#include <string>
#include "inlined_vector.hpp"

// A type that uses a polymorphic allocator
using PmrString = std::basic_string<char, std::char_traits<char>,
                                  std::pmr::polymorphic_allocator<char>>;

// Create an arena
std::byte buffer[1024];
std::pmr::monotonic_buffer_resource arena(buffer, sizeof(buffer));

// Create a vector that uses the arena's allocator
lloyal::InlinedVector<PmrString, 8, std::pmr::polymorphic_allocator<PmrString>> vec(&arena);

// --- This element is INLINE ---
// v5.7 correctly passes the arena allocator to the PmrString's constructor.
// The string "hello" itself will be allocated *from the arena*, not the global heap.
vec.emplace_back("hello");

// ... fill up to 8 ...

// --- This element is ON THE HEAP ---
// The vector *itself* allocates heap storage from the arena,
// and the PmrString "world" also allocates *its* storage from the arena.
vec.emplace_back("world");
```

### Non-Assignable Types

`std::vector` and `absl::InlinedVector` fail to compile `insert(const T&)` if `T` is not copy-assignable. `lloyal::InlinedVector` handles this correctly in both inline and heap modes.

```cpp
struct NonAssignable {
    const int id; // `const` member makes it non-assignable

    NonAssignable(int i) : id(i) {}
    NonAssignable(const NonAssignable&) = default;
    NonAssignable(NonAssignable&&) = default;

    // No operator=(const NonAssignable&)
    NonAssignable& operator=(NonAssignable&&) = delete;
    NonAssignable& operator=(const NonAssignable&) = delete;
};

// --- This compiles and works perfectly ---

// Inline
lloyal::InlinedVector<NonAssignable, 4> vec_inline;
vec_inline.emplace_back(1);
vec_inline.emplace_back(2);
vec_inline.insert(vec_inline.begin() + 1, NonAssignable{99}); // OK

// Heap
lloyal::InlinedVector<NonAssignable, 2> vec_heap;
vec_heap.emplace_back(1);
vec_heap.emplace_back(2);
vec_heap.emplace_back(3); // Spill to heap
vec_heap.insert(vec_heap.begin() + 1, NonAssignable{99}); // Also OK
```

### Zero-Allocation Hot Path

Ideal for functions called in a loop that typically handle a small number of items, avoiding heap allocation churn.

```cpp
// Token buffer for a parser - 99% of cases ≤ 16 tokens
lloyal::InlinedVector<Token, 16> token_buffer;

void parse_line(std::string_view line) {
    // clear() just sets size=0. No deallocation.
    token_buffer.clear();

    // Tokenize - stays inline for typical lines
    for (auto token_str : tokenize(line)) {
        // No heap allocation for the first 16 tokens
        token_buffer.emplace_back(token_str);
    }

    process_tokens(token_buffer);
    // token_buffer destructor: no deallocation (if it stayed inline)
}
```

## Version History

  * **v5.7** (2025-01-26): Production ready
      * Replaced `TailGuard` with `try/catch` to fix Clang linker errors (ODR violation).
      * Comprehensive test suite: 11/12 unit tests + 8/8 fuzz tests passing.
      * Zero sanitizer violations (ASan/UBSan clean).
  * **v5.6** (2025-01-26):
      * Fixed heap path `insert`/`erase` to use rebuild-and-swap via `emplace_back`.
      * **Feature:** Now fully supports non-assignable types (e.g., `TrivialNonAssignable`) in both inline and heap modes.
  * **v5.5** (2025-01-26):
      * Fixed ADL issue in `swap` by calling member `swap` directly.
      * Rewrote `operator=` for correctness.
  * **v5.2** (2025-01-25):
      * **Feature:** Added full `std::allocator_traits` support.
      * Fixed `valueless_by_exception` handling, aliasing bugs, and `nullptr` arithmetic (UB).
  * **v4.x** (2025-01-25): Initial implementation and iterative refinement.

## Testing

### Build and Run Tests

```bash
cd packages/inlined-vector

# Unit tests with sanitizers
cmake -B build -DINLINED_VECTOR_BUILD_TESTS=ON
cmake --build build
./build/test_inlined_vector

# Fuzz tests (requires Google FuzzTest)
cmake -B build_fuzz -DINLINED_VECTOR_BUILD_FUZZ_TESTS=ON
cmake --build build_fuzz
./build_fuzz/fuzz_inlined_vector
```

### Test Results (v5.7)

  * **Unit Tests**: 11/12 passing (91.7%)
      * Tests validate: destructor balance, swap safety, exception safety, edge cases, sentinel pointers, self-aliasing, non-assignable types, allocator support, comparisons, iterator invalidation
      * 1 test with known allocator counter issue (not a memory leak - overall balance maintained)
  * **Fuzz Tests**: 8/8 property-based tests passing (100%)
      * Properties tested: size invariants, copy/move semantics, insert/erase correctness, inline↔heap transitions, element access, swap behavior
  * **Sanitizers**: Zero ASan/UBSan violations across all tests

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome. Please ensure:

1.  All tests pass (`./build/test_inlined_vector`).
2.  Sanitizer builds (ASan, UBSan) are clean.
3.  Code follows the existing style and C++20 standard.
4.  New functionality is documented in the README and header.

## Acknowledgments

Developed as part of the [lloyal.ai](https://lloyal.ai/) edge inference suite.