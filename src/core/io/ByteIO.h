#pragma once

#include <QByteArray>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Bounds-checked byte access for file readers and writers. A reader never reads past its data: the first failed read sets a
// sticky error flag and every later read returns 0, so a parser can read a whole structure and test ok() once, and a damaged
// file can never cause an out-of-range access.
namespace doodlebold::io {

enum class ByteOrder {
    Little,
    Big
};

inline std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size, ByteOrder order) : m_data(data), m_size(size), m_order(order) {}
    ByteReader(const QByteArray& data, ByteOrder order)
        : ByteReader(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size()), order) {}

    bool ok() const { return !m_failed; }
    std::size_t pos() const { return m_pos; }
    std::size_t size() const { return m_size; }
    std::size_t remaining() const { return m_size - m_pos; }
    bool atEnd() const { return m_pos == m_size; }

    std::uint8_t u8() { return static_cast<std::uint8_t>(read(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(read(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(read(4)); }
    std::uint64_t u64() { return read(8); }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    const std::uint8_t* span(std::size_t n) {
        if (m_failed || n > remaining()) {
            m_failed = true;
            return nullptr;
        }
        const std::uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }
    QByteArray bytes(std::size_t n) {
        const std::uint8_t* p = span(n);
        return p ? QByteArray(reinterpret_cast<const char*>(p), static_cast<qsizetype>(n)) : QByteArray();
    }
    void skip(std::size_t n) { span(n); }
    ByteReader sub(std::size_t n) {
        const std::uint8_t* p = span(n);
        ByteReader r(p ? p : m_data, p ? n : 0, m_order);
        if (!p)
            r.fail();
        return r;
    }
    // The next `n` bytes without moving, or null when fewer remain (does not set the error flag).
    const std::uint8_t* peek(std::size_t n) const { return n <= remaining() ? m_data + m_pos : nullptr; }
    void seek(std::size_t pos) {
        if (pos > m_size)
            m_failed = true;
        else
            m_pos = pos;
    }
    void fail() { m_failed = true; }

private:
    std::uint64_t read(int n) {
        const std::uint8_t* p = span(static_cast<std::size_t>(n));
        if (!p)
            return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) {
            const int shift = m_order == ByteOrder::Little ? i * 8 : (n - 1 - i) * 8;
            v |= static_cast<std::uint64_t>(p[i]) << shift;
        }
        return v;
    }

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
    ByteOrder m_order;
    bool m_failed = false;
};

class ByteWriter {
public:
    explicit ByteWriter(ByteOrder order) : m_order(order) {}

    void u8(std::uint32_t v) { put(v, 1); }
    void u16(std::uint32_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i16(std::int32_t v) { put(static_cast<std::uint32_t>(v), 2); }
    void i32(std::int32_t v) { put(static_cast<std::uint32_t>(v), 4); }
    void bytes(const QByteArray& b) { m_data.append(b); }
    void bytes(const char* p, std::size_t n) { m_data.append(p, static_cast<qsizetype>(n)); }
    void zeros(std::size_t n) { m_data.append(static_cast<qsizetype>(n), '\0'); }
    void padTo(std::size_t multiple) {
        const std::size_t rest = static_cast<std::size_t>(m_data.size()) % multiple;
        if (rest)
            zeros(multiple - rest);
    }

    std::size_t size() const { return static_cast<std::size_t>(m_data.size()); }
    const QByteArray& data() const { return m_data; }
    QByteArray take() { return std::move(m_data); }

private:
    void put(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) {
            const int shift = m_order == ByteOrder::Little ? i * 8 : (n - 1 - i) * 8;
            m_data.append(static_cast<char>((v >> shift) & 0xFFu));
        }
    }

    QByteArray m_data;
    ByteOrder m_order;
};

}
