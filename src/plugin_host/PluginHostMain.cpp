// doodlebold-plugin-host: one format plugin, run apart from Doodlebold (plugin-process-design). It loads the plugin, keeps to its
// restrictions (PluginHostRestrictions), and answers Doodlebold's messages (PluginHostProtocol.h) over its standard input and output,
// turning them into the plugin's calls and the plugin's calls back into messages. Plain C++ and the system's own libraries: it is
// started where it may read nothing else.
#include "plugin_host/PluginHostProtocol.h"
#include "plugin_host/PluginHostRestrictions.h"

#include <doodlebold/format_plugin.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

using namespace doodlebold::plugin_host;

namespace {

// How long the helper waits with nothing to do before it ends: the owner's choice, so a plugin used again soon needn't start again,
// and one not used doesn't keep its memory.
constexpr int kIdleExitMs = 60000;

// ---- Standard input and output ----

#ifdef _WIN32
HANDLE g_in = INVALID_HANDLE_VALUE;
HANDLE g_out = INVALID_HANDLE_VALUE;

bool readExact(void* data, std::size_t size) {
    auto* p = static_cast<char*>(data);
    while (size > 0) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        if (!ReadFile(g_in, p, want, &got, nullptr) || got == 0)
            return false;
        p += got;
        size -= got;
    }
    return true;
}

bool writeAll(const void* data, std::size_t size) {
    const auto* p = static_cast<const char*>(data);
    while (size > 0) {
        DWORD put = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        if (!WriteFile(g_out, p, want, &put, nullptr) || put == 0)
            return false;
        p += put;
        size -= put;
    }
    return true;
}

// Whether a message starts arriving within `ms`.
bool waitForInput(int ms) {
    constexpr int kPollMs = 20;  // how often an anonymous pipe is looked at: it has no event to wait on
    for (int waited = 0; waited < ms; waited += kPollMs) {
        DWORD available = 0;
        if (!PeekNamedPipe(g_in, nullptr, 0, nullptr, &available, nullptr))
            return true;  // closed: the read that follows says so
        if (available > 0)
            return true;
        Sleep(kPollMs);
    }
    return false;
}
#else
int g_in = -1;
int g_out = -1;

bool readExact(void* data, std::size_t size) {
    auto* p = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t got = ::read(g_in, p, size);
        if (got <= 0)
            return false;
        p += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
}

bool writeAll(const void* data, std::size_t size) {
    const auto* p = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t put = ::write(g_out, p, size);
        if (put <= 0)
            return false;
        p += put;
        size -= static_cast<std::size_t>(put);
    }
    return true;
}

bool waitForInput(int ms) {
    pollfd in{g_in, POLLIN, 0};
    return ::poll(&in, 1, ms) != 0;
}
#endif

// ---- Messages ----

[[noreturn]] void endHelper(int code) {
    std::_Exit(code);
}

void send(Message type, const Bytes& body) {
    const Header h{static_cast<std::uint32_t>(type), body.data().size()};
    if (!writeAll(&h, sizeof h) || !writeAll(body.data().data(), body.data().size()))
        endHelper(1);  // Doodlebold has gone
}

// The most a message of `type` may carry from Doodlebold.
std::uint64_t mostFor(Message type) {
    switch (type) {
    case Message::Read: return kMostFileBytes + 64;
    default: return kMostMessageBytes;
    }
}

// The next message; the helper ends when Doodlebold has gone or sends what doesn't fit.
Message receive(Bytes& body) {
    Header h{};
    if (!readExact(&h, sizeof h) || h.length > mostFor(static_cast<Message>(h.type)))
        endHelper(1);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(h.length));
    if (!readExact(data.data(), data.size()))
        endHelper(1);
    body = Bytes(std::move(data));
    return static_cast<Message>(h.type);
}

// Doodlebold's answer to a callback: Reply, or the helper ends.
std::int32_t awaitReply() {
    Bytes body;
    if (receive(body) != Message::Reply)
        endHelper(1);
    const auto value = body.get<std::int32_t>();
    if (!value)
        endHelper(1);
    return *value;
}

