#include "sdk_progress.h"

#include <algorithm>

namespace CoreDeck {
    SdkManagerProgressLine ParseSdkManagerProgressLine(const std::string &line) {
        const auto trim = [](const std::string &text) {
            const auto start = text.find_first_not_of(" \t\r\n");
            return start == std::string::npos ? std::string{} :
                text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
        };
        // Both legacy progress bars and cmdline-tools 23's plain percentage output.
        for (size_t pos = 0; pos < line.size(); ++pos) {
            if (line[pos] != '%') continue;
            if (pos + 1 < line.size() && line[pos + 1] != ' ' &&
                line[pos + 1] != '\t' && line[pos + 1] != '(' && line[pos + 1] != '\r') continue;
            size_t start = pos;
            while (start > 0 && line[start - 1] >= '0' && line[start - 1] <= '9') --start;
            if (start == pos || pos - start > 3) continue;
            if (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t' && line[start - 1] != ']') continue;
            int percent = 0;
            for (size_t digit = start; digit < pos; ++digit) percent = percent * 10 + line[digit] - '0';
            if (percent > 100) continue;

            std::string status = trim(line.substr(pos + 1));
            const bool unzipping = line.find("Unzipping") != std::string::npos;
            const auto open = line.find('(', pos);
            const auto close = open == std::string::npos ? std::string::npos : line.find(')', open);
            if (close != std::string::npos) {
                status = (unzipping ? "Unzipping " : "Downloading ") + line.substr(open + 1, close - open - 1);
            } else if (status.empty() || status.starts_with("ETA:")) {
                status = unzipping ? "Unzipping..." : "Downloading...";
            }
            return {percent, status};
        }
        return {};
    }

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
