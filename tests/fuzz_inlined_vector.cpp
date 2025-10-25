// FuzzTest harness for InlinedVector v5.5+
// Uses Google FuzzTest for property-based testing

#include <inlined_vector.hpp>
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <vector>
#include <algorithm>

namespace {

// Property: push_back maintains size invariant
void PushBackMaintainsSize(const std::vector<int>& values) {
    lloyal::InlinedVector<int, 4> vec;

    for (int val : values) {
        size_t old_size = vec.size();
        vec.push_back(val);
        ASSERT_EQ(vec.size(), old_size + 1);
    }

    ASSERT_EQ(vec.size(), values.size());
}
FUZZ_TEST(InlinedVectorFuzz, PushBackMaintainsSize);

// Property: copy produces identical container
void CopyProducesIdentical(const std::vector<int>& values) {
    lloyal::InlinedVector<int, 4> original;
    for (int val : values) {
        original.push_back(val);
    }

    auto copy = original;

    ASSERT_EQ(original.size(), copy.size());
    ASSERT_TRUE(std::equal(original.begin(), original.end(), copy.begin()));
}
FUZZ_TEST(InlinedVectorFuzz, CopyProducesIdentical);

// Property: insert at valid position doesn't corrupt container
void InsertMaintainsInvariants(const std::vector<int>& initial,
                                const std::vector<int>& to_insert) {
    lloyal::InlinedVector<int, 4> vec;
    for (int val : initial) {
        vec.push_back(val);
    }

    for (int val : to_insert) {
        if (vec.empty()) {
            vec.push_back(val);
        } else {
            // Insert at random valid position (use value as seed)
            size_t pos = static_cast<size_t>(std::abs(val)) % (vec.size() + 1);
            size_t old_size = vec.size();
            vec.insert(vec.begin() + pos, val);
            ASSERT_EQ(vec.size(), old_size + 1);
        }
    }

    // Verify iterators work
    ASSERT_EQ(vec.end() - vec.begin(), static_cast<std::ptrdiff_t>(vec.size()));
}
FUZZ_TEST(InlinedVectorFuzz, InsertMaintainsInvariants);

// Property: erase maintains size invariant
void EraseMaintainsSize(const std::vector<int>& values) {
    if (values.empty()) return;

    lloyal::InlinedVector<int, 4> vec;
    for (int val : values) {
        vec.push_back(val);
    }

    // Erase first element repeatedly
    while (!vec.empty()) {
        size_t old_size = vec.size();
        vec.erase(vec.begin());
        ASSERT_EQ(vec.size(), old_size - 1);
    }

    ASSERT_TRUE(vec.empty());
}
FUZZ_TEST(InlinedVectorFuzz, EraseMaintainsSize);

// Property: clear always produces empty container
void ClearProducesEmpty(const std::vector<int>& values) {
    lloyal::InlinedVector<int, 4> vec;
    for (int val : values) {
        vec.push_back(val);
    }

    vec.clear();

    ASSERT_EQ(vec.size(), 0);
    ASSERT_TRUE(vec.empty());
    ASSERT_EQ(vec.begin(), vec.end());
}
FUZZ_TEST(InlinedVectorFuzz, ClearProducesEmpty);

// Property: swap exchanges contents
void SwapExchangesContents(const std::vector<int>& values_a,
                            const std::vector<int>& values_b) {
    lloyal::InlinedVector<int, 4> vec_a, vec_b;

    for (int val : values_a) vec_a.push_back(val);
    for (int val : values_b) vec_b.push_back(val);

    size_t size_a = vec_a.size();
    size_t size_b = vec_b.size();

    std::vector<int> copy_a(vec_a.begin(), vec_a.end());
    std::vector<int> copy_b(vec_b.begin(), vec_b.end());

    vec_a.swap(vec_b);

    ASSERT_EQ(vec_a.size(), size_b);
    ASSERT_EQ(vec_b.size(), size_a);
    ASSERT_TRUE(std::equal(vec_a.begin(), vec_a.end(), copy_b.begin()));
    ASSERT_TRUE(std::equal(vec_b.begin(), vec_b.end(), copy_a.begin()));
}
FUZZ_TEST(InlinedVectorFuzz, SwapExchangesContents);

// Property: inline<->heap transitions preserve contents
void TransitionsPreserveContents(const std::vector<int>& values) {
    lloyal::InlinedVector<int, 4> vec;
    std::vector<int> reference;

    for (int val : values) {
        vec.push_back(val);
        reference.push_back(val);

        // Verify after each insertion
        ASSERT_TRUE(std::equal(vec.begin(), vec.end(), reference.begin()));
    }
}
FUZZ_TEST(InlinedVectorFuzz, TransitionsPreserveContents);

// Property: element access is consistent
void ElementAccessConsistent(const std::vector<int>& values) {
    if (values.empty()) return;

    lloyal::InlinedVector<int, 4> vec;
    for (int val : values) {
        vec.push_back(val);
    }

    // Verify operator[] matches iterator access
    for (size_t i = 0; i < vec.size(); ++i) {
        ASSERT_EQ(vec[i], *(vec.begin() + i));
    }
}
FUZZ_TEST(InlinedVectorFuzz, ElementAccessConsistent);

} // namespace
