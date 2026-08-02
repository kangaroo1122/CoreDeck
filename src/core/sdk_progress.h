#ifndef COREDECK_SDK_PROGRESS_H
#define COREDECK_SDK_PROGRESS_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace CoreDeck {
    struct SdkProgressRange {
        float Start = 0.0F;
        float End = 1.0F;
    };

    namespace OnboardingSdkProgress {
        inline constexpr SdkProgressRange Tools{0.0F, 0.35F};
        inline constexpr SdkProgressRange Licenses{0.35F, 0.42F};
        inline constexpr SdkProgressRange Packages{0.42F, 0.97F};
        inline constexpr SdkProgressRange Finalize{0.97F, 1.0F};
    }

    struct SdkOperationProgress {
        std::mutex Mutex;
        std::atomic<bool> CancelRequested{false};
        float Percent = 0.0F;
        float RangeStart = 0.0F;
        float RangeEnd = 1.0F;
        std::string StatusText;
        std::string DetailText;
        bool Finished = false;
        bool Succeeded = false;
    };

    void SetSdkProgressRange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        float start,
        float end
    );

    SdkProgressRange GetSdkProgressRange(const std::shared_ptr<SdkOperationProgress> &progress);

    void SetSdkProgressSubrange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const SdkProgressRange &parent,
        float localStart,
        float localEnd
    );

    void ReportSdkProgress(
        const std::shared_ptr<SdkOperationProgress> &progress,
        float localPercent,
        const std::string &status,
        const std::string &detail = ""
    );

    void ReportSdkProgressInSubrange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const SdkProgressRange &localRange,
        float localPercent,
        const std::string &status,
        const std::string &detail = ""
    );
}

#endif // COREDECK_SDK_PROGRESS_H
