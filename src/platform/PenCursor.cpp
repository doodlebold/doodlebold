#include "platform/PenCursor.h"

#ifdef DOODLEBOLD_WAYLAND_PEN_CURSOR

#include "cursor-shape-v1-client.h"
#include "tablet-unstable-v2-client.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>

#include <wayland-client.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace doodlebold::platform::pen_cursor {

namespace {

std::uint32_t shapeName(Qt::CursorShape shape) {
    switch (shape) {
    case Qt::ArrowCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    case Qt::CrossCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
    case Qt::IBeamCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    case Qt::PointingHandCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    case Qt::OpenHandCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    case Qt::ClosedHandCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    case Qt::SizeHorCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
    case Qt::SizeVerCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
    case Qt::SizeAllCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL;
    case Qt::WaitCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
    case Qt::BusyCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
    case Qt::ForbiddenCursor: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
    default: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
}

class Binding;

struct Tool {
    Binding* owner = nullptr;
    zwp_tablet_tool_v2* tool = nullptr;
    wp_cursor_shape_device_v1* shapes = nullptr;
    std::uint32_t serial = 0;  // of the last proximity-in: a cursor request must carry it
    bool inProximity = false;
};

class Binding {
public:
    static Binding* instance() {
        static std::unique_ptr<Binding> binding = [] {
            auto b = std::make_unique<Binding>();
            return b->connect() ? std::move(b) : nullptr;
        }();
        return binding.get();
    }

    // Runs after QGuiApplication, when Qt's connection is gone: nothing is released here, aboutToQuit does.
    ~Binding() = default;

    bool connected() const { return m_display != nullptr; }

    void setWanted(Qt::CursorShape shape) {
        if (shape == m_wanted || !m_display)
            return;
        m_wanted = shape;
        for (Tool* t : m_tools)
            if (t->inProximity)
                apply(*t);
        wl_display_flush(m_display);
    }

private:
    bool connect() {
        if (qEnvironmentVariable("DOODLEBOLD_PEN_CURSOR") == QLatin1String("0") || !qGuiApp ||
            !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return false;
        auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!wayland || !wayland->display() || !wayland->seat())
            return false;
        m_display = wayland->display();
        m_queue = wl_display_create_queue(m_display);
        m_wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(m_display));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_wrapper), m_queue);
        m_registry = wl_display_get_registry(m_wrapper);
        static const wl_registry_listener registryListener = {
            [](void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t) {
                auto* self = static_cast<Binding*>(data);
                if (std::strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0)
                    self->m_manager =
                        static_cast<zwp_tablet_manager_v2*>(wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1));
                else if (std::strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
                    self->m_shapeManager = static_cast<wp_cursor_shape_manager_v1*>(
                        wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1));
            },
            [](void*, wl_registry*, std::uint32_t) {},
        };
        wl_registry_add_listener(m_registry, &registryListener, this);
        wl_display_roundtrip_queue(m_display, m_queue);
        if (!m_manager) {
            disconnect();
            return false;
        }
        m_seat = zwp_tablet_manager_v2_get_tablet_seat(m_manager, wayland->seat());
        static const zwp_tablet_seat_v2_listener seatListener = {
            [](void*, zwp_tablet_seat_v2*, zwp_tablet_v2* tablet) { zwp_tablet_v2_destroy(tablet); },
            [](void* data, zwp_tablet_seat_v2*, zwp_tablet_tool_v2* tool) { static_cast<Binding*>(data)->addTool(tool); },
            [](void*, zwp_tablet_seat_v2*, zwp_tablet_pad_v2* pad) { zwp_tablet_pad_v2_destroy(pad); },
        };
        zwp_tablet_seat_v2_add_listener(m_seat, &seatListener, this);
        wl_display_roundtrip_queue(m_display, m_queue);
        // Qt reads the connection; events for this queue are handled whenever Qt's event loop wakes (the same tablet events).
        m_awake = QObject::connect(QAbstractEventDispatcher::instance(qGuiApp->thread()), &QAbstractEventDispatcher::awake,
                                   [this] { wl_display_dispatch_queue_pending(m_display, m_queue); });
        m_quit = QObject::connect(qGuiApp, &QCoreApplication::aboutToQuit, [this] { disconnect(); });
        return true;
    }

    void disconnect() {
        QObject::disconnect(m_awake);
        QObject::disconnect(m_quit);
        for (Tool* t : m_tools)
            removeTool(t, false);
        m_tools.clear();
        if (m_seat)
            zwp_tablet_seat_v2_destroy(m_seat);
        if (m_shapeManager)
            wp_cursor_shape_manager_v1_destroy(m_shapeManager);
        if (m_manager)
            zwp_tablet_manager_v2_destroy(m_manager);
        if (m_registry)
            wl_registry_destroy(m_registry);
        if (m_wrapper)
            wl_proxy_wrapper_destroy(m_wrapper);
        if (m_queue)
            wl_event_queue_destroy(m_queue);
        m_seat = nullptr, m_shapeManager = nullptr, m_manager = nullptr, m_registry = nullptr, m_wrapper = nullptr;
        m_queue = nullptr;
        m_display = nullptr;  // Qt's connection: not ours to close
    }

    void addTool(zwp_tablet_tool_v2* proxy) {
        auto* t = new Tool{this, proxy};
        if (m_shapeManager)
            t->shapes = wp_cursor_shape_manager_v1_get_tablet_tool_v2(m_shapeManager, proxy);
        m_tools.push_back(t);
        static const zwp_tablet_tool_v2_listener toolListener = [] {
            zwp_tablet_tool_v2_listener l{};
            l.type = [](auto...) {};
            l.hardware_serial = [](auto...) {};
            l.hardware_id_wacom = [](auto...) {};
            l.capability = [](auto...) {};
            l.done = [](auto...) {};
            l.removed = [](void* data, zwp_tablet_tool_v2*) {
                auto* tool = static_cast<Tool*>(data);
                tool->owner->removeTool(tool, true);
            };
            l.proximity_in = [](void* data, zwp_tablet_tool_v2*, std::uint32_t serial, zwp_tablet_v2*, wl_surface*) {
                auto* tool = static_cast<Tool*>(data);
                tool->serial = serial;
                tool->inProximity = true;
                tool->owner->apply(*tool);
                wl_display_flush(tool->owner->m_display);
                // Qt sets the pen's cursor to the mouse pointer's on the same event, after this or before it: set ours again
                // once it is done.
                QTimer::singleShot(0, qGuiApp, [owner = tool->owner, proxy = tool->tool] { owner->applyAgain(proxy); });
            };
            l.proximity_out = [](void* data, zwp_tablet_tool_v2*) { static_cast<Tool*>(data)->inProximity = false; };
            l.down = [](auto...) {};
            l.up = [](auto...) {};
            l.motion = [](auto...) {};
            l.pressure = [](auto...) {};
            l.distance = [](auto...) {};
            l.tilt = [](auto...) {};
            l.rotation = [](auto...) {};
            l.slider = [](auto...) {};
            l.wheel = [](auto...) {};
            l.button = [](auto...) {};
            l.frame = [](auto...) {};
            return l;
        }();
        zwp_tablet_tool_v2_add_listener(proxy, &toolListener, t);
    }

    void applyAgain(zwp_tablet_tool_v2* proxy) {
        if (!m_display)
            return;
        for (Tool* t : m_tools)
            if (t->tool == proxy && t->inProximity)
                apply(*t);
        wl_display_flush(m_display);
    }

    void removeTool(Tool* t, bool erase) {
        if (t->shapes)
            wp_cursor_shape_device_v1_destroy(t->shapes);
        zwp_tablet_tool_v2_destroy(t->tool);
        if (erase)
            std::erase(m_tools, t);
        delete t;
    }

    // A null cursor surface hides the cursor; a shape is set by name where cursor-shape-v1 exists, else the default.
    void apply(const Tool& t) {
        if (m_wanted == Qt::BlankCursor || !t.shapes)
            zwp_tablet_tool_v2_set_cursor(t.tool, t.serial, nullptr, 0, 0);
        else
            wp_cursor_shape_device_v1_set_shape(t.shapes, t.serial, shapeName(m_wanted));
    }

    wl_display* m_display = nullptr;
    wl_event_queue* m_queue = nullptr;
    wl_display* m_wrapper = nullptr;
    wl_registry* m_registry = nullptr;
    zwp_tablet_manager_v2* m_manager = nullptr;
    wp_cursor_shape_manager_v1* m_shapeManager = nullptr;
    zwp_tablet_seat_v2* m_seat = nullptr;
    std::vector<Tool*> m_tools;
    Qt::CursorShape m_wanted = Qt::ArrowCursor;
    QMetaObject::Connection m_awake, m_quit;
};

}

bool available() {
    const Binding* b = Binding::instance();
    return b && b->connected();
}

void set(Qt::CursorShape shape) {
    if (Binding* b = Binding::instance())
        b->setWanted(shape);
}

}

#else

namespace doodlebold::platform::pen_cursor {

bool available() {
    return false;
}

void set(Qt::CursorShape) {}

}

#endif
