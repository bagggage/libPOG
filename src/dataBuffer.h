#ifndef BUFFER_H
#define BUFFER_H

#include <cstddef>
#include <cstdint>

struct DataBuffer {
    typedef uint8_t byte_t;

    static constexpr uint16_t MAX_SIZE = 8192;

    byte_t data[MAX_SIZE];
    size_t size = 0;

    inline operator const char*() const { return reinterpret_cast<const char*>(data); }
    inline operator char*() { return reinterpret_cast<char*>(data); }
};

#endif // !DATA_BUFFER_H