void sendText(Message type, const char* text) {
    Bytes b;
    b.putText(text);
    send(type, b);
}

// ---- Reading: the plugin's callbacks, each a message ----

struct ReadContext {
    std::vector<std::uint8_t> file;
};

int setCanvas(void*, std::int32_t width, std::int32_t height, double ppi, const std::uint8_t background[4]) {
    Bytes b;
    b.put(width), b.put(height), b.put(ppi);
    b.putBytes(background, 4);
    send(Message::SetCanvas, b);
    return awaitReply();
}

// A plane, `bytesPerValue` × `channels` per value, in bands of kBandRows rows: neither side holds it twice whole.
void sendPlane(Plane plane, const void* data, std::size_t stride, std::int32_t width, std::int32_t height, std::size_t valueBytes) {
    const std::size_t rowBytes = std::size_t(width) * valueBytes;
    for (std::int32_t first = 0; first < height; first += static_cast<std::int32_t>(kBandRows)) {
        const auto rows = static_cast<std::uint32_t>(std::min<std::int32_t>(static_cast<std::int32_t>(kBandRows), height - first));
        Bytes b;
        b.put(static_cast<std::uint8_t>(plane));
        b.put(static_cast<std::uint32_t>(first));
        b.put(rows);
        for (std::uint32_t r = 0; r < rows; ++r)
            b.putBytes(static_cast<const std::uint8_t*>(data) + (std::size_t(first) + r) * stride, rowBytes);
        send(Message::LayerBand, b);
    }
}

std::int32_t addLayer(void*, const doodlebold_layer* l) {
    if (!l || l->struct_size < sizeof(doodlebold_layer))
        return -1;
    Bytes b;
    putLayer(b, *l);
    send(Message::AddLayer, b);
    if (l->kind == DOODLEBOLD_LAYER_PIXELS && l->width > 0 && l->height > 0 && l->pixels)
        sendPlane(Plane::Pixels, l->pixels, l->stride, l->width, l->height, 8);
    if (l->has_mask && l->mask_width > 0 && l->mask_height > 0 && l->mask)
        sendPlane(Plane::Mask, l->mask, l->mask_stride, l->mask_width, l->mask_height, 2);
    send(Message::LayerBand, Bytes());  // an empty band: the layer's planes are all sent
    return awaitReply();
}

void note(void*, const char* text) {
    sendText(Message::Note, text);
}

void setError(void*, const char* text) {
    sendText(Message::SetError, text);
}

// ---- Writing ----

struct WriteContext {
    std::deque<std::string> texts;  // what the layers' names and blend keys point to
};

// Fills `out` (rows `stride` apart) from Doodlebold's bands of a plane.
int getPlane(Plane plane, std::uint32_t layer, std::uint16_t* out, std::size_t stride, std::int32_t width, std::int32_t height,
             std::size_t valueBytes) {
    if (!out)
        return 0;
    Bytes ask;
    ask.put(static_cast<std::uint8_t>(plane));
    ask.put(layer);
    send(Message::GetPlane, ask);
    const std::size_t rowBytes = std::size_t(width) * valueBytes;
    if (stride < rowBytes)
        endHelper(1);
    for (;;) {
        Bytes body;
        const Message type = receive(body);
        if (type == Message::Reply) {
            const auto ok = body.get<std::int32_t>();
            return ok ? *ok : 0;
        }
        const auto first = body.get<std::uint32_t>(), rows = body.get<std::uint32_t>();
        if (type != Message::Band || !first || !rows || std::uint64_t(*first) + *rows > std::uint64_t(height))
            endHelper(1);
        const std::uint8_t* data = body.getBytes(std::size_t(*rows) * rowBytes);
        if (!data)
            endHelper(1);
        for (std::uint32_t r = 0; r < *rows; ++r)
            std::memcpy(reinterpret_cast<std::uint8_t*>(out) + (std::size_t(*first) + r) * stride, data + r * rowBytes, rowBytes);
    }
}

const doodlebold_write_host* g_writeHost = nullptr;

