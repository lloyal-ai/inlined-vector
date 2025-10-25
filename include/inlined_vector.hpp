/**
 * @file inlined_vector.hpp
 * @brief Defines lloyal::InlinedVector, a std::vector-like container with
 * Small Buffer Optimization (SBO) and full allocator support.
 *
 * @version 5.7 (Production Ready)
 * @date 2025-01-26
 * @copyright Copyright (C) 2025 Lloyal AI - MIT License
 */

#pragma once

#include <algorithm> // For std::min, std::equal, std::lexicographical_compare
#include <cassert>
#include <cstddef>
#include <cstring>   // For std::memmove, std::memcpy
#include <iterator>  // For std::reverse_iterator, std::distance, std::make_move_iterator
#include <limits>    // For std::numeric_limits
#include <memory>    // For std::allocator, std::allocator_traits, std::to_address
#include <new>       // For std::launder
#include <stdexcept> // For std::out_of_range
#include <type_traits> // For type traits used throughout
#include <utility>   // For std::swap, std::move, std::forward
#include <variant>   // For std::variant, std::get_if, std::holds_alternative
#include <vector>    // For std::vector (heap storage backend)

// [[no_unique_address]] requires C++20
#if __cplusplus >= 202002L
#define LLOYAL_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define LLOYAL_NO_UNIQUE_ADDRESS
#endif


namespace lloyal {

/**
 * @brief A std::vector-like container optimized for small sizes using
 * Small Buffer Optimization (SBO).
 *
 * `InlinedVector` stores up to `N` elements directly within its own footprint
 * (inline storage) without requiring heap allocation. If the number of elements
 * exceeds `N`, it automatically transitions to using heap storage, behaving
 * like `std::vector<T, Alloc>`.
 *
 * This container provides full allocator support via `std::allocator_traits`,
 * correctly handling `std::uses_allocator` construction for elements stored
 * both inline and on the heap. It supports custom allocators, including
 * polymorphic memory resources (`std::pmr`).
 *
 * @tparam T The type of elements stored. Must satisfy the requirements of
 * Erasable, MoveConstructible, and (for copy operations) CopyConstructible
 * from the chosen Allocator. Must be a non-const, non-volatile object type.
 * @tparam N The number of elements to store inline. Must be greater than 0.
 * This defines the threshold for switching to heap allocation.
 * @tparam Alloc The allocator type. Defaults to `std::allocator<T>`.
 *
 * @note Exception Safety: Provides the strong exception safety guarantee for most
 * operations if `T`'s move/swap operations are `noexcept` or if copy operations
 * do not throw. Otherwise, provides the basic guarantee (container remains
 * in a valid state). See `strong_exception_guarantee` static member.
 *
 * @note Iterator Invalidation: Follows `std::vector` rules when operating solely
 * within inline storage or solely within heap storage (e.g., `insert`/`erase`
 * invalidates at and after the operation point).
 * **Critical:** *All* iterators, pointers, and references are invalidated when
 * the container transitions between inline and heap storage (e.g., during
 * `reserve`, `shrink_to_fit`, or a `push_back`/`insert` that crosses capacity `N`).
 *
 * @note Non-Assignable Types: Supports non-assignable (but MoveConstructible) types
 * for `insert` and `erase` operations in both inline and heap modes by using
 * internal rebuild-and-swap logic instead of direct assignment where necessary.
 */
template<typename T, std::size_t N, typename Alloc = std::allocator<T>>
class InlinedVector {
public:
    // --- Compile-Time Constraints ---
    static_assert(N > 0, "InlinedVector requires an inline capacity N > 0");
    static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "InlinedVector requires T to be a non-cv object type");
    static_assert(std::is_move_constructible_v<T>, "InlinedVector requires T to be MoveConstructible");

    // --- Public Member Types ---
    using value_type = T;
    using allocator_type = Alloc;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // --- Static Constants ---
    /** @brief The number of elements that can be stored inline without heap allocation. */
    static constexpr size_t inline_capacity = N;

    /**
     * @brief A compile-time constant indicating whether operations generally
     * provide the strong exception guarantee, based on `T`'s move/swap properties.
     */
    static constexpr bool strong_exception_guarantee =
        std::is_nothrow_move_assignable_v<T> ||
        (std::is_nothrow_swappable_v<T> && std::is_nothrow_move_constructible_v<T>);


private:
    // --- Private Member Types ---
    using AllocTraits = std::allocator_traits<Alloc>;
    using HeapVec = std::vector<T, Alloc>;

    // ========================================================================
    // InlineBuf: Internal POD struct holding the inline buffer and size.
    // Provides RAII semantics (ctor/dtor/swap) for exception safety during
    // slow-path rebuilds in `insert` and `erase`.
    // ========================================================================
    struct InlineBuf {
        alignas(T) std::byte buf[sizeof(T) * N];
        size_type size = 0;

        InlineBuf() noexcept = default;
        pointer ptr() noexcept { return std::launder(reinterpret_cast<pointer>(buf)); }
        const_pointer ptr() const noexcept { return std::launder(reinterpret_cast<const_pointer>(buf)); }

        ~InlineBuf() { if constexpr (!std::is_trivially_destructible_v<T>) std::destroy_n(ptr(), size); }

