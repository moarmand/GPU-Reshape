#pragma once

// Std
#include <vector>

/// Stack based container, with optional heap fallback
template<typename T, size_t STACK_LENGTH>
struct TrivialStackVector {
    /// Initialize to size
    TrivialStackVector(size_t size = 0) : data(stack) {
        Resize(size);
    }

    /// Copy from other
    TrivialStackVector(const TrivialStackVector& other) : TrivialStackVector(other.size) {
        std::memcpy(data, other.Data(), sizeof(T) * size);
    }

    /// Move from other
    TrivialStackVector(TrivialStackVector&& other) : fallback(std::move(other.fallback)), size(other.size) {
        if (other.data != other.stack) {
            data = fallback.data();
        } else {
            std::memcpy(stack, other.stack, sizeof(T) * other.size);
            data = stack;
        }
    }

    /// Assign copy from other
    TrivialStackVector& operator=(const TrivialStackVector& other) {
        Resize(other.Size());
        std::memcpy(data, other.Data(), sizeof(T) * size);
    }

    /// Assign move from other
    TrivialStackVector& operator=(TrivialStackVector&& other) {
        size = other.size;
        fallback = std::move(other.fallback);

        if (other.data != other.stack) {
            data = fallback.data();
        } else {
            std::memcpy(stack, other.stack, sizeof(T) * other.size);
            data = stack;
        }
    }

    /// Resize this container
    /// \param length the desired length
    void Resize(size_t length) {
        if (data != stack || length > STACK_LENGTH) {
            fallback.resize(length);
            data = fallback.data();
        }

        size = length;
    }

    /// Add a value to this container
    /// \param value the value to be added
    void Add(const T& value) {
        if (data != stack || size >= STACK_LENGTH) {
            fallback.push_back(value);
            data = fallback.data();
        } else {
            data[size] = value;
        }

        size++;
    }

    /// Size of this container
    size_t Size() const {
        return size;
    }

    /// Get the element at a given index
    T& operator[](uint32_t i) {
        return data[i];
    }

    /// Get the element at a given index
    const T& operator[](uint32_t i) const {
        return data[i];
    }

    /// Get the data address of this container
    T* Data() {
        return data;
    }

    /// Get the data address of this container
    const T* Data() const {
        return data;
    }

    T* begin() {
        return data;
    }

    const T* begin() const {
        return data;
    }

    T* end() {
        return data + size;
    }

    const T* end() const {
        return data + size;
    }

private:
    /// Current size of this container
    size_t size{0};

    /// Current base address of the data
    T* data{nullptr};

    /// Initial stack data
    T stack[STACK_LENGTH];

    /// Heap fallback
    std::vector<T> fallback;
};