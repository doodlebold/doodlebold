#pragma once

// The messages between Doodlebold and doodlebold-plugin-host, the process a format plugin runs in (plugin-process-design): over
// the helper's standard input and output, each a type, a length and that many bytes. Both ends check every message; one that
// doesn't fit ends the call. Plain C++, no Qt, so the helper needs no library beside it in its sandbox.
#include <doodlebold/format_plugin.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace doodlebold::plugin_host {

// The most layers one write sends the helper: as many as a file may hold (the .doodle reader's 20,000).
inline constexpr std::uint32_t kMostLayers = 20000;

// The widest layer a plugin may hand over, in pixels: four times the largest canvas, so one band of it always fits a message.
inline constexpr std::int32_t kWidestLayer = 65536;

// The protocol's version: a helper of another one is refused at its first answer.
inline constexpr std::uint32_t kProtocolVersion = 1;

// Pixels cross in bands of this many rows, so neither side holds a layer twice in full: a tile row's height.
inline constexpr std::uint32_t kBandRows = 64;

// The most one message may carry, in bytes: the largest band (kBandRows rows of the widest layer at 8 bytes a pixel) with room
// for its header. A length past it is a broken or hostile helper.
inline constexpr std::uint64_t kMostMessageBytes = std::uint64_t{kBandRows} * std::uint64_t{kWidestLayer} * 8u + 4096u;

// The most a file's bytes may be when they go to the helper whole (a plugin reads from memory): the plugin reader's own limit.
inline constexpr std::uint64_t kMostFileBytes = 1500ull << 20;

// The longest text (a note, an error, a name) taken from a helper, in bytes.
inline constexpr std::uint32_t kMostTextBytes = 4096;

enum class Message : std::uint32_t {
    // Doodlebold to the helper.
    Describe = 1,   // → Description
    Recognize = 2,  // u32 format, the file's first bytes → Recognized
    Read = 3,       // u32 format, the file → SetCanvas, AddLayer…, Note, SetError, then ReadDone
    Write = 4,      // u32 format, the canvas, the layers' numbers → GetPlane…, WriteBytes…, SetError, then WriteDone
    Reply = 5,      // i32: the answer to SetCanvas, AddLayer, GetPlane and WriteBytes
    Band = 6,       // a band of a plane asked for with GetPlane: u32 first row, u32 rows, the values
    Quit = 7,
    // The helper to Doodlebold.
    Description = 101,  // u32 protocol, u32 plugin API version, text name, u32 count, then each format's numbers and texts
    Recognized = 102,   // u8
    SetCanvas = 103,    // i32 width, i32 height, f64 ppi, u8[4] background → Reply
    AddLayer = 104,     // the layer's numbers and texts, then its planes as LayerBand messages → Reply
    LayerBand = 105,    // u8 plane (0 pixels, 1 mask), u32 first row, u32 rows, the values
    Note = 106,         // text
    SetError = 107,     // text
    ReadDone = 108,     // i32 the plugin's result
    GetPlane = 109,     // u8 plane (0 pixels, 1 mask, 2 the composite), u32 layer → Band…, then Reply
    WriteBytes = 110,   // the bytes to append → Reply
    WriteDone = 111,    // i32 the plugin's result
    Failed = 112,       // text: the helper couldn't load the plugin or keep to its restrictions
};

// The planes GetPlane and LayerBand name.
enum class Plane : std::uint8_t {
    Pixels = 0,
    Mask = 1,
    Composite = 2,
};

// A message's bytes, written and read in the machine's own order (both ends are the same build on the same machine).
class Bytes {
public:
    Bytes() = default;
    explicit Bytes(std::vector<std::uint8_t> data) : m_data(std::move(data)) {}

    template <typename T> void put(T value) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        m_data.insert(m_data.end(), p, p + sizeof value);
    }
    void putBytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        m_data.insert(m_data.end(), p, p + size);
    }
    void putText(const char* text) {
        const std::size_t n = text ? std::min<std::size_t>(std::strlen(text), kMostTextBytes) : 0;
        put(static_cast<std::uint32_t>(n));
        putBytes(text, n);
    }

    template <typename T> std::optional<T> get() {
        if (m_at + sizeof(T) > m_data.size())
            return std::nullopt;
        T value;
        std::memcpy(&value, m_data.data() + m_at, sizeof value);
        m_at += sizeof value;
        return value;
    }
    // Points into the message; nullptr when it holds fewer than `size` bytes more.
    const std::uint8_t* getBytes(std::size_t size) {
        if (size > m_data.size() - m_at)
            return nullptr;
        const std::uint8_t* p = m_data.data() + m_at;
        m_at += size;
        return p;
    }
    std::optional<std::string> getText() {
        const auto n = get<std::uint32_t>();
        if (!n || *n > kMostTextBytes)
            return std::nullopt;
        const std::uint8_t* p = getBytes(*n);
        return p ? std::optional(std::string(reinterpret_cast<const char*>(p), *n)) : std::nullopt;
    }
    bool atEnd() const { return m_at == m_data.size(); }
    std::size_t left() const { return m_data.size() - m_at; }

    const std::vector<std::uint8_t>& data() const { return m_data; }
    std::vector<std::uint8_t>& data() { return m_data; }

private:
    std::vector<std::uint8_t> m_data;
    std::size_t m_at = 0;
};

// A message as it travels: its type and length, then its bytes.
struct Header {
    std::uint32_t type;
    std::uint64_t length;
};

// A layer's numbers and texts, without its planes (they follow as bands).
inline void putLayer(Bytes& b, const doodlebold_layer& l) {
    for (const std::int32_t v : {l.kind, l.parent, l.opacity, l.visible, l.clipping, l.x, l.y, l.width, l.height, l.has_mask,
                                 l.mask_enabled, l.mask_x, l.mask_y, l.mask_width, l.mask_height})
        b.put(v);
    b.put(l.mask_outside);
    b.putText(l.name);
    b.putText(l.blend_mode);
}

// The layer putLayer wrote, its texts kept in `texts` (which must outlive it). Its planes are left null.
inline bool getLayer(Bytes& b, doodlebold_layer& l, std::deque<std::string>& texts) {
    l = doodlebold_layer{};
    l.struct_size = sizeof l;
    std::int32_t* fields[] = {&l.kind,  &l.parent,   &l.opacity,  &l.visible,      &l.clipping,    &l.x,          &l.y,          &l.width,
                              &l.height, &l.has_mask, &l.mask_enabled, &l.mask_x, &l.mask_y, &l.mask_width, &l.mask_height};
    for (std::int32_t* field : fields) {
        const auto v = b.get<std::int32_t>();
        if (!v)
            return false;
        *field = *v;
    }
    const auto outside = b.get<std::uint16_t>();
    auto name = b.getText(), blend = b.getText();
    if (!outside || !name || !blend)
        return false;
    l.mask_outside = *outside;
    l.name = texts.emplace_back(std::move(*name)).c_str();
    l.blend_mode = blend->empty() ? nullptr : texts.emplace_back(std::move(*blend)).c_str();
    return true;
}

}
