#include "platform/BackgroundBlur.h"

#ifdef DOODLEBOLD_WAYLAND_BLUR

#include "ext-background-effect-v1-client.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPlatformSurfaceEvent>
#include <QSet>
#include <QWindow>

#include <wayland-client.h>

#include <cstring>
#include <functional>
#include <memory>

namespace doodlebold::platform::background_blur {

namespace {

// Qt's Wayland plugin destroys the wl_surface on hide and makes a new one on show, often at the same address: a kept effect
// would send its region for a dead surface (protocol error "wl_surface was destroyed" ends the connection).
class SurfaceWatch : public QObject {
public:
    explicit SurfaceWatch(std::function<void(QWindow*)> release) : m_release(std::move(release)) {}
    bool eventFilter(QObject* watched, QEvent* event) override {
        const bool gone = event->type() == QEvent::Hide ||
                          (event->type() == QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                                                                           QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
        if (gone)
            m_release(static_cast<QWindow*>(watched));
        return false;
    }

private:
    std::function<void(QWindow*)> m_release;
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

    bool blurs() const { return m_display && (m_capabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR); }

    bool setRegion(QWindow* window, const QRegion& region) {
        if (!blurs() || !window)
            return false;
        auto* surface =
            reinterpret_cast<wl_surface*>(window->winId());  // NOLINT(performance-no-int-to-ptr): Qt hands the surface out as a WId
        if (!surface || std::strcmp(wl_proxy_get_class(reinterpret_cast<wl_proxy*>(surface)), "wl_surface") != 0)
            return false;
        if (!m_watched.contains(window)) {
            m_watched.insert(window);
            window->installEventFilter(&m_watch);
            QObject::connect(window, &QObject::destroyed, qGuiApp, [this, window] {
                release(window);
                m_watched.remove(window);
            });
        }
        Effect& effect = m_effects[window];
        if (effect.surface != surface) {
            if (effect.object)
                ext_background_effect_surface_v1_destroy(effect.object);
            effect.surface = surface;
            effect.object = ext_background_effect_manager_v1_get_background_effect(m_manager, surface);
        }
        if (region.isEmpty()) {
            ext_background_effect_surface_v1_set_blur_region(effect.object, nullptr);
        } else {
            wl_region* area = wl_compositor_create_region(m_compositor);
            for (const QRect& r : region)
                wl_region_add(area, r.x(), r.y(), r.width(), r.height());
            ext_background_effect_surface_v1_set_blur_region(effect.object, area);
            wl_region_destroy(area);
        }
        wl_display_flush(m_display);
        return true;
    }

    void release(QWindow* window) {
        const auto it = m_effects.find(window);
        if (it == m_effects.end())
            return;
        if (it->object)
            ext_background_effect_surface_v1_destroy(it->object);
        m_effects.erase(it);
        if (m_display)
            wl_display_flush(m_display);
    }

private:
    struct Effect {
        wl_surface* surface = nullptr;
        ext_background_effect_surface_v1* object = nullptr;
    };

    bool connect() {
        if (qEnvironmentVariable("DOODLEBOLD_BLUR") == QLatin1String("0") || !qGuiApp ||
            !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return false;
        auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!wayland || !wayland->display())
            return false;
        m_display = wayland->display();
        m_queue = wl_display_create_queue(m_display);
        m_wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(m_display));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_wrapper), m_queue);
        m_registry = wl_display_get_registry(m_wrapper);
        static const wl_registry_listener registryListener = {
            [](void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t) {
                auto* self = static_cast<Binding*>(data);
                if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0)
                    self->m_manager = static_cast<ext_background_effect_manager_v1*>(
                        wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
                else if (std::strcmp(interface, wl_compositor_interface.name) == 0)
                    self->m_compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
            },
            [](void*, wl_registry*, std::uint32_t) {},
        };
        wl_registry_add_listener(m_registry, &registryListener, this);
        wl_display_roundtrip_queue(m_display, m_queue);
        if (!m_manager || !m_compositor) {
            disconnect();
            return false;
        }
        static const ext_background_effect_manager_v1_listener managerListener = {
            [](void* data, ext_background_effect_manager_v1*, std::uint32_t flags) { static_cast<Binding*>(data)->m_capabilities = flags; },
        };
        ext_background_effect_manager_v1_add_listener(m_manager, &managerListener, this);
        wl_display_roundtrip_queue(m_display, m_queue);
        m_awake = QObject::connect(QAbstractEventDispatcher::instance(qGuiApp->thread()), &QAbstractEventDispatcher::awake,
                                   [this] { wl_display_dispatch_queue_pending(m_display, m_queue); });
        m_quit = QObject::connect(qGuiApp, &QCoreApplication::aboutToQuit, [this] { disconnect(); });
        return true;
    }

