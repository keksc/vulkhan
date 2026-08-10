#pragma once

// =============================================================================
// 1. WASM LINKAGE MACROS & CORE TYPES
// =============================================================================
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#define WASM_IMPORT(env, name)                                                 \
  __attribute__((import_module(env), import_name(name)))

using size_t = unsigned long;
using intptr_t = long;
using uintptr_t = unsigned long;

// =============================================================================
// 2. INTERNAL UTILITIES & DYNAMIC BUMP ALLOCATOR
// =============================================================================
namespace wasm_internal {
// Magic external linker symbol that tracks your data section's memory ceiling
unsigned char __heap_base;

inline void *memcpy(void *dest, const void *src, size_t count) {
  auto *d = static_cast<char *>(dest);
  const auto *s = static_cast<const char *>(src);
  while (count--)
    *d++ = *s++;
  return dest;
}

inline size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

class Allocator {
private:
  inline static uintptr_t heap_start = 0;
  inline static size_t offset = 0;
  inline static size_t current_reserved_capacity = 0;

public:
  static void *allocate(size_t size) {
    size = (size + 7) & ~7; // 8-byte alignment

    if (heap_start == 0) {
      // Safely capture the address pointing past all read-only string literals
      heap_start = reinterpret_cast<uintptr_t>(&__heap_base);

      // Calculate how much space remains in our initial WebAssembly page
      // allocation layout
      size_t current_allocated_bytes =
          __builtin_wasm_memory_size(0) * 64 * 1024;
      if (current_allocated_bytes > heap_start) {
        current_reserved_capacity = current_allocated_bytes - heap_start;
      } else {
        current_reserved_capacity = 0;
      }
    }

    // Check if we need to request more memory pages from the host (Wasmtime)
    if (offset + size > current_reserved_capacity) {
      size_t needed_bytes = (offset + size) - current_reserved_capacity;
      size_t pages_to_request = (needed_bytes + (64 * 1024 - 1)) / (64 * 1024);

      intptr_t old_page_count = __builtin_wasm_memory_grow(0, pages_to_request);
      if (old_page_count == -1) {
        return nullptr; // Host refused to grant more memory
      }

      current_reserved_capacity += pages_to_request * 64 * 1024;
    }

    void *ptr = reinterpret_cast<void *>(heap_start + offset);
    offset += size;
    return ptr;
  }

  static void reset() { offset = 0; }
};
} // namespace wasm_internal

// Global memory allocation overrides (Separated into header definitions)
void *operator new(size_t size) {
  return wasm_internal::Allocator::allocate(size);
}
void *operator new[](size_t size) {
  return wasm_internal::Allocator::allocate(size);
}
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
void operator delete[](void *, size_t) noexcept {}
inline void *operator new(size_t, void *p) noexcept {
  return p;
} // Placement new

// =============================================================================
// 3. THE STANDARD NAMESPACE INTERFACE (std::)
// =============================================================================
namespace std {

// --- std::string ---
class string {
private:
  char *m_data = nullptr;
  size_t m_size = 0;

public:
  string() = default;

  string(const char *str) {
    m_size = wasm_internal::strlen(str);
    m_data = new char[m_size + 1];
    wasm_internal::memcpy(m_data, str, m_size + 1);
  }

  const char *c_str() const { return m_data ? m_data : ""; }
  size_t size() const { return m_size; }
  size_t length() const { return m_size; }
  bool empty() const { return m_size == 0; }
};

// --- std::vector ---
template <typename T> class vector {
private:
  T *m_data = nullptr;
  size_t m_size = 0;
  size_t m_capacity = 0;

  void reserve_internal(size_t new_cap) {
    if (new_cap <= m_capacity)
      return;

    // Allocate uninitialized raw memory blocks to accommodate object arrays
    // without default constructors
    T *new_data = static_cast<T *>(operator new(new_cap * sizeof(T)));

    for (size_t i = 0; i < m_size; ++i) {
      // Re-map elements using target copy constructors via placement new
      ::new (static_cast<void *>(&new_data[i])) T(m_data[i]);
      m_data[i].~T(); // Explicitly destroy the old object layout index
    }

    m_data = new_data;
    m_capacity = new_cap;
  }

public:
  vector() = default;

  ~vector() {
    for (size_t i = 0; i < m_size; ++i) {
      m_data[i].~T();
    }
  }

  void push_back(const T &value) {
    if (m_size >= m_capacity) {
      reserve_internal(m_capacity == 0 ? 4 : m_capacity * 2);
    }
    ::new (static_cast<void *>(&m_data[m_size++])) T(value);
  }

  T &operator[](size_t index) { return m_data[index]; }
  const T &operator[](size_t index) const { return m_data[index]; }
  size_t size() const { return m_size; }
  bool empty() const { return m_size == 0; }
  T *data() { return m_data; }

  T *begin() { return m_data; }
  T *end() { return m_data + m_size; }
};

// --- std::unique_ptr ---
template <typename T> class unique_ptr {
private:
  T *m_ptr = nullptr;

public:
  unique_ptr() = default;
  explicit unique_ptr(T *p) : m_ptr(p) {}
  ~unique_ptr() {
    if (m_ptr)
      m_ptr->~T();
  }

  unique_ptr(const unique_ptr &) = delete;
  unique_ptr &operator=(const unique_ptr &) = delete;

  unique_ptr(unique_ptr &&other) noexcept : m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;
  }
  unique_ptr &operator=(unique_ptr &&other) noexcept {
    if (this != &other) {
      m_ptr = other.m_ptr;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  T &operator*() const { return *m_ptr; }
  T *operator->() const { return m_ptr; }
  T *get() const { return m_ptr; }
  explicit operator bool() const { return m_ptr != nullptr; }
};

template <typename T, typename... Args>
unique_ptr<T> make_unique(Args &&...args) {
  return unique_ptr<T>(new T(static_cast<Args &&>(args)...));
}
} // namespace std
