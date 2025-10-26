#include <benchmark/benchmark.h>
#include <vector>
#include <string>
#include <memory> // For std::unique_ptr

// The competitors
#include "inlined_vector.hpp" // Your v5.7+
#include "absl/container/inlined_vector.h"
#include "boost/container/small_vector.hpp"

// --- Configuration ---

// The inline size N we will test for all SBO containers
constexpr size_t kInlineCapacity = 16;

// The types we will test
using TrivialType = uint64_t;
using ComplexType = std::string;
using MoveOnlyType = std::unique_ptr<int>;

// Use a static value to prevent optimization
static TrivialType g_trivial_val = 42;
static ComplexType g_complex_val = "hello world a longer string";
static auto g_move_val = [] { return std::make_unique<int>(42); };

// --- Simple Benchmark Allocator ---
// (Minimal overhead, no tracking, stateful via ID)
template <typename T> struct BenchAllocator {
    using value_type = T;
    int id = 0; // State to make it non-trivial

    BenchAllocator(int i = 0) noexcept : id(i) {}
    template <typename U> BenchAllocator(const BenchAllocator<U>& other) noexcept : id(other.id) {}

    T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }

    // Use default construct/destroy from std::allocator_traits
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::false_type;
    using is_always_equal = std::false_type; // Not always equal due to id

    friend bool operator==(const BenchAllocator& a, const BenchAllocator& b) { return a.id == b.id; }
    friend bool operator!=(const BenchAllocator& a, const BenchAllocator& b) { return a.id != b.id; }
};

// =========================================================================
// BENCHMARK 1: Fill (push_back)
// =========================================================================

