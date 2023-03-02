﻿#pragma once

#include "Allocators.h"
#include "AllocatorTag.h"
#include <Common/Assert.h>

template<typename T>
class ContainerAllocator {
public:
    using value_type = T;

    /// Constructor
    ContainerAllocator() {
        ASSERT(false, "Null container allocators not supported");
    }

    /// Constructor
    ContainerAllocator(const Allocators& allocators) noexcept : allocators(allocators) {
        /** */
    }

    /// Copy constructor
    ContainerAllocator(const ContainerAllocator&) = default;

    /// Copy constructor
    template<typename U>
    ContainerAllocator(const ContainerAllocator<U>& other) : ContainerAllocator(other.allocators) {
        
    }

    /// Assignment constructor
    ContainerAllocator& operator=(const ContainerAllocator&) = default;

    /// Allocate elements
    /// \param count number of elements
    /// \return base ptr
    [[nodiscard]] T* allocate(size_t count) {
        return static_cast<T*>(allocators.alloc(allocators.userData, sizeof(T) * count, alignof(T), allocators.tag));
    }

    /// Deallocate element
    /// \param ptr base ptr
    void deallocate(T* ptr, size_t) {
        allocators.free(allocators.userData, ptr, alignof(T));
    }
    
#if __cplusplus >= 202002L
    /// Allocate raw
    /// \param size expected size
    /// \param align expected alignment
    /// \return base ptr
    void* allocate_bytes(const size_t size, size_t align = alignof(max_align_t)) {
        return static_cast<T*>(allocators.alloc(allocators.userData, size, align, allocators.tag));
    }

    /// Deallocate raw
    /// \param ptr base ptr
    /// \param align allocation alignment
    void deallocate_bytes(void* ptr, size_t, size_t align = alignof(max_align_t)) noexcept {
        allocators.free(allocators.userData, ptr, align);
    }

    /// Allocate an object
    /// \param count number of objects
    /// \return base ptr
    template <class U>
    U* allocate_object(size_t count = 1u) {
        return static_cast<U*>(allocate_bytes(sizeof(U) * count, alignof(U)));
    }

    /// Deallocate an object
    /// \param ptr base ptr
    /// \param count number of objects
    template <class U>
    void deallocate_object(U* ptr, size_t count = 1u) noexcept {
        deallocate_bytes(ptr, sizeof(U) * count, alignof(U));
    }

    /// Allocate and construct a new object
    /// \param args parameters
    /// \return object ptr
    template <class U, class... A>
    U* new_object(A&&... args) {
        return new (allocate_object<U>()) U(std::forward<A...>(args...));
    }

    /// Destruct and deallocate an object
    /// \param ptr base ptr
    template <class U>
    void delete_object(U* ptr) noexcept {
        ptr->~U();
        deallocate_object(ptr);
    }
#endif // __cplusplus >= 202002L

    /// Check for equality
    bool operator==(const ContainerAllocator& rhs) const {
        return allocators == rhs.allocators;
    }

    /// Base allocators
    Allocators allocators;
};
