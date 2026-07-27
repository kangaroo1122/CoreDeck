#include "shared_folder_sync.h"

#include <chrono>
#include <future>

#include "../core/adb.h"

namespace CoreDeck {
    namespace {
        constexpr auto SYNC_INTERVAL = std::chrono::seconds(8);

        const char *SyncModeStatus(const SharedFolderSyncMode mode) {
            switch (mode) {
                case SharedFolderSyncMode::DeviceToHost:
                    return "Syncing shared folder from emulator...";
                case SharedFolderSyncMode::Bidirectional:
                default:
                    return "Syncing shared folder...";
            }
        }

        Context::SharedFolderSyncJob &EnsureJob(
            Context &context,
            const std::string &avdName,
            const std::string &serial
        ) {
            auto [it, inserted] = context.SharedFolderSync.PerAvd.try_emplace(avdName);
            auto &job = it->second;
            if (inserted || job.AvdName.empty()) {
                job.AvdName = avdName;
            }
            if (!serial.empty()) {
                job.Serial = serial;
            }
            return job;
        }

        void StartJob(
            Context &context,
            Context::SharedFolderSyncJob &job,
            const SharedFolderSyncMode mode
        ) {
            if (job.Busy || job.AvdName.empty() || job.Serial.empty()) {
                return;
            }

            job.Mode = mode;
            job.Busy = true;
            job.LastAttempt = std::chrono::steady_clock::now();
            context.SharedFolderSync.Status = SyncModeStatus(mode);
            context.SharedFolderSync.Error.clear();

            const SdkInfo sdk = context.Host.Sdk;
            const std::string avdName = job.AvdName;
            const std::string serial = job.Serial;
            job.Future = std::async(std::launch::async, [sdk, avdName, serial, mode] {
                return SyncSharedFolder(sdk, serial, avdName, mode);
            });
        }

        bool ResolveRunningSerial(const Context &context, const std::string &avdName, std::string *serial) {
            if (!context.Host.Manager.IsRunning(avdName)) {
                return false;
            }
            const int consolePort = context.Host.Manager.GetConsolePort(avdName);
            const std::string resolved = EmulatorSerialForConsolePort(consolePort);
            if (resolved.empty()) {
                return false;
            }
            if (serial != nullptr) {
                *serial = resolved;
            }
            return true;
        }

        bool IsSyncDue(const Context::SharedFolderSyncJob &job, const std::chrono::steady_clock::time_point now) {
            return job.LastAttempt == std::chrono::steady_clock::time_point{} ||
                   now - job.LastAttempt >= SYNC_INTERVAL;
        }

        void StopAvdAfterFinalSync(Context &context, Context::SharedFolderSyncJob &job) {
            context.Host.Manager.Stop(job.AvdName);
            job.PendingStop = false;
        }

        void PollJobs(Context &context) {
            for (auto &[avdName, job]: context.SharedFolderSync.PerAvd) {
                if (!job.Busy || !job.Future.valid()) {
                    continue;
                }
                if (job.Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    continue;
                }

                const SharedFolderSyncResult result = job.Future.get();
                job.Busy = false;
                if (result.Success) {
                    job.InitialSynced = true;
                    context.SharedFolderSync.Status = result.Output.find("Shared folder conflicts were saved as:") != std::string::npos
                                                        ? "Shared folder synced with conflicts."
                                                        : "Shared folder synced.";
                    context.SharedFolderSync.Error.clear();
                } else {
                    context.SharedFolderSync.Status.clear();
                    context.SharedFolderSync.Error = result.Output.empty() ? "Could not sync shared folder." : result.Output;
                }

                if (job.PendingStop) {
                    if (job.Mode == SharedFolderSyncMode::DeviceToHost) {
                        StopAvdAfterFinalSync(context, job);
                    } else {
                        StartJob(context, job, SharedFolderSyncMode::DeviceToHost);
                    }
                }
            }
        }

        void RemoveFinishedInactiveJobs(Context &context) {
            for (auto it = context.SharedFolderSync.PerAvd.begin(); it != context.SharedFolderSync.PerAvd.end();) {
                const auto &[avdName, job] = *it;
                if (!job.Busy && !job.PendingStop && !context.Host.Manager.IsRunning(avdName)) {
                    it = context.SharedFolderSync.PerAvd.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    bool IsSharedFolderStopPending(const Context &context, const std::string &avdName) {
        const auto it = context.SharedFolderSync.PerAvd.find(avdName);
        return it != context.SharedFolderSync.PerAvd.end() && it->second.PendingStop;
    }

    void RequestSharedFolderSync(Context &context, const std::string &avdName, const std::string &serial) {
        auto &job = EnsureJob(context, avdName, serial);
        StartJob(context, job, SharedFolderSyncMode::Bidirectional);
    }

    void RequestAvdStopWithSharedFolderSync(Context &context, const std::string &avdName) {
        std::string serial;
        if (!ResolveRunningSerial(context, avdName, &serial)) {
            context.Host.Manager.Stop(avdName);
            return;
        }

        auto &job = EnsureJob(context, avdName, serial);
        job.PendingStop = true;
        if (!job.Busy) {
            StartJob(context, job, SharedFolderSyncMode::DeviceToHost);
        }
    }

    void DriveSharedFolderSync(Context &context) {
        PollJobs(context);

        const auto now = std::chrono::steady_clock::now();
        for (const auto &avd: context.Catalog.Avds) {
            std::string serial;
            if (!ResolveRunningSerial(context, avd.Name, &serial)) {
                continue;
            }

            auto &job = EnsureJob(context, avd.Name, serial);
            if (job.Busy || job.PendingStop || context.Host.Manager.IsStopping(avd.Name)) {
                continue;
            }
            if (IsSyncDue(job, now)) {
                StartJob(context, job, SharedFolderSyncMode::Bidirectional);
            }
        }

        RemoveFinishedInactiveJobs(context);
    }

    void PullRunningSharedFoldersBeforeShutdown(Context &context) {
        for (auto &[avdName, job]: context.SharedFolderSync.PerAvd) {
            if (job.Future.valid()) {
                job.Future.wait();
                if (job.Busy) {
                    (void)job.Future.get();
                    job.Busy = false;
                }
            }
        }

        for (const auto &avd: context.Catalog.Avds) {
            std::string serial;
            if (!ResolveRunningSerial(context, avd.Name, &serial)) {
                continue;
            }
            (void)SyncSharedFolder(context.Host.Sdk, serial, avd.Name, SharedFolderSyncMode::DeviceToHost);
        }
    }
}