// --- Trivial Type: uint64_t ---
template <typename VecType>
static void BM_Fill_Trivial(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        VecType vec;
        for (size_t i = 0; i < n; ++i) {
            vec.push_back(g_trivial_val);
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_Fill_Trivial, std::vector<TrivialType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Trivial, lloyal::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Trivial, absl::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Trivial, boost::container::small_vector<TrivialType, kInlineCapacity>)->Range(1, 128);

// --- Complex Type: std::string ---
template <typename VecType>
static void BM_Fill_Complex(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        VecType vec;
        for (size_t i = 0; i < n; ++i) {
            vec.push_back(g_complex_val);
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_Fill_Complex, std::vector<ComplexType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Complex, lloyal::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Complex, absl::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Complex, boost::container::small_vector<ComplexType, kInlineCapacity>)->Range(1, 128);

// --- Complex Type with Custom Allocator ---
template <typename VecType>
static void BM_Fill_Complex_Alloc(benchmark::State& state) {
    const size_t n = state.range(0);
    BenchAllocator<ComplexType> alloc(1); // Create allocator instance
    for (auto _ : state) {
        VecType vec(alloc); // Construct with allocator
        for (size_t i = 0; i < n; ++i) {
            vec.push_back(g_complex_val);
        }
        benchmark::ClobberMemory();
    }
}
// Compare lloyal vs std::vector with the same custom allocator
BENCHMARK_TEMPLATE(BM_Fill_Complex_Alloc, std::vector<ComplexType, BenchAllocator<ComplexType>>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Fill_Complex_Alloc, lloyal::InlinedVector<ComplexType, kInlineCapacity, BenchAllocator<ComplexType>>)->Range(1, 128);


// =========================================================================
// BENCHMARK 2: Reserve
// =========================================================================

template <typename VecType>
static void BM_Reserve(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        VecType vec;
        benchmark::DoNotOptimize(vec.data());
        vec.reserve(n);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_Reserve, std::vector<TrivialType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Reserve, lloyal::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Reserve, absl::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_Reserve, boost::container::small_vector<TrivialType, kInlineCapacity>)->Range(1, 128);

// =========================================================================
// BENCHMARK 3: Copy Construction
// =========================================================================

// NOTE: Previous runs showed potential timer issues with this benchmark across all types.
// Keeping it for comparison, but results may need scrutiny.
template <typename VecType>
static void BM_CopyConstruct(benchmark::State& state) {
    const size_t n = state.range(0);
    state.PauseTiming();
    VecType source_vec;
    for(size_t i = 0; i < n; ++i) source_vec.push_back(g_complex_val);
    state.ResumeTiming();

    for (auto _ : state) {
        VecType copy_vec(source_vec);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_CopyConstruct, std::vector<ComplexType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_CopyConstruct, lloyal::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_CopyConstruct, absl::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_CopyConstruct, boost::container::small_vector<ComplexType, kInlineCapacity>)->Range(1, 128);

// =========================================================================
// BENCHMARK 4: Move Construction
// =========================================================================

template <typename VecType>
static void BM_MoveConstruct(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType source_vec;
        for(size_t i = 0; i < n; ++i) source_vec.push_back(g_complex_val);
        state.ResumeTiming();

        VecType move_vec(std::move(source_vec));
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_MoveConstruct, std::vector<ComplexType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_MoveConstruct, lloyal::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_MoveConstruct, absl::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_MoveConstruct, boost::container::small_vector<ComplexType, kInlineCapacity>)->Range(1, 128);

// --- Move Construction with Custom Allocator ---
template <typename VecType>
static void BM_MoveConstruct_Alloc(benchmark::State& state) {
    const size_t n = state.range(0);
    BenchAllocator<ComplexType> alloc(1);
    for (auto _ : state) {
        state.PauseTiming();
        VecType source_vec(alloc);
        for(size_t i = 0; i < n; ++i) source_vec.push_back(g_complex_val);
        state.ResumeTiming();

        VecType move_vec(std::move(source_vec)); // Allocator should propagate (POCMA=true)
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_MoveConstruct_Alloc, std::vector<ComplexType, BenchAllocator<ComplexType>>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_MoveConstruct_Alloc, lloyal::InlinedVector<ComplexType, kInlineCapacity, BenchAllocator<ComplexType>>)->Range(1, 128);


// =========================================================================
// BENCHMARK 5: Insert at Front
// =========================================================================

// --- Trivial Type ---
template <typename VecType>
static void BM_InsertFront_Trivial(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.push_back(g_trivial_val);
        state.ResumeTiming();
        
        vec.insert(vec.begin(), g_trivial_val);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_InsertFront_Trivial, std::vector<TrivialType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Trivial, lloyal::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Trivial, absl::InlinedVector<TrivialType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Trivial, boost::container::small_vector<TrivialType, kInlineCapacity>)->Range(1, 128);

// --- Complex Type ---
template <typename VecType>
static void BM_InsertFront_Complex(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.push_back(g_complex_val);
        state.ResumeTiming();
        
        vec.insert(vec.begin(), g_complex_val);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_InsertFront_Complex, std::vector<ComplexType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Complex, lloyal::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Complex, absl::InlinedVector<ComplexType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Complex, boost::container::small_vector<ComplexType, kInlineCapacity>)->Range(1, 128);

// --- Complex Type with Custom Allocator ---
template <typename VecType>
static void BM_InsertFront_Complex_Alloc(benchmark::State& state) {
    const size_t n = state.range(0);
    BenchAllocator<ComplexType> alloc(1);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec(alloc);
        for(size_t i = 0; i < n; ++i) vec.push_back(g_complex_val);
        state.ResumeTiming();

        vec.insert(vec.begin(), g_complex_val);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_InsertFront_Complex_Alloc, std::vector<ComplexType, BenchAllocator<ComplexType>>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_Complex_Alloc, lloyal::InlinedVector<ComplexType, kInlineCapacity, BenchAllocator<ComplexType>>)->Range(1, 128);

// --- Move-Only Type ---
template <typename VecType>
static void BM_InsertFront_MoveOnly(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.push_back(g_move_val());
        state.ResumeTiming();
        
        vec.insert(vec.begin(), g_move_val());
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_InsertFront_MoveOnly, std::vector<MoveOnlyType>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_MoveOnly, lloyal::InlinedVector<MoveOnlyType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_MoveOnly, absl::InlinedVector<MoveOnlyType, kInlineCapacity>)->Range(1, 128);
BENCHMARK_TEMPLATE(BM_InsertFront_MoveOnly, boost::container::small_vector<MoveOnlyType, kInlineCapacity>)->Range(1, 128);

// =========================================================================
// BENCHMARK 6: Erase from Front
// =========================================================================

// --- Trivial Type ---
template <typename VecType>
static void BM_EraseFront_Trivial(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.push_back(g_trivial_val);
        state.ResumeTiming();
        
        if (!vec.empty()) {
            vec.erase(vec.begin());
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_EraseFront_Trivial, std::vector<TrivialType>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Trivial, lloyal::InlinedVector<TrivialType, kInlineCapacity>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Trivial, absl::InlinedVector<TrivialType, kInlineCapacity>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Trivial, boost::container::small_vector<TrivialType, kInlineCapacity>)->Range(2, 128);

// --- Complex Type ---
template <typename VecType>
static void BM_EraseFront_Complex(benchmark::State& state) {
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.push_back(g_complex_val);
        state.ResumeTiming();
        
        if (!vec.empty()) {
            vec.erase(vec.begin());
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK_TEMPLATE(BM_EraseFront_Complex, std::vector<ComplexType>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Complex, lloyal::InlinedVector<ComplexType, kInlineCapacity>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Complex, absl::InlinedVector<ComplexType, kInlineCapacity>)->Range(2, 128);
BENCHMARK_TEMPLATE(BM_EraseFront_Complex, boost::container::small_vector<ComplexType, kInlineCapacity>)->Range(2, 128);

// =========================================================================
// BENCHMARK 7: Non-Assignable Type Insert (The "Killer Feature")
// =========================================================================

struct NonAssignable {
    const int val;
    NonAssignable(int v = 0) : val(v) {}
    NonAssignable(const NonAssignable&) = default;
    NonAssignable(NonAssignable&&) = default;
    NonAssignable& operator=(const NonAssignable&) = delete;
    NonAssignable& operator=(NonAssignable&&) = delete;
};

template <typename VecType>
static void BM_InsertFront_NonAssignable(benchmark::State& state) {
    const size_t n = state.range(0); // Test both inline and heap
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.emplace_back(i); // Use emplace_back
        state.ResumeTiming();
        
        vec.insert(vec.begin(), NonAssignable(42));
        benchmark::ClobberMemory();
    }
}

// ** FIX: Run for both inline (N/2) and heap (N+1) sizes **
BENCHMARK_TEMPLATE(BM_InsertFront_NonAssignable, lloyal::InlinedVector<NonAssignable, kInlineCapacity>)
    ->Ranges({{kInlineCapacity / 2, kInlineCapacity / 2}, {kInlineCapacity + 1, kInlineCapacity + 1}});

// (Other implementations still commented out as they won't compile)

// =========================================================================
// BENCHMARK 8: Shrink To Fit (Heap -> Inline Transition)
// =========================================================================

template <typename VecType>
static void BM_ShrinkToFit(benchmark::State& state) {
    // Start slightly above inline capacity, shrink back down
    const size_t start_size = kInlineCapacity + 5;
    const size_t end_size = kInlineCapacity / 2;

    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < start_size; ++i) vec.push_back(g_complex_val);
        vec.resize(end_size); // Resize down while still on heap
        state.ResumeTiming();

        vec.shrink_to_fit(); // The operation to measure
        benchmark::ClobberMemory();
    }
}
// std::vector might reallocate, lloyal will move elements and deallocate
BENCHMARK_TEMPLATE(BM_ShrinkToFit, std::vector<ComplexType>);
BENCHMARK_TEMPLATE(BM_ShrinkToFit, lloyal::InlinedVector<ComplexType, kInlineCapacity>);
// Note: absl/boost do not transition back to inline, so their shrink_to_fit is different


// --- Main ---
BENCHMARK_MAIN();