        InlineBuf(const InlineBuf& other) : size(0) {
            pointer d = ptr(); const_pointer s = other.ptr(); size_type i = 0;
            try { for (; i < other.size; ++i) std::construct_at(d + i, s[i]); size = other.size; }
            catch (...) { std::destroy_n(d, i); throw; }
        }

        InlineBuf(InlineBuf&& other) noexcept(std::is_nothrow_move_constructible_v<T>) : size(0) {
            pointer d = ptr(); pointer s = other.ptr(); size_type i = 0;
            try { for (; i < other.size; ++i) std::construct_at(d + i, std::move(s[i])); size = other.size; other.clear(); }
            catch (...) { std::destroy_n(d, i); throw; } // Always re-throw
        }

        InlineBuf& operator=(const InlineBuf& other) { if (this != &other) { InlineBuf temp(other); swap(temp); } return *this; }
        InlineBuf& operator=(InlineBuf&& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T>) {
            if (this != &other) { InlineBuf temp_buf(std::move(other)); swap(temp_buf); } return *this;
        }

        void clear() noexcept { if constexpr (!std::is_trivially_destructible_v<T>) std::destroy_n(ptr(), size); size = 0; }

        void swap(InlineBuf& other) noexcept(std::is_nothrow_swappable_v<T> && std::is_nothrow_move_constructible_v<T>) {
            using std::swap; size_type min_sz = std::min(size, other.size);
            for (size_type i = 0; i < min_sz; ++i) swap(ptr()[i], other.ptr()[i]);
            if (size > other.size) {
                for (size_type i = min_sz; i < size; ++i) { std::construct_at(other.ptr() + i, std::move(ptr()[i])); std::destroy_at(ptr() + i); }
            } else if (other.size > size) {
                for (size_type i = min_sz; i < other.size; ++i) { std::construct_at(ptr() + i, std::move(other.ptr()[i])); std::destroy_at(other.ptr() + i); }
            }
            swap(size, other.size);
        }
    };

    // --- Core State ---
    using Storage = std::variant<InlineBuf, HeapVec>;
    mutable Storage storage_; // Holds either InlineBuf or HeapVec. Mutable for recovery.
    LLOYAL_NO_UNIQUE_ADDRESS allocator_type alloc_; // The allocator instance.

    // ========================================================================
    // Allocator Traits Helpers (operate on raw T*)
    // ========================================================================
    /** @brief Constructs an object at `p` using the container's allocator. */
    template<class... Args> T* construct_at_(T* p, Args&&... args) { AllocTraits::construct(alloc_, p, std::forward<Args>(args)...); return p; }
    /** @brief Destroys an object at `p` using the container's allocator. */
    void destroy_at_(T* p) noexcept { AllocTraits::destroy(alloc_, p); }
    /** @brief Destroys `n` objects starting at `p` using the container's allocator. */
    void destroy_n_(T* p, size_type n) noexcept { for (size_type i = 0; i < n; ++i) AllocTraits::destroy(alloc_, p + i); }

    // ========================================================================
    // Helper methods for state management and optimization
    // ========================================================================

    /** @brief Returns a pointer to static, aligned, non-constructed storage. Used as a non-null sentinel for empty ranges. */
    static std::byte* empty_bytes_() noexcept { alignas(T) static std::byte s_empty_buf[sizeof(T)]; return s_empty_buf; }
    /** @brief Returns a `const_pointer` sentinel for empty ranges. */
    static const T* empty_sentinel_c() noexcept { return reinterpret_cast<const T*>(empty_bytes_()); }
    /** @brief Returns a `pointer` sentinel for empty ranges. */
    static T* empty_sentinel() noexcept { return reinterpret_cast<T*>(empty_bytes_()); }

    /** @brief Checks if the variant storage is in the valueless_by_exception state. */
    bool is_valueless_() const noexcept { return storage_.valueless_by_exception(); }

    /**
     * @brief Recovers from the valueless_by_exception state by emplacing an empty InlineBuf.
     * @warning Only call from non-const mutating methods. Ensures basic guarantee.
     */
    void recover_if_valueless_() const noexcept { if (is_valueless_()) storage_.template emplace<InlineBuf>(); }

    /** @brief Checks if storage is currently inline (or valueless, treated as inline). Non-mutating. */
    bool is_inline() const noexcept { if (is_valueless_()) return true; return std::holds_alternative<InlineBuf>(storage_); }

    /** @brief Gets a pointer to the beginning of the storage. Recovers if valueless. Returns sentinel if empty. */
    pointer data() noexcept {
        recover_if_valueless_(); // Ensure valid state before access
        if (auto* buf = std::get_if<InlineBuf>(&storage_)) return buf->size == 0 ? empty_sentinel() : buf->ptr();
        auto& vec = std::get<HeapVec>(storage_); return vec.empty() ? empty_sentinel() : vec.data();
    }
    /** @brief Gets a const pointer to the beginning of the storage. Returns sentinel if empty or valueless. Non-mutating. */
    const_pointer data() const noexcept {
        if (is_valueless_()) return empty_sentinel_c();
        if (const auto* buf = std::get_if<InlineBuf>(&storage_)) return buf->size == 0 ? empty_sentinel_c() : buf->ptr();
        const auto& vec = std::get<HeapVec>(storage_); return vec.empty() ? empty_sentinel_c() : vec.data();
    }

public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /** @brief Constructs an empty InlinedVector using the specified allocator. */
    explicit InlinedVector(const Alloc& alloc = Alloc{}) noexcept
        : storage_(std::in_place_type<InlineBuf>), alloc_(alloc) {}

