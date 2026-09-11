#include "window_activation.h"

#if defined(_WIN32)

#include <algorithm>
#include <vector>
#include <windows.h>

namespace CoreDeck {
    namespace {
        struct WindowSearch {
            const std::vector<ProcessId> *ProcessIds = nullptr;
            HWND Window = nullptr;
        };

        BOOL CALLBACK FindProcessWindow(HWND window, LPARAM data) {
            auto *search = reinterpret_cast<WindowSearch *>(data);
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
                return TRUE;
            }

            DWORD ownerPid = 0;
            GetWindowThreadProcessId(window, &ownerPid);
            if (std::ranges::find(*search->ProcessIds, ownerPid) == search->ProcessIds->end()) {
                return TRUE;
            }

            search->Window = window;
            return FALSE;
        }
    }

    bool ActivateProcessWindow(const ProcessId pid) {
        if (pid == 0) {
            return false;
        }

        std::vector<ProcessId> processIds;
        CollectProcessTreePids(pid, processIds);
        WindowSearch search{.ProcessIds = &processIds};
        EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        if (search.Window == nullptr) {
            return false;
        }

        if (IsIconic(search.Window)) {
            ShowWindow(search.Window, SW_RESTORE);
        } else {
            ShowWindow(search.Window, SW_SHOW);
        }
        BringWindowToTop(search.Window);
        return SetForegroundWindow(search.Window) != FALSE;
    }
}

#elif defined(__linux__)

#include <algorithm>
#include <cstdint>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace CoreDeck {
    namespace {
        bool WindowBelongsToProcess(
            Display *display,
            Window window,
            Atom processIdAtom,
            const std::vector<ProcessId> &processIds
        ) {
            Atom actualType = None;
            int actualFormat = 0;
            unsigned long itemCount = 0;
            unsigned long bytesAfter = 0;
            unsigned char *data = nullptr;
            const int status = XGetWindowProperty(
                display,
                window,
                processIdAtom,
                0,
                1,
                False,
                XA_CARDINAL,
                &actualType,
                &actualFormat,
                &itemCount,
                &bytesAfter,
                &data
            );

            bool matches = false;
            if (status == Success && actualType == XA_CARDINAL && actualFormat == 32 && itemCount == 1 && data != nullptr) {
                const auto ownerPid = static_cast<ProcessId>(*reinterpret_cast<unsigned long *>(data));
                matches = std::ranges::find(processIds, ownerPid) != processIds.end();
            }
            if (data != nullptr) {
                XFree(data);
            }
            return matches;
        }

        Window FindProcessWindow(
            Display *display,
            Window parent,
            Atom processIdAtom,
            const std::vector<ProcessId> &processIds
        ) {
            if (WindowBelongsToProcess(display, parent, processIdAtom, processIds)) {
                XWindowAttributes attributes{};
                if (XGetWindowAttributes(display, parent, &attributes) != 0 &&
                    attributes.map_state != IsUnmapped) {
                    return parent;
                }
            }

            Window root = None;
            Window parentWindow = None;
            Window *children = nullptr;
            unsigned int childCount = 0;
            if (XQueryTree(display, parent, &root, &parentWindow, &children, &childCount) == 0) {
                return None;
            }

            Window result = None;
            for (unsigned int i = 0; i < childCount && result == None; i++) {
                result = FindProcessWindow(display, children[i], processIdAtom, processIds);
            }
            if (children != nullptr) {
                XFree(children);
            }
            return result;
        }
    }

    bool ActivateProcessWindow(const ProcessId pid) {
        if (pid <= 0) {
            return false;
        }

        Display *display = XOpenDisplay(nullptr);
        if (display == nullptr) {
            return false;
        }

        std::vector<ProcessId> processIds;
        CollectProcessTreePids(pid, processIds);
        const Window root = DefaultRootWindow(display);
        const Atom processIdAtom = XInternAtom(display, "_NET_WM_PID", True);
        const Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
        if (processIdAtom == None || activeWindowAtom == None) {
            XCloseDisplay(display);
            return false;
        }

        const Window target = FindProcessWindow(display, root, processIdAtom, processIds);
        if (target == None) {
            XCloseDisplay(display);
            return false;
        }

        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.serial = 0;
        event.xclient.send_event = True;
        event.xclient.display = display;
        event.xclient.window = target;
        event.xclient.message_type = activeWindowAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // Source indication: normal application.
        event.xclient.data.l[1] = CurrentTime;

        const Status sent = XSendEvent(
            display,
            root,
            False,
            SubstructureRedirectMask | SubstructureNotifyMask,
            &event
        );
        XMapRaised(display, target);
        XFlush(display);
        XCloseDisplay(display);
        return sent != 0;
    }
}

#else

namespace CoreDeck {
    bool ActivateProcessWindow(const ProcessId) {
        return false;
    }
}

#endif
