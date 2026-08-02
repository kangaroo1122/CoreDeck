#include "sdk_progress.h"

#include <algorithm>

namespace CoreDeck {
    void SetSdkProgressRange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const float start,
        const float end
    ) {
        if (!progress) {
            return;
        }
        const float clampedStart = std::clamp(start, 0.0F, 1.0F);
        const float clampedEnd = std::clamp(end, clampedStart, 1.0F);
        std::lock_guard lock(progress->Mutex);
        progress->RangeStart = clampedStart;
        progress->RangeEnd = clampedEnd;
        progress->Percent = std::max(progress->Percent, clampedStart);
    }

    SdkProgressRange GetSdkProgressRange(const std::shared_ptr<SdkOperationProgress> &progress) {
        if (!progress) {
            return {};
        }
        std::lock_guard lock(progress->Mutex);
        return {progress->RangeStart, progress->RangeEnd};
    }

    void SetSdkProgressSubrange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const SdkProgressRange &parent,
        const float localStart,
        const float localEnd
    ) {
        const float start = std::clamp(localStart, 0.0F, 1.0F);
        const float end = std::clamp(localEnd, start, 1.0F);
        const float width = parent.End - parent.Start;
        SetSdkProgressRange(progress, parent.Start + start * width, parent.Start + end * width);
    }

    void ReportSdkProgress(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const float localPercent,
        const std::string &status,
        const std::string &detail
    ) {
        ReportSdkProgressInSubrange(progress, {}, localPercent, status, detail);
    }

    void ReportSdkProgressInSubrange(
        const std::shared_ptr<SdkOperationProgress> &progress,
        const SdkProgressRange &localRange,
        const float localPercent,
        const std::string &status,
        const std::string &detail
    ) {
        if (!progress || progress->CancelRequested.load()) {
            return;
        }
        std::lock_guard lock(progress->Mutex);
        const float rangeStart = std::clamp(localRange.Start, 0.0F, 1.0F);
        const float rangeEnd = std::clamp(localRange.End, rangeStart, 1.0F);
        const float local = std::clamp(localPercent, 0.0F, 1.0F);
        const float operationLocal = rangeStart + local * (rangeEnd - rangeStart);
        const float mapped = progress->RangeStart + operationLocal * (progress->RangeEnd - progress->RangeStart);
        progress->Percent = std::max(progress->Percent, mapped);
        progress->StatusText = status;
        progress->DetailText = detail;
        progress->Finished = false;
        progress->Succeeded = false;
    }
}