int getPixels(void*, std::uint32_t layer, std::uint16_t* out, std::size_t stride) {
    if (layer >= g_writeHost->layer_count)
        return 0;
    const doodlebold_layer& l = g_writeHost->layers[layer];
    return getPlane(Plane::Pixels, layer, out, stride, l.width, l.height, 8);
}

int getMask(void*, std::uint32_t layer, std::uint16_t* out, std::size_t stride) {
    if (layer >= g_writeHost->layer_count)
        return 0;
    const doodlebold_layer& l = g_writeHost->layers[layer];
    return getPlane(Plane::Mask, layer, out, stride, l.mask_width, l.mask_height, 2);
}

int getComposite(void*, std::uint16_t* out, std::size_t stride) {
    return getPlane(Plane::Composite, 0, out, stride, g_writeHost->width, g_writeHost->height, 8);
}

// The plugin's writes, gathered: a plugin may write a row or a pixel at a time, and each message waits for Doodlebold's answer.
// Sent when this much has gathered, and at the end of the write: a piece well within a message.
constexpr std::size_t kWritePiece = 8u << 20;
Bytes g_pendingWrite;
bool g_writeRefused = false;  // Doodlebold refused a piece: every later write fails, and so does the save

void sendPendingWrite() {
    if (g_pendingWrite.data().empty() || g_writeRefused)
        return;
    send(Message::WriteBytes, g_pendingWrite);
    g_pendingWrite = Bytes();
    g_writeRefused = !awaitReply();
}

int writeBytes(void*, const void* bytes, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    while (size > 0 && !g_writeRefused) {
        const std::size_t piece = std::min(size, kWritePiece - g_pendingWrite.data().size());
        g_pendingWrite.putBytes(p, piece);
        p += piece;
        size -= piece;
        if (g_pendingWrite.data().size() >= kWritePiece)
            sendPendingWrite();
    }
    return g_writeRefused ? 0 : 1;
}

// ---- The calls Doodlebold asks for ----

const doodlebold_plugin* g_plugin = nullptr;

const doodlebold_format* formatAt(Bytes& body) {
    const auto index = body.get<std::uint32_t>();
    if (!index || *index >= g_plugin->format_count)
        endHelper(1);
    return &g_plugin->formats[*index];
}

void describe() {
    Bytes b;
    b.put(kProtocolVersion);
    b.put(g_plugin->api_version);
    b.putText(g_plugin->name);
    b.put(g_plugin->format_count);
    for (std::uint32_t i = 0; i < g_plugin->format_count; ++i) {
        const doodlebold_format& f = g_plugin->formats[i];
        b.put(f.struct_size);
        b.putText(f.id);
        b.putText(f.name);
        b.putText(f.extensions);
        b.put(f.flags);
        b.put(static_cast<std::uint8_t>(f.recognizes != nullptr));
        b.put(static_cast<std::uint8_t>(f.read != nullptr));
        b.put(static_cast<std::uint8_t>(f.write != nullptr));
    }
    send(Message::Description, b);
}

void recognize(Bytes& body) {
    const doodlebold_format* f = formatAt(body);
    const std::size_t n = body.left();
    const std::uint8_t* head = body.getBytes(n);
    Bytes b;
    b.put(static_cast<std::uint8_t>(f->recognizes && f->recognizes(head, n) != 0));
    send(Message::Recognized, b);
}

void read(Bytes& body) {
    const doodlebold_format* f = formatAt(body);
    const std::size_t n = body.left();
    const std::uint8_t* file = body.getBytes(n);
    ReadContext context;
    const doodlebold_read_host host{sizeof(doodlebold_read_host), &context, file, n, setCanvas, addLayer, note, setError};
    Bytes done;
    done.put(static_cast<std::int32_t>(f->read ? f->read(&host) : DOODLEBOLD_ERROR_UNSUPPORTED));
    send(Message::ReadDone, done);
}

