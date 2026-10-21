#ifndef COREDECK_HEALTH_CHECK_H
#define COREDECK_HEALTH_CHECK_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "sdk.h"

namespace CoreDeck {
    bool IsAvdManagerHelpOutput(const std::string &output);
    enum class HealthStatus { Passed, Warning, Failed, Skipped };
    enum class HealthFix { None, Sdk, Jdk, Images };
    struct HealthCheckResult {
        std::string Label;
        HealthStatus Status;
        std::string Detail;
        HealthFix Fix = HealthFix::None;
    };

    std::vector<HealthCheckResult> RunEnvironmentHealthCheck(
        const SdkInfo &sdk, const std::shared_ptr<std::atomic<bool>> &cancel
    );
}
#endif
