#include <benchmark/benchmark.h>
#include <vector>
#include <string>
#include <memory> // For std::unique_ptr

// The competitors
#include <inlined_vector.hpp> // Your v5.7
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

// =========================================================================
// BENCHMARK 1: Fill (push_back)
// Measures: Construction + N push_backs.
// This clearly shows the SBO performance "cliff" when state.range(0) > N.
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
        benchmark::ClobberMemory(); // Prevent optimization
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


// =========================================================================
// BENCHMARK 2: Reserve
// Measures: Cost of calling reserve(n).
// SBO containers should be a no-op for n <= N.
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
// Measures: Cost of copy-constructing a vector of size N.
// =========================================================================

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
// Measures: Cost of move-constructing a vector of size N.
// SBO containers will be O(N) (element-wise move) when inline,
// but O(1) (pointer swap) when on heap.
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

// =========================================================================
// BENCHMARK 5: Insert at Front
// Measures: Cost of inserting at begin(), forcing all elements to shift.
// This highlights the "rebuild-and-swap" cost for `lloyal` on the heap.
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
// Measures: Cost of erasing at begin(), forcing all elements to shift.
// This also highlights the "rebuild-and-swap" cost for `lloyal` on the heap.
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
// Measures: The ability of `lloyal::InlinedVector` to perform an `insert`
// on a non-assignable type when heap-allocated.
// =========================================================================

struct NonAssignable {
    const int val; // const makes it non-assignable
    NonAssignable(int v = 0) : val(v) {}

    // Movable and Copyable (constructors)
    NonAssignable(const NonAssignable&) = default;
    NonAssignable(NonAssignable&&) = default;

    // NOT Assignable
    NonAssignable& operator=(const NonAssignable&) = delete;
    NonAssignable& operator=(NonAssignable&&) = delete;
};

template <typename VecType>
static void BM_InsertFront_NonAssignable(benchmark::State& state) {
    // We only test a heap-allocated size
    const size_t n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        VecType vec;
        for(size_t i = 0; i < n; ++i) vec.emplace_back(i);
        state.ResumeTiming();
        
        // This operation will compile for lloyal::InlinedVector
        // but fail to compile for all others.
        vec.insert(vec.begin(), NonAssignable(42));
        benchmark::ClobberMemory();
    }
}

// NOTE: We only run this benchmark for lloyal::InlinedVector.
// The other lines are commented out because they will fail to compile,
// which *proves* the feature.
BENCHMARK_TEMPLATE(BM_InsertFront_NonAssignable, lloyal::InlinedVector<NonAssignable, kInlineCapacity>)
    ->Range(kInlineCapacity + 1, kInlineCapacity + 1);

// --- UNCOMMENT THE LINES BELOW TO VERIFY COMPILE-TIME FAILURE ---
// BENCHMARK_TEMPLATE(BM_InsertFront_NonAssignable, std::vector<NonAssignable>)
//     ->Range(kInlineCapacity + 1, kInlineCapacity + 1);
// BENCHMARK_TEMPLATE(BM_InsertFront_NonAssignable, absl::InlinedVector<NonAssignable, kInlineCapacity>)
//     ->Range(kInlineCapacity + 1, kInlineCapacity + 1);
// BENCHMARK_TEMPLATE(BM_InsertFront_NonAssignable, boost::container::small_vector<NonAssignable, kInlineCapacity>)
//     ->Range(kInlineCapacity + 1, kInlineCapacity + 1);