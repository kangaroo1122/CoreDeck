#import <Cocoa/Cocoa.h>

#include "window_activation.h"

namespace CoreDeck {
    bool ActivateProcessWindow(const ProcessId pid) {
        if (pid <= 0) {
            return false;
        }

        @autoreleasepool {
            NSRunningApplication *application =
                [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            if (application == nil || application.terminated) {
                return false;
            }

            if (application.hidden) {
                [application unhide];
            }

            return [application activateWithOptions:
                NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
        }
    }
}
