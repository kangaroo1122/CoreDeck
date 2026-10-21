#include "environment_probe.h"

#include "context.h"
#include "../core/jdk.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/sdk_bootstrap.h"
#include "../core/sdk_packages.h"

namespace CoreDeck {
    bool RefreshJavaHomeStatusAsync(Context &context, JavaHomeStatus &state, const std::string &path) {
        auto &work = context.JavaProbe;
        if (work.Future.valid() && work.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = work.Future.get();
                if (work.RequestedPath == path) {
                    state = std::move(result);
                }
            } catch (const std::exception &e) {
                Log::Error("Java probe failed: ", e.what());
                if (work.RequestedPath == path) {
                    state = {.Path = path, .Text = "Unable to read Java version."};
                }
            }
        }
        // The input may be a macOS bundle whose normalized Path differs from the text field.
        if (!work.Future.valid() && work.RequestedPath == path && !state.Text.empty()) {
            return true;
        }
        if (!work.Future.valid()) {
            work.RequestedPath = path;
            state = {};
            work.Future = std::async(std::launch::async, [path] { return ReadJavaHomeStatus(path); });
        }
        return false;
    }

    bool RefreshSdkPathStatusAsync(Context &context, const std::string &path) {
        auto &probe = context.SdkPathProbe;
        if (probe.Future.valid() && probe.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = probe.Future.get();
                if (probe.RequestedPath == path) {
                    probe.Value = std::move(result);
                    probe.ReadyPath = path;
                    probe.Ready = true;
                }
            } catch (const std::exception &e) {
                Log::Error("SDK path probe failed: ", e.what());
                if (probe.RequestedPath == path) {
                    probe.Value = {};
                    probe.ReadyPath = path;
                    probe.Ready = true;
                    probe.Error = "SDK discovery failed. Check directory permissions and try again.";
                }
            }
        }
        if (!probe.Future.valid() && (!probe.Ready || probe.ReadyPath != path)) {
            probe.RequestedPath = path;
            probe.Error.clear();
            probe.Future = std::async(std::launch::async, [path] {
                Context::SdkPathProbeWork::Result result;
                if (!path.empty()) {
                    result.Valid = Paths::Onboarding::ValidateSdkPath(path);
                    result.HasManager = HasSdkManager(path);
                    result.CanInstall = CanInstallAndroidSdkIntoDirectory(path);
                    result.Sdk = BuildSdkInfoFromSdkRoot(path, "");
                }
                return result;
            });
        }
        return probe.Ready && probe.ReadyPath == path && !probe.Future.valid();
    }

}