void write(Bytes& body) {
    const doodlebold_format* f = formatAt(body);
    doodlebold_write_host host{};
    host.struct_size = sizeof host;
    const auto width = body.get<std::int32_t>(), height = body.get<std::int32_t>();
    const auto ppi = body.get<double>();
    const std::uint8_t* background = body.getBytes(4);
    const auto count = body.get<std::uint32_t>();
    if (!width || !height || !ppi || !background || !count || *count > kMostLayers)
        endHelper(1);
    WriteContext context;
    std::vector<doodlebold_layer> layers(*count);
    for (doodlebold_layer& l : layers)
        if (!getLayer(body, l, context.texts))
            endHelper(1);
    host.context = &context;
    host.width = *width, host.height = *height, host.ppi = *ppi;
    std::memcpy(host.background, background, 4);
    host.layer_count = *count;
    host.layers = layers.data();
    host.get_pixels = getPixels, host.get_mask = getMask, host.get_composite = getComposite;
    host.write = writeBytes, host.set_error = setError;
    g_writeHost = &host;
    g_pendingWrite = Bytes();
    g_writeRefused = false;
    const std::int32_t result = f->write ? f->write(&host) : DOODLEBOLD_ERROR_UNSUPPORTED;
    sendPendingWrite();
    g_writeHost = nullptr;
    Bytes done;
    done.put(result == DOODLEBOLD_OK && g_writeRefused ? std::int32_t{DOODLEBOLD_ERROR_WRITE_FAILED} : result);
    send(Message::WriteDone, done);
}

}

// The protocol keeps copies of standard input and output of its own.
bool keepProtocolStreams() {
#ifdef _WIN32
    const auto own = [](DWORD which) {
        HANDLE copy = nullptr;
        DuplicateHandle(GetCurrentProcess(), GetStdHandle(which), GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
        return copy;
    };
    g_in = own(STD_INPUT_HANDLE);
    g_out = own(STD_OUTPUT_HANDLE);
    return g_in && g_out;
#else
    g_in = fcntl(0, F_DUPFD_CLOEXEC, 3);
    g_out = fcntl(1, F_DUPFD_CLOEXEC, 3);
    return g_in >= 0 && g_out >= 0;
#endif
}

// The plugin's standard input and output become the null device: a plugin that prints, or reads its input, would otherwise break
// the messages.
const char* setStandardStreamsAside() {
#ifdef _WIN32
    // A C library the plugin brings takes its streams from these as it loads.
    const HANDLE null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (null == INVALID_HANDLE_VALUE)
        return "the helper can't open the null device";
    if (!SetStdHandle(STD_INPUT_HANDLE, null) || !SetStdHandle(STD_OUTPUT_HANDLE, null))
        return "the helper can't set its standard streams aside";
#ifndef _MSC_VER
    // MinGW's C library is a system library the plugin may share, with its streams open already. Built with MSVC, the helper's
    // own is linked in (/MT), so no plugin's is ever the same one.
    const int crtNull = _open("NUL", _O_RDWR);
    if (crtNull < 0 || _dup2(crtNull, 0) != 0 || _dup2(crtNull, 1) != 0)
        return "the helper can't set its C library's standard streams aside";
#endif
    return nullptr;
#else
    const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null < 0 || dup2(null, 0) != 0 || dup2(null, 1) != 1)
        return "the helper can't set its standard streams aside";
    return nullptr;
#endif
}

int main(int argc, char** argv) {
    if (!keepProtocolStreams())
        endHelper(1);  // with nothing to say it on
    if (const char* why = setStandardStreamsAside())
        sendText(Message::Failed, why), endHelper(1);
    if (const char* why = restrictBeforeLoading(argc, argv))
        sendText(Message::Failed, why), endHelper(1);
    const doodlebold_plugin* plugin = nullptr;
    if (const char* why = loadPlugin(argc, argv, plugin))
        sendText(Message::Failed, why), endHelper(1);
    if (const char* why = restrictAfterLoading())
        sendText(Message::Failed, why), endHelper(1);
    g_plugin = plugin;
    for (;;) {
        if (!waitForInput(kIdleExitMs))
            endHelper(0);
        Bytes body;
        switch (receive(body)) {
        case Message::Describe: describe(); break;
        case Message::Recognize: recognize(body); break;
        case Message::Read: read(body); break;
        case Message::Write: write(body); break;
        case Message::Quit: endHelper(0);
        default: endHelper(1);
        }
    }
}
