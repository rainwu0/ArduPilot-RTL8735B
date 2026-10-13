#pragma once

#include <stdint.h>
#include <algorithm>
#include <vector>
#include <atomic>

class ByteBuffer {
public:
    static inline std::atomic<uint32_t> empty_storage_accesses{0};
    explicit ByteBuffer(uint32_t size) { set_size(size); }

    bool set_size(uint32_t size)
    {
        _storage.assign(size, 0);
        _head = 0;
        _tail = 0;
        return true;
    }

    uint32_t available() const
    {
        if (_storage.empty()) {
            return 0;
        }
        return (_tail + _storage.size() - _head) % _storage.size();
    }

    uint32_t space() const
    {
        return _storage.empty() ? 0 : _storage.size() - 1 - available();
    }

    uint32_t get_size() const { return _storage.size(); }

    int16_t peek(uint32_t offset) const
    {
        if (offset >= available()) {
            return -1;
        }
        return _storage[(_head + offset) % _storage.size()];
    }

    bool advance(uint32_t count)
    {
        if (count > available()) {
            return false;
        }
        _head = (_head + count) % _storage.size();
        return true;
    }

    uint32_t write(const uint8_t *data, uint32_t count)
    {
        if (_storage.empty() && count != 0) {
            empty_storage_accesses++;
        }
        const uint32_t n = std::min(count, space());
        for (uint32_t i = 0; i < n; i++) {
            _storage[_tail] = data[i];
            _tail = (_tail + 1) % _storage.size();
        }
        return n;
    }

    uint32_t read(uint8_t *data, uint32_t count)
    {
        if (_storage.empty() && count != 0) {
            empty_storage_accesses++;
        }
        const uint32_t n = std::min(count, available());
        for (uint32_t i = 0; i < n; i++) {
            data[i] = _storage[_head];
            _head = (_head + 1) % _storage.size();
        }
        return n;
    }

    bool read_byte(uint8_t *data) { return read(data, 1) == 1; }
    void clear() { _head = _tail = 0; }

private:
    std::vector<uint8_t> _storage;
    uint32_t _head = 0;
    uint32_t _tail = 0;
};