    void disconnect() {
        QObject::disconnect(m_awake);
        QObject::disconnect(m_quit);
        for (const Effect& e : std::as_const(m_effects))
            if (e.object)
                ext_background_effect_surface_v1_destroy(e.object);
        m_effects.clear();
        if (m_manager)
            ext_background_effect_manager_v1_destroy(m_manager);
        if (m_compositor)
            wl_compositor_destroy(m_compositor);
        if (m_registry)
            wl_registry_destroy(m_registry);
        if (m_wrapper)
            wl_proxy_wrapper_destroy(m_wrapper);
        if (m_queue)
            wl_event_queue_destroy(m_queue);
        m_manager = nullptr, m_compositor = nullptr, m_registry = nullptr, m_wrapper = nullptr, m_queue = nullptr;
        m_display = nullptr;  // Qt's connection: not ours to close
        m_capabilities = 0;
    }

    wl_display* m_display = nullptr;
    wl_event_queue* m_queue = nullptr;
    wl_display* m_wrapper = nullptr;
    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    ext_background_effect_manager_v1* m_manager = nullptr;
    std::uint32_t m_capabilities = 0;
    QHash<QWindow*, Effect> m_effects;
    QSet<QWindow*> m_watched;
    SurfaceWatch m_watch{[this](QWindow* window) { release(window); }};
    QMetaObject::Connection m_awake, m_quit;
};

}

bool available() {
    const Binding* b = Binding::instance();
    return b && b->blurs();
}

bool setRegion(QWindow* window, const QRegion& region) {
    Binding* b = Binding::instance();
    return b && b->setRegion(window, region);
}

void prepare(QWindow*, const QRegion&) {}

void release(QWindow* window) {
    if (Binding* b = Binding::instance())
        b->release(window);
}

}

#elif defined(Q_OS_WIN)

#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QTransform>
#include <QWindow>

#include <windows.h>

#include <dwmapi.h>

namespace doodlebold::platform::background_blur {

namespace {

// Windows 11 22H2's documented backdrop, for a window with a frame; not every SDK's headers name these yet.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kSystemBackdropType = 38;
constexpr int kBackdropNone = 1;
constexpr int kBackdropAcrylic = 3;

// Menus and tool tips don't take the backdrop reliably: they get acrylic through this undocumented call instead, looked up
// at run time so that its absence only leaves them unblurred.
struct AccentPolicy {
    DWORD state;
    DWORD flags;
    DWORD gradientColor;
    DWORD animationId;
};
struct CompositionData {
    DWORD attribute;
    void* data;
    SIZE_T size;
};
constexpr DWORD kAccentPolicyAttribute = 19;
constexpr DWORD kAccentDisabled = 0;
constexpr DWORD kAccentAcrylic = 4;
constexpr DWORD kAcrylicTint = 0x01000000;  // the window paints its own colour; a tint of alpha 0 turns the window black

bool setAccent(HWND hwnd, DWORD state) {
    using SetComposition = BOOL(WINAPI*)(HWND, CompositionData*);
    static const auto set = reinterpret_cast<SetComposition>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
    if (!set)
        return false;
    AccentPolicy accent{state, 0, kAcrylicTint, 0};
    CompositionData data{kAccentPolicyAttribute, &accent, sizeof(accent)};
    return set(hwnd, &data);
}

bool setBackdrop(HWND hwnd, bool on) {
    const MARGINS margins{on ? -1 : 0, on ? -1 : 0, on ? -1 : 0, on ? -1 : 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const BOOL dark = on;
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));
    const int backdrop = on ? kBackdropAcrylic : kBackdropNone;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, kSystemBackdropType, &backdrop, sizeof(backdrop)));
}

}

bool available() {
    return qEnvironmentVariable("DOODLEBOLD_BLUR") != QLatin1String("0") && qGuiApp &&
           QGuiApplication::platformName() == QLatin1String("windows") &&
           QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621);
}

bool setRegion(QWindow* window, const QRegion& region) {
    if (!window || !available())
        return false;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    // By the window's kind, not WS_EX_LAYERED: a menu is layered only during Qt's opening animation, which a menu opened by
    // hovering over the menu bar does not play.
    if (window->type() != Qt::Popup && window->type() != Qt::ToolTip)
        return setBackdrop(hwnd, !region.isEmpty());
    if (!setAccent(hwnd, region.isEmpty() ? kAccentDisabled : kAccentAcrylic))
        return false;
    // Without a layered window's per-pixel alpha, the menu's translucent pixels need the frame extended over them.
    const int extend = region.isEmpty() ? 0 : -1;
    const MARGINS margins{extend, extend, extend, extend};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // Acrylic fills the whole window: its region gives a popup its rounded corners. Windows owns the HRGN from here, and
    // counts in device pixels.
    const qreal ratio = window->devicePixelRatio();
    SetWindowRgn(hwnd, region.isEmpty() ? nullptr : QTransform::fromScale(ratio, ratio).map(region).toHRGN(), TRUE);
    return true;
}

void prepare(QWindow* window, const QRegion& region) {
    setRegion(window, region);
}

void release(QWindow*) {}

}

#else

namespace doodlebold::platform::background_blur {

bool available() {
    return false;
}

bool setRegion(QWindow*, const QRegion&) {
    return false;
}

void prepare(QWindow*, const QRegion&) {}

void release(QWindow*) {}

}

#endif