    /** @brief Destroys the InlinedVector, clearing its contents. */
    ~InlinedVector() { clear(); } // Delegates destruction logic to clear()

    /** @brief Copy constructor. Uses the source allocator according to allocator traits. */
    InlinedVector(const InlinedVector& other)
        : alloc_(AllocTraits::select_on_container_copy_construction(other.alloc_)),
          storage_(std::in_place_type<InlineBuf>)
    {
        const size_type n = other.size();
        if (n == 0) return;
        if (n <= N) {
            auto& buf = std::get<InlineBuf>(storage_);
            pointer d = buf.ptr();
            const_pointer s = other.data();
            size_type i = 0;
            try {
                for (; i < n; ++i) construct_at_(d + i, s[i]);
                buf.size = n;
            } catch (...) { destroy_n_(d, i); throw; }
        } else {
            HeapVec vec(alloc_);
            vec.reserve(n);
            const_pointer s = other.data();
            try {
                for (size_type i = 0; i < n; ++i) vec.emplace_back(s[i]);
                storage_ = std::move(vec);
            } catch (...) { throw; } // vec dtor handles cleanup
        }
    }
    /** @brief Copy constructor using an explicitly provided allocator. */
    InlinedVector(const InlinedVector& other, const Alloc& alloc)
        : alloc_(alloc), storage_(std::in_place_type<InlineBuf>)
    {
        const size_type n = other.size();
        if (n == 0) return;
        if (n <= N) {
             auto& buf = std::get<InlineBuf>(storage_);
             pointer d = buf.ptr();
             const_pointer s = other.data();
             size_type i = 0;
             try { for (; i < n; ++i) construct_at_(d + i, s[i]); buf.size = n; }
             catch (...) { destroy_n_(d, i); throw; }
        } else {
             HeapVec vec(alloc_);
             vec.reserve(n);
             const_pointer s = other.data();
             try { for (size_type i = 0; i < n; ++i) vec.emplace_back(s[i]); storage_ = std::move(vec); }
             catch (...) { throw; }
        }
    }

