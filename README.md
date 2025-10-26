# `InlinedVector`

[![CI](https://github.com/lloyal-ai/inlined-vector/actions/workflows/ci.yml/badge.svg)](https://github.com/lloyal-ai/inlined-vector/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17%2F20-blue.svg)](https://en.cppreference.com/w/cpp/17)

A **C++17/20 header-only** container with `std::vector` semantics, Small Buffer Optimization (SBO), and full, robust allocator support. **Truly zero external dependencies**—just copy one header file into your project.

**Performance validated** (Apple M1, N=16, -O3, Abseil master, Boost 1.88):
- **13.2× faster** than `std::vector` for inline operations (trivial types, size=8)
- **Fastest heap insertions** at N=128 (rebuild-and-swap wins)
- **Zero overhead** for custom allocators (parent pointer architecture)
- **Only implementation** that compiles `insert`/`erase` for non-assignable types on heap

This container is a production-ready, drop-in replacement for `std::vector` in scenarios where elements are often small (e.g., `< 16`), delivering massive performance benefits by avoiding heap allocations while maintaining competitive performance for larger collections.

## Features

  * **Zero External Dependencies**: A single C++17 header. No build system complexity, no large libraries.
  * **Small Buffer Optimization**: Guarantees zero heap allocations as long as `size() <= N`.
  * **Bidirectional Heap↔Inline Transitions**: `shrink_to_fit()` can return from heap to inline storage, eliminating permanent allocations from temporary size spikes.
  * **Allocator-Aware**: Full support for `std::allocator_traits` and `std::pmr`.
      * Guarantees **correct allocator propagation** (POCMA, POCS, `select_on_container_copy_construction`) consistent with standard containers.
      * Ensures `std::uses_allocator` construction uses the **correct owning allocator instance**, even for elements stored inline.
      * All element lifetimes (construction/destruction) are managed via `allocator_traits` through the **owning container's allocator**, ensuring compatibility with stateful or custom allocators.
      * **ABI Note (v5.7+):** The addition of the `parent_` pointer to `InlineBuf` changes the memory layout. Code compiled against v5.6 or earlier is **not binary-compatible** with v5.7+.
  * **Supports Non-Assignable Types**: `insert()` and `erase()` work for non-assignable types (e.g., `const` members) even when heap-allocated, a feature `std::vector` and other SBO implementations lack.
  * **Robust Exception Safety**: Provides the **strong exception guarantee** for most operations by default, using internal rebuild-and-swap where necessary. Clearly defines the **single specific scenario** (inline insert fast-path with throwing copy assignment) where the basic guarantee applies, and safely handles the `valueless_by_exception` state through automatic recovery on mutation.
  * **Sanitizer-Clean**: Verified clean with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
  * **Fuzz-Tested**: Validated against Google FuzzTest for property-based correctness.
  * **Compact Implementation**: \~965 lines in a single header with comprehensive comments explaining design decisions.

## Quick Start

```cpp
#include "inlined_vector.hpp"
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

## Why This Exists: Non-Assignable Types That Just Work

Unlike `std::vector`, `absl::InlinedVector`, and `boost::small_vector`, this container supports types with `const` members or deleted assignment operators in **all** operations—including `insert()` and `erase()`, even when heap-allocated.

**This isn't a workaround. It's correct design.**

### The Problem with std::vector

Standard containers require `MoveAssignable` for `insert()`/`erase()` because they shift elements via assignment. Types with `const` members **implicitly delete** their assignment operators, making them incompatible with these operations.

### Real-World Examples That Work Here (But Fail Elsewhere)

#### 1. Immutable Domain Objects

```cpp
struct AuditEvent {
    const uint64_t event_id;      // Immutable - prevents accidental modification
    const Timestamp occurred_at;  // Immutable timestamp
    std::string description;      // Mutable payload

    AuditEvent(uint64_t id, Timestamp t, std::string desc)
        : event_id(id), occurred_at(t), description(std::move(desc)) {}
};

lloyal::InlinedVector<AuditEvent, 16> audit_log;
audit_log.emplace_back(1, now(), "User logged in");
audit_log.insert(audit_log.begin(), AuditEvent{0, earlier(), "System start"});
// ✅ Works! std::vector<AuditEvent> fails to compile insert().
```

**Why `const` matters:** Event IDs and timestamps shouldn't change after creation. Using `const` enforces this invariant at compile-time, preventing bugs like `event.event_id = wrong_id;`. With `std::vector`, you'd have to choose between type safety and container flexibility.

#### 2. Cache Entries with Immutable Keys

```cpp
struct CacheEntry {
    const std::string key;  // Key cannot change after construction
    std::string value;      // Value can be updated
    size_t hit_count = 0;

    CacheEntry(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)) {}
};

lloyal::InlinedVector<CacheEntry, 32> lru_cache;
// Insert, erase, reorder—all work despite const key
lru_cache.erase(lru_cache.begin() + 5);  // ✅ Compiles and runs correctly
lru_cache.insert(lru_cache.begin(), CacheEntry{"hot_key", "frequent_value"});
```

**Why `const` matters:** In a cache, the key identifies the entry and should never change. Accidental assignment like `entry.key = new_key` breaks lookup invariants. `const` prevents this entire class of bugs.

#### 3. Resource Handles with Deleted Assignment

```cpp
struct ResourceHandle {
    const uint64_t id;
    std::unique_ptr<Resource> resource;

    ResourceHandle(ResourceHandle&&) = default;
    ResourceHandle& operator=(ResourceHandle&&) = delete;  // Prevent reassignment
};

lloyal::InlinedVector<ResourceHandle, 8> handles;
handles.insert(handles.begin(), ResourceHandle{next_id(), acquire_resource()});  // ✅ Works
```

**Why deleted assignment matters:** Some types shouldn't be reassigned after construction (e.g., RAII handles, thread IDs, database connections). Deleting `operator=` enforces this, but breaks compatibility with standard containers.

### How It Works: Rebuild-and-Swap

Instead of shifting elements via assignment (like `std::vector`), `lloyal::InlinedVector` uses a rebuild-and-swap approach for `insert()`/`erase()` operations:

1. Construct elements into a new buffer (construction, not assignment)
2. Swap the new buffer into place
3. Destroy the old buffer

This bypasses the `MoveAssignable` requirement entirely, at the cost of O(n) reconstruction. **Benchmarks show this is actually faster** than in-place assignment for heap insertions (6169ns vs 6335ns Abseil at N=128).

### The Key Insight

These aren't edge cases—they're **good design patterns**. Using `const` to enforce immutability guarantees is a best practice in modern C++. `std::vector` forces you to choose between:
- ✅ Type safety with `const` members (but lose `insert`/`erase`)
- ✅ Container flexibility (but lose compile-time invariants)

`lloyal::InlinedVector` gives you both.

**Note:** For small inline buffers, the primary benefit remains avoiding heap allocations entirely (8-13× faster). Non-assignable type support becomes most critical when:
- Your collection grows beyond inline capacity and uses heap storage
- You want to use `insert()`/`erase()` with types that have `const` members
- You're designing domain types with immutability guarantees and need full container compatibility

### Verification

This capability is validated by:
- **Benchmark suite** (`bench/bench_inlined_vector.cpp:317-344`): Non-assignable insert benchmark runs at 819ns
- **Fuzz tests** (`tests/fuzz_inlined_vector.cpp`): 9/9 fuzz tests passing with non-assignable types across inline↔heap transitions
- **Zero sanitizer violations** (ASan/UBSan clean)

**No peer implementation can do this**—their architectures fundamentally rely on assignment-based algorithms.

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

  * **C++17 Compiler (C++20 Recommended)**:
      * Required C++17 features: `std::variant`, `if constexpr`, `std::launder`
      * Optional C++20 feature: `[[no_unique_address]]` (reduces empty allocator overhead)
      * **GCC**: 9+ (C++17)
      * **Clang**: 7+ (C++17)
      * **AppleClang**: 13+ (C++17)
      * **MSVC**: 19.14 (VS 2017) or later

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

#### Implementation Strategy

`lloyal::InlinedVector` uses a **three-tier optimization strategy** for inline operations:

1.  **Trivial Types** (e.g., `int`, `float`, POD structs): Uses `memcpy`/`memmove` for maximum speed.
2.  **Nothrow-Move Types** (e.g., `std::string`, `std::unique_ptr`): Uses an optimized in-place shift-and-assign.
3.  **Potentially-Throwing Types** (legacy code, types with throwing moves): Uses a rebuild-and-swap path to provide the strong exception guarantee.

This approach ensures **optimal performance for modern types** (Tiers 1-2) while maintaining **correctness guarantees for all types** (Tier 3).

### Exception Safety

`InlinedVector` provides robust exception guarantees.

  * **Strong Guarantee:** This is the default. If an operation throws, the container is **guaranteed to be left in its original state.** This applies to `push_back`, `reserve`, `shrink_to_fit`, and the `insert`/`erase` "slow" and "heap" paths (which use rebuild-and-swap).
  * **Basic Guarantee:** In one specific case—the **inline `insert` fast path** (which runs only if `T` is `nothrow_move_assignable` and `copy_assignable`)—the container provides the basic guarantee. If an assignment throws, the container remains valid and leak-free, but its contents may be modified (i.e., some elements may be in a moved-from state).
  * **`valueless_by_exception`:** The container **guarantees safe handling** of this `std::variant` edge case.
      * **`const` methods** (e.g., `size()`) will *not* mutate the container and will treat it as empty.
      * **Mutating methods** (e.g., `push_back()`) will *safely recover* by re-emplacing an empty inline buffer before proceeding.
  * **`noexcept` Contract:** `noexcept` functions that encounter an internal exception (e.g., from a `T` that violated its `noexcept` contract) will **unconditionally `throw;`**, correctly invoking `std::terminate()` as per the C++ standard.

-----

## Detailed Guarantees

Beyond the `std::vector`-like API, `lloyal::InlinedVector` provides specific behavioral guarantees, especially regarding allocators and exceptions.

### Allocator Propagation and Usage

`InlinedVector` adheres strictly to standard C++ allocator rules:

* **Construction:** Allocators are passed via constructors and stored. `select_on_container_copy_construction` is used for copy construction allocator selection.
* **Element Lifetime:** **All** construction and destruction of elements `T`, whether stored inline or on the heap, is performed using `std::allocator_traits<Alloc>::construct` and `std::allocator_traits<Alloc>::destroy` invoked on the **container's current allocator instance**. This ensures correct behavior even with stateful allocators or allocators with custom `construct`/`destroy` logic. The internal `InlineBuf` uses a `parent_` pointer back to the owning `InlinedVector` to access the correct allocator instance for these operations.
* **Copy Assignment (`operator=`):** Honors `propagate_on_container_copy_assignment` (POCMA). If `POCMA::value` is true and allocators differ, the destination's allocator is replaced *after* clearing elements with the old allocator. If false, allocators must be equal (or behavior is undefined per standard library rules, though `InlinedVector` handles this via element-wise copy).
* **Move Assignment (`operator=`):** Honors `propagate_on_container_move_assignment` (POCMA).
    * If `POCMA::value` is true, the source allocator is **always moved** to the destination (after clearing destination contents). The source container is left with a default-constructed allocator.
    * If `POCMA::value` is false (and `is_always_equal::value` is false), allocators **must compare equal** for resource stealing (O(1) move). If they differ, an **element-wise move** is performed using the destination's allocator (O(n)).
    * **Invariant:** If the move results in the destination holding an `InlineBuf`, its internal `parent_` pointer is correctly **retargeted** to the destination container.
* **Swap (`swap()` member and non-member):** Honors `propagate_on_container_swap` (POCS).
    * If `POCS::value` is true, the allocator instances themselves are **swapped** between the containers using `std::swap`.
    * If `POCS::value` is false (and `is_always_equal::value` is false), the allocators **must compare equal**. Swapping containers with unequal, non-propagating allocators is **undefined behavior** per the standard; `InlinedVector` includes an `assert` to detect this in debug builds.
    * **Invariant:** During a swap involving one inline and one heap container (`mixed swap`), the internal `InlineBuf` object may move between containers via `std::variant::swap`. After the swap, the `parent_` pointer within any moved `InlineBuf` is correctly **retargeted** to its new owning container. The `parent_` pointers are *never* swapped directly by `InlineBuf::swap`.

### Enhanced Exception Safety

`InlinedVector` prioritizes correctness and aims for the strong guarantee where feasible.

* **Strong Guarantee (Default):** Operations like `push_back`, `emplace_back`, `reserve`, `shrink_to_fit`, and `insert`/`erase` (when not using the specific inline fast path below) provide the strong guarantee. If an exception occurs (e.g., from an element's constructor or move), the container is **rolled back to its original state**. This is achieved primarily through:
    * Copy-and-swap or rebuild-and-swap semantics for heap operations.
    * Careful ordering and RAII (`InlineBuf` temporary) for inline rebuilds.
* **Basic Guarantee (Specific Case):** The **only** deviation from the strong guarantee occurs during the **fast path** of `insert()` when operating **inline** *and* when `T` satisfies `std::is_nothrow_move_assignable_v<T>` and `std::is_copy_assignable_v<T>`. This path shifts elements using move assignment for performance. If the final **copy assignment** (`p[idx] = src`) throws an exception, the container remains in a **valid state** (destructible, invariants hold), but the element at `p[idx]` might be left in a moved-from state, and the newly constructed temporary element at the end will be destroyed. This matches the basic guarantee provided by `std::vector::insert` under similar conditions.
* **`valueless_by_exception` Handling:** If an operation leaves the internal `std::variant` storage in the valueless state (e.g., due to an exception during a move in `shrink_to_fit` or `swap`), the container guarantees:
    * **`const` methods** (`size`, `empty`, `capacity`, `data`, iterators, `operator[]`, `at`) will **safely operate** as if the container is empty (returning 0 size, valid empty ranges, etc.) **without modifying** the state.
    * Any subsequent **mutating operation** (`push_back`, `insert`, `clear`, etc.) will first **atomically recover** by emplacing a default-constructed `InlineBuf` before proceeding with the operation. This ensures the container always transitions back to a valid state upon mutation.

### Trivial Type Optimizations (Implicit Lifetime)

For trivially copyable types `T`, `InlinedVector` uses `memcpy` and `memmove` for certain inline operations (like append or shift during insert/erase) to improve performance. This is **correct and standard-conformant** under C++17's P0593 rules ("Implicit object creation for trivial types"), which allow objects of such types to implicitly begin their lifetime when their storage is written via byte-copy operations.

### Iterator Invalidation

Invalidation rules are critical and follow `std::vector` logic *within a storage mode*.

| Operation | Invalidation | Reason |
| :--- | :--- | :--- |
| `push_back`, `insert`, `emplace_back` | **All** iterators, pointers, references | If `size()` > `capacity()` (reallocation) **OR** if `size()` crosses `N` (transition inline→heap). |
| `reserve`, `shrink_to_fit` | **All** iterators, pointers, references | If storage mode changes (inline↔heap) **OR** if heap capacity changes. |
| `insert`, `erase` (no realloc/transition) | At or after modification point | Standard sequential container behavior. |
| `clear()`, `operator=` | **All** iterators, pointers, references | Container contents replaced. |
| `pop_back` | `end()` iterator, last element ref/ptr | Last element removed. |
| `swap` | **All** iterators, pointers, references | Unless both heap & POCS=true/`is_always_equal`. Container contents change owners. |

-----

## Comparison to `absl::InlinedVector` and `boost::container::small_vector`

`lloyal::InlinedVector` is a **zero-dependency alternative** to Abseil's and Boost's SBO containers, with architectural advantages that enable features impossible in those implementations.

### Core Architectural Differences

  * **`lloyal::InlinedVector` (This Library): Modern C++17/20 Design**
    Uses `std::variant` for type-safe storage discrimination and delegates heap management. This architecture enables:

      * **Zero external dependencies** (single header, C++17 STL only)
      * **Bidirectional heap↔inline transitions** via `shrink_to_fit()`
      * **Full support for non-assignable types** (types with `const` members work everywhere)
      * **Valueless exception recovery** for guaranteed basic safety

  * **`absl::InlinedVector`: Modern C++ Design (Current Master)**
    Uses sophisticated storage management with `shrink_to_fit()` support. Part of Abseil library:

      * ✅ Supports heap↔inline transitions via `shrink_to_fit()` (added post-LTS 2021)
      * ❌ Requires entire Abseil library infrastructure
      * ❌ Fails to compile `insert`/`erase` on non-assignable types when on the heap

  * **`boost::container::small_vector`: C++03/11 Compatibility**
    C++11-compatible design with permanent heap allocation after first spill:

      * ❌ Requires Boost library infrastructure
      * ❌ **Permanent heap allocation** after first spill (per Boost docs: "any change to capacity...will cause the vector to permanently switch to dynamically allocated storage")
      * ❌ Fails to compile `insert`/`erase` on non-assignable types when on the heap

### Head-to-Head (N=16)

| Feature | `lloyal::InlinedVector` | `absl::InlinedVector` | `boost::small_vector` |
| :--- | :--- | :--- | :--- |
| **C++ Standard** | **C++17 / C++20** | C++11 | C++03 / C++11 |
| **Dependencies** | **None (Single Header)** | Abseil Library Base | Boost Libraries |
| **Bidirectional Heap↔Inline** | ✅ **Yes (via `shrink_to_fit`)** | ✅ **Yes (via `shrink_to_fit`)*** | ❌ **No (Permanent Heap)** |
| **Support for Non-Assignable Types** | ✅ **Fully Supported** | ❌ **Not Supported on Heap** | ❌ **Not Supported on Heap** |
| **Custom Allocator Overhead** | **0% (parent pointer)** | N/A | N/A |
| **Heap `insert` Algorithm** | **Rebuild-and-Swap** | In-Place Shift | In-Place Shift |
| **Heap `insert` Perf. (Complex, N=128)** | **6169 ns (fastest)** ✅ | 6335 ns | 6317 ns |
| **Inline `push_back` Perf. (Trivial, N=8)** | **13.2 ns (fastest)** ✅ | 17.2 ns | 16.6 ns |
| **Non-Assignable `insert` (Heap, N=17)** | ✅ **Compiles & Runs (819 ns)** | ❌ **Compile Fail** | ❌ **Compile Fail** |

***Note:** Abseil's `shrink_to_fit()` was added post-LTS 2021. Older LTS versions (e.g., 2021_03_24) lack this feature. This comparison uses current master branches (as of 2025-01).

### Key Insights:

1.  **Dominant Inline Performance:** For its primary use case (small, inline vectors), `lloyal` is **13.2× faster** than `std::vector` for trivial types and **23% faster** than Abseil.
2.  **Winning Heap Performance:** The "rebuild-and-swap" logic is now the **fastest implementation** for heap insertions at N=128 (6169ns vs 6335ns Abseil).
3.  **Zero Allocator Overhead:** Custom allocators (BenchAllocator) show **0% overhead** vs std::allocator (379ns vs 380ns), validating parent pointer architecture.
4.  **Unique Correctness Guarantee:** It is the *only* implementation to compile and run the non-assignable `insert` benchmark, proving its superior type support.

-----

## When to Use `InlinedVector`

### ✅ Use `InlinedVector` When:

  * **Most instances contain ≤ N elements** (achieves 8-12× speedup over `std::vector`)
  * **You need zero external dependencies** (embedded systems, header-only libraries, minimal toolchain)
  * **You're building an SDK/library** and cannot impose Abseil/Boost on your users
  * **Build time and binary size matter** (single 965-line header vs large library integration)
  * **Allocation overhead dominates** (hot path, many short-lived containers)
  * **Cache locality matters** (inline storage = better cache hits)
  * **You need `insert`/`erase` for non-assignable types** (unique capability vs peers)
  * **Workloads have temporary size spikes** (heap→inline transition recovers memory)

### ❌ Use `std::vector` When:

  * **Elements typically > N** (inline buffer wastes stack space, no performance benefit)
  * **Container is long-lived and large** (no benefit from SBO)
  * **Unpredictable sizes** with frequent large allocations
  * **Simplicity matters more than 12× inline performance gain**

### ⚖️ Consider Peers (`absl::InlinedVector` / `boost::small_vector`) When:

  * Already deeply integrated with Abseil or Boost ecosystems
  * Need absolute fastest heap insert for complex types (~10% advantage)
  * Require C++03/C++11 compatibility (Boost supports C++03)
  * Prioritize decades of production battle-testing over modern C++17/20 features
  * Don't need non-assignable type support (all implementations support heap→inline transitions except Boost)

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

**Why This Works:** `std::vector::insert` requires `MoveAssignable` when shifting elements in-place during insertion. For non-assignable types (e.g., types with `const` members), this requirement makes the operation ill-formed. Our rebuild-and-swap approach uses only construction/destruction, bypassing this requirement entirely. See: [std::vector::insert requirements](https://en.cppreference.com/w/cpp/container/vector/insert)

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

### Bidirectional Transitions (Temporary Spikes)

Ideal for algorithms with temporary size spikes, as `shrink_to_fit` reclaims all heap memory.

```cpp
// Pathfinding or tree traversal buffer
lloyal::InlinedVector<Node*, 16> path_stack; // Usually < 16 deep

// Temporarily traverse a very deep branch
for (int i = 0; i < 100; ++i) {
    path_stack.push_back(get_deep_node(i));  // → spills to heap at 17
}

// ... algorithm finishes, return to a shallow state
path_stack.resize(5); // Back to 5 elements (still on heap)

// --- Reclaim all heap memory ---
path_stack.shrink_to_fit(); // → returns to inline storage!

// No heap allocation remains.
// Note: Abseil master also supports this; Boost permanently stays on heap.
assert(path_stack.capacity() == 16);
```

-----

## Performance Benchmarks

### Test Environment
- **Hardware**: Apple M1 (ARM64)
- **Compiler**: AppleClang 17.0.0, -O3 -march=native
- **Inline Capacity**: N=16
- **Framework**: Google Benchmark v1.8.3
- **Comparison Libraries**:
  - **Abseil**: `master` branch (2025-10-26, includes shrink_to_fit)
  - **Boost**: `1.88.0` (Homebrew)

Full benchmark suite in `bench/` directory with allocator-specific tests. Run: `cmake -B build_bench -DINLINED_VECTOR_BUILD_BENCHMARKS=ON && cmake --build build_bench && ./build_bench/bench_inlined_vector`

### 1. Inline Performance Dominance

**The primary benefit of SBO is eliminating heap allocations.** All implementations achieve massive speedups for small collections:

```cpp
// Fill 8 elements (trivial type: uint64_t)
lloyal::InlinedVector<uint64_t, 16> vec;
for (int i = 0; i < 8; ++i) vec.push_back(i);
```

| Implementation | Time | Speedup vs `std::vector` |
|----------------|------|--------------------------|
| `std::vector` | 174 ns | 1.0× (baseline) |
| **`lloyal::InlinedVector`** | **13.2 ns** | **13.2×** ✅ |
| `boost::small_vector` | 16.6 ns | 10.5× |
| `absl::InlinedVector` | 17.2 ns | 10.1× |

**`lloyal` is fastest for trivial types** (23% faster than Abseil), due to optimized `memcpy` fast paths in the three-tier strategy.

```cpp
// Fill 8 elements (complex type: std::string)
lloyal::InlinedVector<std::string, 16> vec;
```

| Implementation | Time | Speedup vs `std::vector` |
|----------------|------|--------------------------|
| `std::vector` | 533 ns | 1.0× (baseline) |
| **`absl::InlinedVector`** | **352 ns** | **1.51×** ✅ |
| `lloyal::InlinedVector` | 380 ns | 1.40× |
| `boost::small_vector` | 390 ns | 1.37× |

**Abseil leads for complex types** (~7% faster), but all SBO implementations are within 10% of each other—essentially competitive.

### 2. Heap Performance - Insert Front (std::string, N=128)

**Rebuild-and-swap is now the fastest strategy** for large heap insertions:

```cpp
// Insert at front (128 elements, on heap)
vec.insert(vec.begin(), value);
```

| Implementation | Time | vs lloyal |
|----------------|------|-----------|
| **`lloyal::InlinedVector`** | **6169 ns** | baseline ✅ |
| `boost::small_vector` | 6317 ns | +2.4% |
| `absl::InlinedVector` | 6335 ns | +2.7% |
| `std::vector` | 6608 ns | +7.1% |

**`lloyal` is fastest across all implementations**, proving rebuild-and-swap is not just correct but also performant at scale.

### 3. Custom Allocator Performance (BenchAllocator, std::string, N=8)

**Parent pointer architecture shows zero overhead** for custom allocators:

```cpp
BenchAllocator<std::string> alloc(1);
lloyal::InlinedVector<std::string, 16, BenchAllocator<std::string>> vec(alloc);
for (int i = 0; i < 8; ++i) vec.push_back(value);
```

| Allocator Type | Time | Overhead |
|----------------|------|----------|
| `std::allocator` | 380 ns | baseline |
| `BenchAllocator` | 379 ns | **0%** ✅ |

**Zero measurable overhead** for custom allocators validates the parent pointer design—correct allocator-awareness without performance cost.

### 4. Shrink To Fit - Heap → Inline Transition

**Bidirectional heap↔inline transition performs identically to std::vector reallocation**:

```cpp
// Start with 21 elements (heap), shrink to 8, then shrink_to_fit
lloyal::InlinedVector<std::string, 16> vec;
for (int i = 0; i < 21; ++i) vec.push_back(value);  // → heap
vec.resize(8);  // Still on heap
vec.shrink_to_fit();  // → returns to inline storage
```

| Implementation | Time | Behavior |
|----------------|------|----------|
| `lloyal::InlinedVector` | 1177 ns | Heap → Inline ✅ |
| `std::vector` | 1188 ns | Heap → Heap (realloc) |

**Zero overhead** for bidirectional transition—reclaiming memory is as fast as std::vector's heap reallocation.

### 5. The Unique Feature: Non-Assignable Types

```cpp
// Real-world use case: audit logs with immutable event IDs
struct AuditEvent {
    const uint64_t event_id;  // Immutable - prevents accidental modification
    std::string description;

    AuditEvent(uint64_t id, std::string desc)
        : event_id(id), description(std::move(desc)) {}
};

lloyal::InlinedVector<AuditEvent, 16> audit_log;
for (int i = 0; i < 17; ++i)
    audit_log.emplace_back(i, "event " + std::to_string(i));  // Spills to heap
audit_log.insert(audit_log.begin(), AuditEvent{0, "System start"});
```

| Implementation | Result |
|----------------|--------|
| **`lloyal::InlinedVector`** | ✅ **Compiles & Runs (819 ns)** |
| `absl::InlinedVector` | ❌ **Does Not Compile** |
| `boost::small_vector` | ❌ **Does Not Compile** |
| `std::vector` | ❌ **Does Not Compile** |

**Only `lloyal::InlinedVector` supports this**—a correctness guarantee impossible in any peer implementation.

### Performance Summary

| Scenario | lloyal Performance | When This Matters |
|----------|-------------------|-------------------|
| **Inline Fill (trivial, N=8)** | **13.2× faster than std::vector** ✅ | Parsers, token buffers, hot paths |
| **Inline Fill (complex, N=8)** | 1.4× faster than std::vector | Small string collections, temporary containers |
| **Heap Insert (N=128)** | **Fastest (6169 ns)** ✅ | Large collections after growth |
| **Custom Allocators** | **0% overhead** ✅ | PMR, arena allocators, stats tracking |
| **Shrink To Fit (heap→inline)** | **Same speed as std::vector** ✅ | Memory reclamation after temp spikes |
| **Non-Assignable Types** | ✅ **Only impl that compiles** | Correctness-critical code with `const` members |

**Bottom line:** `lloyal::InlinedVector` delivers **best-in-class performance** (fastest inline trivial fills, fastest heap insertions) while providing **unique correctness guarantees** (non-assignable types, zero allocator overhead) and **zero dependencies**. The allocator-aware rebuild-and-swap strategy proves to be not just correct but also the fastest approach at scale.

-----

## Version History

  * **v5.7** (2025-01-26): Production ready - **Allocator-Aware Release**
      * **Major Feature:** Full allocator-awareness with parent pointer architecture
        * All element construction/destruction via `AllocTraits::construct/destroy` using owning allocator
        * Correct allocator propagation (POCMA, POCS, SOCCC)
        * Parent pointer retargeting for swap and move operations
        * **ABI Breaking Change:** Added `parent_` pointer to `InlineBuf` (not binary-compatible with v5.6)
      * **C++17 Compatibility:** Added polyfill for `std::construct_at` (C++20 feature)
      * Replaced `TailGuard` with `try/catch` to fix Clang linker errors (ODR violation)
      * Rebuilt heap `insert`/`erase` logic to support non-assignable types
      * Comprehensive test suite: **15/15 unit tests + 9/9 fuzz tests passing** (100%)
      * **Zero sanitizer violations** (ASan/UBSan clean)
      * Regression tests for allocator bugs (swap, move, parent retargeting)
  * **v5.2** (2025-01-25):
      * **Feature:** Added initial `std::allocator_traits` support
      * Fixed `valueless_by_exception` handling, aliasing bugs, and `nullptr` arithmetic (UB)
  * **v4.x** (2025-01-25): Initial implementation and iterative refinement

## Testing

### Build and Run Tests

```bash
# Unit tests with sanitizers
cmake -B build -DINLINED_VECTOR_BUILD_TESTS=ON
cmake --build build
./build/test_inlined_vector

# Fuzz tests (requires Google FuzzTest)
cmake -B build_fuzz -DINLINED_VECTOR_BUILD_FUZZ_TESTS=ON
cmake --build build_fuzz
./build_fuzz/fuzz_inlined_vector

# Benchmarks (requires external dependencies)
cmake -B build_bench -DINLINED_VECTOR_BUILD_BENCHMARKS=ON
cmake --build build_bench
./build_bench/bench_inlined_vector
```

### Test Results (v5.7)

  * **Unit Tests**: 15/15 passing (100%) ✅
      * Tests validate: destructor balance, swap safety, exception safety, edge cases, sentinel pointers, self-aliasing, non-assignable types, allocator support, comparisons, iterator invalidation.
      * **Regression tests** for allocator-awareness bugs:
        * TEST 13: InlineBuf::swap allocator mix-up (POCS=true)
        * TEST 14: parent_ retargeting on mixed swap (inline↔heap)
        * TEST 15: parent_ retargeting on move assignment
  * **Fuzz Tests**: 9/9 property-based tests passing (100%) ✅
      * Properties tested: size invariants, copy/move semantics, insert/erase correctness, inline↔heap transitions, element access, swap behavior.
      * **Regression fuzz tests** for allocator bugs using custom allocators (TestAllocator, TestAllocatorPOCS) and non-assignable types.
  * **Sanitizers**: **Zero ASan/UBSan violations** across all tests ✅

-----

## Internal Design Notes (For Contributors)

* **Storage:** Uses `std::variant<InlineBuf, std::vector<T, Alloc>>` (`Storage`). `InlineBuf` is a simple struct holding an aligned `std::byte` buffer, size, and a `parent_` pointer.
* **`InlineBuf::parent_` Invariant:** This pointer **must always** point to the `InlinedVector` instance that currently owns the `InlineBuf` object *within the variant*.
    * It is initialized in `InlineBuf`'s constructor.
    * It is **crucial** for routing `construct`/`destroy` calls to the correct allocator instance.
    * It **must be retargeted** (updated) after any operation that moves an `InlineBuf` from one `InlinedVector`'s `storage_` variant to another's (specifically, in the mixed-mode `swap` and certain `move assignment` paths). `std::variant::swap` and `std::variant::operator=` do *not* update this pointer automatically.
    * It is **never** swapped by `InlineBuf::swap` itself, as the buffers don't change owners during that operation.
* **Allocator Usage:** All element lifetime operations funnel through the private helpers `construct_at_`, `destroy_at_`, `destroy_n_`, which directly call `std::allocator_traits` methods on the container's `alloc_` member.
* **Exception Safety Mechanism:** Strong safety primarily relies on creating temporaries (`InlineBuf tmp` or `HeapVec new_vec`) and swapping them into place only upon success. Recovery from `valueless_by_exception` uses `recover_if_valueless_()` at the start of mutating operations.
* **C++17 Compatibility:** Uses a polyfill for `std::construct_at` (C++20 feature) to maintain C++17 support while using allocator-aware construction.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome. Please ensure:

1.  All tests pass (`./build/test_inlined_vector`).
2.  Sanitizer builds (ASan, UBSan) are clean.
3.  Code follows the existing style and C++17/20 standard.
4.  New functionality is documented in the README and header.

## Acknowledgments

Developed as part of the [lloyal.ai](https://lloyal.ai/) edge inference suite.

**Design inspiration:** This implementation studies the trade-offs in `absl::InlinedVector`, `boost::container::small_vector`, LLVM's `SmallVector`, and `ankerl::svector` to explore a modern C++17/20 architecture prioritizing zero dependencies and bidirectional storage transitions.