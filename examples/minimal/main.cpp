/**
 * @file main.cpp
 * @brief Minimal example demonstrating lloyal::InlinedVector usage
 */

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include "inlined_vector.hpp"

// ============================================================================
// Example 1: Basic Usage - Inline Storage
// ============================================================================
void example_basic_usage() {
    std::cout << "=== Example 1: Basic Usage ===\n";

    lloyal::InlinedVector<int, 4> vec;

    // These operations are all inline (no heap allocation)
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    vec.emplace_back(4);

    std::cout << "Size: " << vec.size() << ", Capacity: " << vec.capacity() << "\n";
    assert(vec.size() == 4);
    assert(vec.capacity() == 4); // Still inline

    // This triggers the transition to heap storage
    vec.push_back(5);

    std::cout << "After spill - Size: " << vec.size() << ", Capacity: " << vec.capacity() << "\n";
    assert(vec.size() == 5);
    assert(vec.capacity() > 4); // Now on the heap

    std::cout << "Contents: ";
    for (int val : vec) {
        std::cout << val << " ";
    }
    std::cout << "\n\n";
}

// ============================================================================
// Example 2: Strings with Insert/Erase
// ============================================================================
void example_strings() {
    std::cout << "=== Example 2: Strings with Insert/Erase ===\n";

    lloyal::InlinedVector<std::string, 8> vec;

    vec.push_back("hello");
    vec.push_back("world");
    vec.push_back("from");
    vec.push_back("lloyal");

    std::cout << "Initial size: " << vec.size() << "\n";

    // Insert works
    vec.insert(vec.begin() + 1, std::string("beautiful"));

    std::cout << "After insert: ";
    for (const auto& s : vec) {
        std::cout << s << " ";
    }
    std::cout << "\n";

    // Erase works
    vec.erase(vec.begin() + 2);

    std::cout << "After erase: ";
    for (const auto& s : vec) {
        std::cout << s << " ";
    }
    std::cout << "\n\n";
}

// ============================================================================
// Example 3: Move-Only Types (unique_ptr)
// ============================================================================
void example_move_only_types() {
    std::cout << "=== Example 3: Move-Only Types ===\n";

    lloyal::InlinedVector<std::unique_ptr<std::string>, 4> vec;

    // Add some unique_ptrs
    vec.emplace_back(std::make_unique<std::string>("Resource 1"));
    vec.emplace_back(std::make_unique<std::string>("Resource 2"));
    vec.emplace_back(std::make_unique<std::string>("Resource 3"));

    std::cout << "Resource count: " << vec.size() << "\n";
    std::cout << "Resources:\n";
    for (const auto& ptr : vec) {
        std::cout << "  - " << *ptr << "\n";
    }

    // Move a resource out
    auto resource = std::move(vec[1]);
    std::cout << "Moved resource: " << *resource << "\n\n";
}

// ============================================================================
// Example 4: Heap-to-Inline Transition (shrink_to_fit)
// ============================================================================
void example_heap_to_inline_transition() {
    std::cout << "=== Example 4: Heap-to-Inline Transition ===\n";

    lloyal::InlinedVector<std::string, 8> vec;

    // Start small (inline)
    vec.push_back("a");
    vec.push_back("b");
    std::cout << "Initial: size=" << vec.size() << ", capacity=" << vec.capacity() << "\n";

    // Grow beyond inline capacity (spill to heap)
    for (int i = 0; i < 20; ++i) {
        vec.push_back("item_" + std::to_string(i));
    }
    std::cout << "After growth: size=" << vec.size() << ", capacity=" << vec.capacity() << "\n";

    // Shrink back down
    vec.resize(6);
    std::cout << "After resize(6): size=" << vec.size() << ", capacity=" << vec.capacity() << "\n";

    // Return to inline storage!
    vec.shrink_to_fit();
    std::cout << "After shrink_to_fit: size=" << vec.size() << ", capacity=" << vec.capacity() << "\n";
    assert(vec.capacity() == 8); // Back to inline capacity

    std::cout << "Successfully returned to inline storage!\n\n";
}

// ============================================================================
// Example 5: Const Members (Compile-Time Demonstration)
// ============================================================================
struct Record {
    const uint64_t id;  // Immutable ID
    std::string data;   // Mutable data

    Record(uint64_t record_id, std::string record_data)
        : id(record_id), data(std::move(record_data)) {}
};

void example_const_members() {
    std::cout << "=== Example 5: Types with Const Members ===\n";

    lloyal::InlinedVector<Record, 8> records;

    // push_back and emplace_back work fine with const members
    records.emplace_back(1, "First record");
    records.emplace_back(2, "Second record");
    records.emplace_back(3, "Third record");

    std::cout << "Records:\n";
    for (const auto& rec : records) {
        std::cout << "  [" << rec.id << "] " << rec.data << "\n";
    }

    // Note: insert/erase with const members work on heap, but may have
    // limitations in some edge cases. The key benefit is that the types
    // compile and work for push_back/emplace_back operations.

    std::cout << "\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "lloyal::InlinedVector Examples\n";
    std::cout << "================================\n\n";

    try {
        example_basic_usage();
        example_strings();
        example_move_only_types();
        example_heap_to_inline_transition();
        example_const_members();

        std::cout << "✓ All examples completed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