    /** @brief Move constructor. Propagates allocator according to traits. */
    InlinedVector(InlinedVector&& other)
        noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<Alloc>)
        : alloc_(std::move(other.alloc_)), storage_(std::in_place_type<InlineBuf>)
    {
        if (other.is_inline()) {
            auto& other_buf = std::get<InlineBuf>(other.storage_);
            if (other_buf.size > 0) {
                auto& this_buf = std::get<InlineBuf>(storage_);
                pointer d = this_buf.ptr();
                pointer s = other_buf.ptr();
                size_type count = other_buf.size;
                size_type i = 0;
                try {
                    for (; i < count; ++i) construct_at_(d + i, std::move(s[i]));
                    this_buf.size = count;
                    other_buf.clear(); // Destroys moved-from elements in source
                } catch(...) {
                    destroy_n_(d, i);
                    throw; // Always re-throw
                }
            }
        } else {
            storage_ = std::move(other.storage_); // Steal vector
            other.storage_.template emplace<InlineBuf>(); // Reset source
        }
    }
    /** @brief Move constructor using an explicitly provided allocator. Steals resources only if allocators compare equal. */
    InlinedVector(InlinedVector&& other, const Alloc& alloc)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : alloc_(alloc), storage_(std::in_place_type<InlineBuf>)
    {
         if (alloc_ == other.alloc_) {
             if (other.is_inline()) {
                 auto& other_buf = std::get<InlineBuf>(other.storage_);
                 if (other_buf.size > 0) {
                      auto& this_buf = std::get<InlineBuf>(storage_);
                      pointer d = this_buf.ptr();
                      pointer s = other_buf.ptr();
                      size_type count = other_buf.size;
                      size_type i = 0;
                      try {
                          for (; i < count; ++i) construct_at_(d + i, std::move(s[i]));
                          this_buf.size = count;
                          other_buf.clear();
                      } catch(...) {
                          destroy_n_(d, i);
                          throw; // Always re-throw
                      }
                 }
             } else {
                 storage_ = std::move(other.storage_);
                 other.storage_.template emplace<InlineBuf>();
             }
         } else {
             // Allocators differ, must move elements individually
             const size_type n = other.size();
             if (n == 0) return;
             if (n <= N) {
                  auto& this_buf = std::get<InlineBuf>(storage_);
                  pointer d = this_buf.ptr();
                  size_type i = 0;
                  try {
                      for (; i < n; ++i) construct_at_(d + i, std::move(other[i]));
                      this_buf.size = n;
                      other.clear();
                  } catch(...) {
                      destroy_n_(d, i);
                      throw; // Always re-throw
                  }
             } else {
                  HeapVec vec(alloc_);
                  vec.reserve(n);
                  try {
                      for (size_type i = 0; i < n; ++i) vec.emplace_back(std::move(other[i]));
                      storage_ = std::move(vec);
                      other.clear();
                  } catch(...) {
                      throw; // Always re-throw
                  }
             }
         }
    }

    /** @brief Copy assignment operator. Handles allocator propagation according to traits. */
    InlinedVector& operator=(const InlinedVector& other) {
        if (this == &other) return *this;
        using POCMA = typename AllocTraits::propagate_on_container_copy_assignment;
        if constexpr (POCMA::value) {
            if (alloc_ != other.alloc_) {
                clear(); // destroy with old allocator
            }
            alloc_ = other.alloc_; // propagate allocator
        }
        // Rebuild implementation ensures correctness
        clear();
        const size_type n = other.size();
        if (n == 0) {
            storage_.template emplace<InlineBuf>(); // Ensure inline
            return *this;
        }
        if (n <= N) {
            // Construct into inline storage
            storage_.template emplace<InlineBuf>();
            auto& buf = std::get<InlineBuf>(storage_);
            pointer d = buf.ptr();
            const_pointer s = other.data();
            size_type i = 0;
            try {
                for (; i < n; ++i) construct_at_(d + i, s[i]);
                buf.size = n;
            } catch (...) {
                destroy_n_(d, i);
                throw;
            }
        } else {
            // Construct into new heap vector
            HeapVec vec(alloc_);
            vec.reserve(n);
            const_pointer s = other.data();
            try {
                for (size_type i = 0; i < n; ++i) vec.emplace_back(s[i]);
                storage_ = std::move(vec);
            } catch (...) {
                throw;
            }
        }
        return *this;
    }
    /** @brief Move assignment operator. Handles allocator propagation according to traits. */
    InlinedVector& operator=(InlinedVector&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T> &&
        std::is_nothrow_move_assignable_v<Alloc> && std::is_nothrow_swappable_v<Alloc>
    ) {
         if (this != &other) {
             using POCMA = typename AllocTraits::propagate_on_container_move_assignment;
             using IsAE = typename AllocTraits::is_always_equal;
             if constexpr (POCMA::value) {
                 clear();
                 alloc_ = std::move(other.alloc_);
                 storage_ = std::move(other.storage_);
                 other.storage_.template emplace<InlineBuf>();
             } else if constexpr (IsAE::value) {
                 clear();
                 storage_ = std::move(other.storage_);
                 other.storage_.template emplace<InlineBuf>();
             } else {
                  if (alloc_ == other.alloc_) {
                      clear();
                      storage_ = std::move(other.storage_);
                      other.storage_.template emplace<InlineBuf>();
                  } else { // Element-wise move
                       clear();
                       const size_type n = other.size();
                       if (n == 0) { other.clear(); return *this; }
                       if (n <= N) {
                           storage_.template emplace<InlineBuf>();
                           auto& buf = std::get<InlineBuf>(storage_);
                           pointer d = buf.ptr();
                           size_type i = 0;
                           try {
                               for (; i < n; ++i) construct_at_(d + i, std::move(other[i]));
                               buf.size = n;
                               other.clear();
                           } catch(...) {
                               destroy_n_(d, i);
                               throw; // Always re-throw
                           }
                       } else {
                           HeapVec vec(alloc_);
                           vec.reserve(n);
                           try {
                               for (size_type i = 0; i < n; ++i) vec.emplace_back(std::move(other[i]));
                               storage_ = std::move(vec);
                               other.clear();
                           } catch(...) {
                               throw; // Always re-throw
                           }
                       }
                  }
             }
         }
        return *this;
    }

    /** @brief Constructs with count copies of value, using allocator alloc. */
    explicit InlinedVector(size_type count, const T& value, const Alloc& alloc = Alloc{})
        : InlinedVector(alloc) { resize(count, value); }
    /** @brief Constructs from iterator range, using allocator alloc. */
    template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
    InlinedVector(InputIt first, InputIt last, const Alloc& alloc = Alloc{})
        : InlinedVector(alloc) { reserve(std::distance(first, last)); for (; first != last; ++first) push_back(*first); }
    /** @brief Constructs from initializer list, using allocator alloc. */
    InlinedVector(std::initializer_list<T> init, const Alloc& alloc = Alloc{})
        : InlinedVector(init.begin(), init.end(), alloc) {}

    /** @brief Returns the associated allocator. */
    allocator_type get_allocator() const noexcept { return alloc_; }

    // ========================================================================
    // Element Access
    // ========================================================================
    /** @brief Access specified element with bounds checking. */
    reference at(size_type pos) { recover_if_valueless_(); if (pos >= size()) throw std::out_of_range("InlinedVector::at"); return data()[pos]; }
    /** @brief Access specified element with bounds checking. */
    const_reference at(size_type pos) const { if (pos >= size()) throw std::out_of_range("InlinedVector::at"); return data()[pos]; }
    /** @brief Access specified element. @warning No bounds checking. */
    reference operator[](size_type pos) noexcept { recover_if_valueless_(); assert(pos < size()); return data()[pos]; }
    /** @brief Access specified element. @warning No bounds checking. */
    const_reference operator[](size_type pos) const noexcept { assert(pos < size()); return data()[pos]; }
    /** @brief Access the first element. @warning Undefined behavior if empty. */
    reference front() noexcept { recover_if_valueless_(); assert(!empty()); return data()[0]; }
    /** @brief Access the first element. @warning Undefined behavior if empty. */
    const_reference front() const noexcept { assert(!empty()); return data()[0]; }
    /** @brief Access the last element. @warning Undefined behavior if empty. */
    reference back() noexcept { recover_if_valueless_(); assert(!empty()); return data()[size() - 1]; }
    /** @brief Access the last element. @warning Undefined behavior if empty. */
    const_reference back() const noexcept { assert(!empty()); return data()[size() - 1]; }


    // ========================================================================
    // Iterators
    // ========================================================================
    /** @brief Returns an iterator to the beginning. */
    iterator begin() noexcept { return data(); }
    /** @brief Returns an iterator to the beginning. */
    const_iterator begin() const noexcept { return data(); }
    /** @brief Returns an iterator to the beginning. */
    const_iterator cbegin() const noexcept { return data(); }
    /** @brief Returns an iterator to the end (past-the-end element). */
    iterator end() noexcept { return data() + size(); }
    /** @brief Returns an iterator to the end (past-the-end element). */
    const_iterator end() const noexcept { return data() + size(); }
    /** @brief Returns an iterator to the end (past-the-end element). */
    const_iterator cend() const noexcept { return data() + size(); }
    /** @brief Returns a reverse iterator to the beginning. */
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    /** @brief Returns a reverse iterator to the beginning. */
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    /** @brief Returns a reverse iterator to the beginning. */
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    /** @brief Returns a reverse iterator to the end. */
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    /** @brief Returns a reverse iterator to the end. */
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    /** @brief Returns a reverse iterator to the end. */
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    // ========================================================================
    // Capacity
    // ========================================================================
    /** @brief Checks if the container is empty. */
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    /** @brief Returns the number of elements in the container. */
    [[nodiscard]] size_type size() const noexcept { if (is_valueless_()) return 0; if (const auto* buf = std::get_if<InlineBuf>(&storage_)) return buf->size; return std::get<HeapVec>(storage_).size(); }
    /** @brief Returns the total number of elements the container can hold without reallocating. */
    [[nodiscard]] size_type capacity() const noexcept { if (is_valueless_()) return N; if (std::holds_alternative<InlineBuf>(storage_)) return N; return std::get<HeapVec>(storage_).capacity(); }
    /** @brief Returns the maximum possible number of elements, according to the allocator. */
    [[nodiscard]] size_type max_size() const noexcept { return AllocTraits::max_size(alloc_); }


    /** @brief Increase capacity. Invalidates all iterators if capacity changes or transitions inline->heap. */
    void reserve(size_type new_cap) {
        recover_if_valueless_(); const size_type current_cap = capacity(); if (new_cap <= current_cap) return;
        if (is_inline()) {
             if (new_cap <= N) return;
             auto& buf = std::get<InlineBuf>(storage_);
             HeapVec vec(alloc_); vec.reserve(new_cap); pointer src_ptr = buf.ptr(); size_type count = buf.size;
             try { for(size_type i=0; i < count; ++i) vec.emplace_back(std::move(src_ptr[i])); } catch(...) { throw; }
             storage_ = std::move(vec); // Destroys old InlineBuf via variant assignment
        } else { std::get<HeapVec>(storage_).reserve(new_cap); }
    }


    /** @brief Reduce capacity to fit size. Invalidates all iterators if transitions heap->inline. */
    void shrink_to_fit() {
        recover_if_valueless_(); if (is_inline()) return;
        auto& vec = std::get<HeapVec>(storage_); const size_type current_size = vec.size();
        if (current_size <= N) {
            InlineBuf temp_buf; // Create on stack
            pointer d = temp_buf.ptr(); pointer s = vec.data(); size_type i = 0;
            try {
                for (; i < current_size; ++i) construct_at_(d + i, std::move(s[i]));
                temp_buf.size = current_size;
                storage_ = std::move(temp_buf); // Move-assign temp_buf, destroying old vector
            } catch (...) {
                destroy_n_(d, i); // Cleanup partially constructed temp_buf
                throw;
            }
        } else {
            vec.shrink_to_fit();
        }
    }

    // ========================================================================
    // Modifiers
    // ========================================================================

    /** @brief Clears the contents. Invalidates all iterators, pointers, references. */
    void clear() noexcept {
        recover_if_valueless_();
        if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
            buf->clear(); // InlineBuf::clear handles destruction
        } else {
             std::get<HeapVec>(storage_).clear();
        }
    }

    /** @brief Appends value to the end. Invalidates end iterator, possibly all if realloc occurs. */
    void push_back(const T& value) { emplace_back(value); }
    /** @brief Appends value to the end. Invalidates end iterator, possibly all if realloc occurs. */
    void push_back(T&& value) { emplace_back(std::move(value)); }

    /** @brief Constructs element in-place at the end. Invalidates end iterator, possibly all if realloc occurs. */
    template<typename... Args>
    reference emplace_back(Args&&... args) {
        recover_if_valueless_();
        if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
            if (buf->size < N) {
                pointer elem = construct_at_(buf->ptr() + buf->size, std::forward<Args>(args)...);
                ++buf->size; return *elem;
            } else {
                const size_type old_size = buf->size;
                const size_type new_cap = std::max<size_type>(N * 2, old_size + (old_size >> 1) + 1);
                HeapVec vec(alloc_); vec.reserve(new_cap);
                pointer src_ptr = buf->ptr();
                try {
                     for(size_type i=0; i < old_size; ++i) vec.emplace_back(std::move(src_ptr[i]));
                     vec.emplace_back(std::forward<Args>(args)...);
                     storage_ = std::move(vec);
                } catch (...) { throw; }
                return std::get<HeapVec>(storage_).back();
            }
        } else {
            auto& vec = std::get<HeapVec>(storage_);
            vec.emplace_back(std::forward<Args>(args)...);
            return vec.back();
        }
    }

    /** @brief Removes the last element. Invalidates end iterator and reference/pointer to last element. */
    void pop_back() noexcept {
        recover_if_valueless_(); assert(!empty());
        if (auto* buf = std::get_if<InlineBuf>(&storage_)) { --buf->size; destroy_at_(buf->ptr() + buf->size); }
        else { std::get<HeapVec>(storage_).pop_back(); }
    }

    /** @brief Inserts value before pos. Invalidates iterators at/after pos, possibly all if realloc occurs. */
    iterator insert(const_iterator pos, const T& value) {
        recover_if_valueless_();
        size_type idx = static_cast<size_type>(std::distance(cbegin(), pos));

        // Lambda captures common insertion logic, taking the source value by const ref.
        auto with_staged_src = [&](const T& src) -> iterator {
            if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
                pointer p = buf->ptr();
                const size_type old_size = buf->size;
                if (old_size < N) {
                    // --- Inline path, space available ---
                    if (idx == old_size) { // Append case
                        construct_at_(p + old_size, src);
                        buf->size = old_size + 1;
                        return begin() + idx;
                    }
                    if constexpr (std::is_trivially_copyable_v<T>) {
                        // Trivial fast path: memmove
                        std::memmove(p + idx + 1, p + idx, (old_size - idx) * sizeof(T));
                        if constexpr (std::is_copy_assignable_v<T>) p[idx] = src;
                        else std::memcpy(p + idx, std::addressof(src), sizeof(T));
                        buf->size = old_size + 1;
                        return begin() + idx;
                    } else if constexpr (std::is_nothrow_move_assignable_v<T> && std::is_copy_assignable_v<T>) {
                        // Nothrow-move fast path: shift + assign
                        construct_at_(p + old_size, std::move(p[old_size - 1]));
                        try {
                            for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(old_size) - 2; i >= static_cast<std::ptrdiff_t>(idx); --i) {
                                p[i + 1] = std::move(p[i]);
                            }
                            p[idx] = src;
                            buf->size = old_size + 1;
                        } catch (...) {
                            destroy_at_(p + old_size); // Cleanup temporary element
                            throw;
                        }
                        return begin() + idx;
                    } else {
                        // Slow path: rebuild buffer using traits
                        InlineBuf tmp; pointer d = tmp.ptr(); pointer s = buf->ptr(); size_type k = 0;
                        try {
                            for (; k < idx; ++k) construct_at_(d + k, std::move_if_noexcept(s[k]));
                            construct_at_(d + k, src); ++k; // copy-construct src
                            for (size_type i = idx; i < old_size; ++i, ++k) construct_at_(d + k, std::move_if_noexcept(s[i]));
                        } catch (...) { destroy_n_(d, k); throw; }
                        tmp.size = old_size + 1;
                        std::get<InlineBuf>(storage_).swap(tmp); // Call member swap
                        return begin() + idx;
                    }
                } else {
                    // --- Inline path, spill to heap ---
                    const size_type new_cap = std::max<size_type>(N * 2, old_size + (old_size >> 1) + 1);
                    HeapVec vec(alloc_); vec.reserve(new_cap);
                    pointer src_ptr = buf->ptr();
                    try {
                        for(size_type i=0; i < idx; ++i) vec.emplace_back(std::move(src_ptr[i]));
                        vec.emplace_back(src); // copy-insert src
                        for(size_type i=idx; i < old_size; ++i) vec.emplace_back(std::move(src_ptr[i]));
                        storage_ = std::move(vec);
                    } catch (...) { throw; } // vec dtor cleans up
                    return begin() + idx;
                }
            } else {
                // --- Heap path: rebuild and swap ---
                auto& vec = std::get<HeapVec>(storage_);
                const size_type old_size = vec.size();
                HeapVec new_vec(alloc_);
                new_vec.reserve(old_size + 1);
                try {
                    for (size_type i = 0; i < idx; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                    new_vec.emplace_back(src); // copy-insert src
                    for (size_type i = idx; i < old_size; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                    vec.swap(new_vec); // Swap new vector into place
                } catch(...) { throw; } // new_vec dtor cleans up
                return begin() + idx;
            }
        };

        // Alias check logic
        if (is_inline()) {
            const auto* b = std::get_if<InlineBuf>(&storage_);
            const bool alias = b && (&value >= b->ptr()) && (&value < b->ptr() + b->size);
            if (alias) { T s(value); return with_staged_src(s); }
            else { return with_staged_src(value); }
        } else {
            const auto& v = std::get<HeapVec>(storage_);
            const bool alias = (&value >= v.data()) && (&value < v.data() + v.size());
            if (alias) { T s(value); return with_staged_src(s); }
            else { return with_staged_src(value); }
        }
    }

    /** @brief Inserts value before pos. Invalidates iterators at/after pos, possibly all if realloc occurs. */
    iterator insert(const_iterator pos, T&& value) {
         recover_if_valueless_();
         size_type idx = static_cast<size_type>(std::distance(cbegin(), pos));

        auto with_staged_src = [&](auto&& src) -> iterator {
             if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
                pointer p = buf->ptr();
                const size_type old_size = buf->size;
                if (old_size < N) {
                    // --- Inline path, space available ---
                    if (idx == old_size) {
                        construct_at_(p + old_size, std::forward<decltype(src)>(src));
                        buf->size = old_size + 1;
                        return begin() + idx;
                    }
                    if constexpr (std::is_trivially_copyable_v<T>) {
                        // Trivial fast path: memmove
                        std::memmove(p + idx + 1, p + idx, (old_size - idx) * sizeof(T));
                        if constexpr (std::is_move_assignable_v<T>) p[idx] = std::forward<decltype(src)>(src);
                        else std::memcpy(p + idx, std::addressof(src), sizeof(T));
                        buf->size = old_size + 1;
                        return begin() + idx;
                    } else if constexpr (std::is_nothrow_move_assignable_v<T>) {
                        // Nothrow-move fast path: shift + assign
                        construct_at_(p + old_size, std::move(p[old_size - 1]));
                        try {
                            for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(old_size) - 2; i >= static_cast<std::ptrdiff_t>(idx); --i) {
                                p[i + 1] = std::move(p[i]);
                            }
                            p[idx] = std::forward<decltype(src)>(src); // move-assign
                            buf->size = old_size + 1;
                        } catch (...) {
                            destroy_at_(p + old_size); // Cleanup temporary element
                            throw;
                        }
                        return begin() + idx;
                    } else {
                        // Slow path: rebuild buffer
                        InlineBuf tmp; pointer d = tmp.ptr(); pointer s = buf->ptr(); size_type k = 0;
                        try {
                            for (; k < idx; ++k) construct_at_(d + k, std::move(s[k]));
                            construct_at_(d + k, std::forward<decltype(src)>(src)); ++k; // move-construct
                            for (size_type i = idx; i < old_size; ++i, ++k) construct_at_(d + k, std::move(s[i]));
                        } catch (...) { destroy_n_(d, k); throw; }
                        tmp.size = old_size + 1;
                        std::get<InlineBuf>(storage_).swap(tmp); // Call member swap
                        return begin() + idx;
                    }
                } else {
                    // --- Inline path, spill to heap ---
                    const size_type new_cap = std::max<size_type>(N * 2, old_size + (old_size >> 1) + 1);
                    HeapVec vec(alloc_); vec.reserve(new_cap);
                    pointer src_ptr = buf->ptr();
                    try {
                         for(size_type i=0; i < idx; ++i) vec.emplace_back(std::move(src_ptr[i]));
                         vec.emplace_back(std::forward<decltype(src)>(src)); // move-insert src
                         for(size_type i=idx; i < old_size; ++i) vec.emplace_back(std::move(src_ptr[i]));
                         storage_ = std::move(vec);
                    } catch (...) { throw; }
                    return begin() + idx;
                }
            } else {
                // --- Heap path: rebuild and swap ---
                 auto& vec = std::get<HeapVec>(storage_);
                 const size_type old_size = vec.size();
                 HeapVec new_vec(alloc_);
                 new_vec.reserve(old_size + 1);
                 try {
                     for (size_type i = 0; i < idx; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                     new_vec.emplace_back(std::forward<decltype(src)>(src)); // move-insert src
                     for (size_type i = idx; i < old_size; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                     vec.swap(new_vec); // Swap new vector into place
                 } catch(...) { throw; }
                 return begin() + idx;
            }
        };

        // Alias check logic
        if (is_inline()) {
            const auto* b = std::get_if<InlineBuf>(&storage_);
            const bool alias = b && (&value >= b->ptr()) && (&value < b->ptr() + b->size);
            if (alias) { T s(std::move(value)); return with_staged_src(std::move(s)); }
            else { return with_staged_src(std::move(value)); }
        } else {
            const auto& v = std::get<HeapVec>(storage_);
            const bool alias = (&value >= v.data()) && (&value < v.data() + v.size());
            if (alias) { T s(std::move(value)); return with_staged_src(std::move(s)); }
            else { return with_staged_src(std::move(value)); }
        }
    }


    /** @brief Erases element at pos. Invalidates iterators at/after pos. */
    iterator erase(const_iterator pos) { recover_if_valueless_(); return erase(pos, pos + 1); }
    /** @brief Erases elements in [first, last). Invalidates iterators at/after first. */
    iterator erase(const_iterator first, const_iterator last) {
        recover_if_valueless_();
        size_type start = static_cast<size_type>(std::distance(cbegin(), first));
        size_type cnt = static_cast<size_type>(std::distance(first, last));
        if (cnt == 0) return begin() + start;

        if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
            // --- Inline path ---
            pointer p = buf->ptr();
            const size_type old_size = buf->size;
            const size_type keep = old_size - cnt;
            if constexpr (std::is_trivially_copyable_v<T>) {
                // Trivial fast path: memmove
                std::memmove(p + start, p + start + cnt, (keep - start) * sizeof(T));
                if constexpr (!std::is_trivially_destructible_v<T>) destroy_n_(p + keep, cnt);
                buf->size = keep;
            } else if constexpr (std::is_nothrow_move_assignable_v<T>) {
                // Nothrow-move fast path: shift
                for (size_type i = start; i < keep; ++i) p[i] = std::move(p[i + cnt]);
                destroy_n_(p + keep, cnt);
                buf->size = keep;
            } else {
                // Slow path: rebuild buffer
                InlineBuf tmp; pointer d = tmp.ptr(); const pointer s = buf->ptr(); size_type k = 0;
                try {
                    for (; k < start; ++k) construct_at_(d + k, std::move(s[k]));
                    for (size_type i = start + cnt; i < old_size; ++i, ++k) construct_at_(d + k, std::move(s[i]));
                } catch (...) { destroy_n_(d, k); throw; }
                tmp.size = k;
                std::get<InlineBuf>(storage_).swap(tmp); // Call member swap
            }
            return begin() + start;
        } else {
            // --- Heap path: rebuild and swap ---
            auto& vec = std::get<HeapVec>(storage_);
            const size_type old_size = vec.size();
            const size_type keep = old_size - cnt;
            HeapVec new_vec(alloc_);
            new_vec.reserve(keep);
            try {
                 for (size_type i = 0; i < start; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                 for (size_type i = start + cnt; i < old_size; ++i) new_vec.emplace_back(std::move_if_noexcept(vec[i]));
                 vec.swap(new_vec); // Swap new vector into place
            } catch(...) { throw; }
            return begin() + start;
        }
    }

    /** @brief Resizes to count elements (default construction). Requires T to be DefaultInsertable. */
    template<typename U = T, std::enable_if_t<std::is_default_constructible_v<U>, int> = 0>
    void resize(size_type count) {
        recover_if_valueless_(); size_type current = size();
        if (count < current) { erase(begin() + count, end()); }
        else if (count > current) {
            reserve(count);
            if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
                pointer p = buf->ptr();
                for (size_type i = current; i < count; ++i) construct_at_(p + i);
                buf->size = count;
            } else {
                std::get<HeapVec>(storage_).resize(count);
            }
        }
    }
    /** @brief Resizes to count elements (copying value). Requires T to be CopyInsertable. */
    void resize(size_type count, const value_type& value) {
        recover_if_valueless_(); size_type current = size();
        if (count < current) { erase(begin() + count, end()); }
        else if (count > current) {
            reserve(count);
            if (auto* buf = std::get_if<InlineBuf>(&storage_)) {
                pointer p = buf->ptr();
                for (size_type i = current; i < count; ++i) construct_at_(p + i, value);
                buf->size = count;
            } else {
                std::get<HeapVec>(storage_).resize(count, value);
            }
        }
    }

    // ========================================================================
    // swap()
    // ========================================================================
    /**
     * @brief Swaps contents with another InlinedVector.
     * @note Behavior depends on allocator propagation traits (POCS).
     * If POCS is true, allocators are swapped. If POCS is false (default),
     * allocators must be equal, or behavior is undefined.
     * Invalidation: All iterators/pointers/references are invalidated unless
     * both containers are on the heap and allocators propagate.
     */
    void swap(InlinedVector& other) noexcept(
        (AllocTraits::propagate_on_container_swap::value || AllocTraits::is_always_equal::value) &&
        std::is_nothrow_swappable_v<T> && std::is_nothrow_move_constructible_v<T>
    ) {
        recover_if_valueless_(); other.recover_if_valueless_();
        using POCS = typename AllocTraits::propagate_on_container_swap;
        if constexpr (POCS::value) {
            using std::swap;
            swap(alloc_, other.alloc_);
        } else {
            // If allocators don't propagate, they must be equal to swap.
            assert(alloc_ == other.alloc_ && "Cannot swap InlinedVectors with unequal non-propagating allocators");
        }

        if (is_inline() && other.is_inline()) {
             auto& buf_a = std::get<InlineBuf>(storage_);
             auto& buf_b = std::get<InlineBuf>(other.storage_);
             buf_a.swap(buf_b); // Call InlineBuf's member swap
             return;
        }
        if (!is_inline() && !other.is_inline()) {
             // Let std::vector::swap handle allocator propagation logic
             std::get<HeapVec>(storage_).swap(std::get<HeapVec>(other.storage_));
             return;
        }
        // Mixed case: relies on std::variant::swap
        storage_.swap(other.storage_);
    }

    // ========================================================================
    // Comparison operators
    // ========================================================================
    friend bool operator==(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() == std::declval<const T&>())) {
        return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }
    friend bool operator!=(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() == std::declval<const T&>())) {
        return !(lhs == rhs);
    }
    friend bool operator<(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() < std::declval<const T&>())) {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
    friend bool operator<=(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() < std::declval<const T&>())) {
        return !(rhs < lhs);
    }
    friend bool operator>(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() < std::declval<const T&>())) {
        return rhs < lhs;
    }
    friend bool operator>=(const InlinedVector& lhs, const InlinedVector& rhs)
        noexcept(noexcept(std::declval<const T&>() < std::declval<const T&>())) {
        return !(lhs < rhs);
    }
};

/** @brief Non-member swap for InlinedVector. */
template<typename T, std::size_t N, typename Alloc>
void swap(InlinedVector<T, N, Alloc>& lhs, InlinedVector<T, N, Alloc>& rhs)
    noexcept(noexcept(lhs.swap(rhs)))
{
    lhs.swap(rhs);
}

} // namespace lloyal