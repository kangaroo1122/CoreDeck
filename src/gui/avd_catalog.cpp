#include "avd_catalog.h"

#include <algorithm>
#include "context.h"
#include "../core/avd.h"
#include "../core/log.h"

namespace CoreDeck {
    void RefreshAvds(Context &context) {
        auto &catalog = context.Catalog;
        catalog.RefreshPending = true;
        catalog.RefreshError.clear();
        // Supersede old work without destroying a running future on the UI thread.
        if (catalog.RefreshFuture.valid()) {
            catalog.RefreshCancel->store(true);
            return;
        }
        catalog.RefreshPending = false;
        catalog.RefreshCancel = std::make_shared<std::atomic<bool>>(false);
        const auto cancel = catalog.RefreshCancel;
        const SdkInfo sdk = context.Host.Sdk;
        std::vector<std::string> loadedOptions;
        loadedOptions.reserve(catalog.PerAvdOptions.size());
        for (const auto &[name, options]: catalog.PerAvdOptions) {
            loadedOptions.push_back(name);
        }
        catalog.RefreshFuture = std::async(std::launch::async, [sdk, cancel, loadedOptions = std::move(loadedOptions)] {
            AvdCatalogResult result;
            result.Names = ListAvdNames(sdk, [cancel] { return cancel->load(); });
            if (cancel->load()) {
                return result;
            }
            result.Avds = LoadAvds(result.Names);
            for (const auto &name: result.Names) {
                if (cancel->load()) {
                    break;
                }
                // Refresh the device catalog, not the user's live option edits.
                if (std::ranges::find(loadedOptions, name) != loadedOptions.end()) {
                    continue;
                }
                result.Options.emplace(name, LoadOptionsFromFile(GetOptionsConfigPath(name)));
            }
            return result;
        });
    }


    void PollAvdRefresh(Context &context) {
        auto &catalog = context.Catalog;
        if (!catalog.RefreshFuture.valid() ||
            catalog.RefreshFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            auto result = catalog.RefreshFuture.get();
            if (!catalog.RefreshCancel->load() && !catalog.RefreshPending) {
                // Read selection at completion, so navigation during a refresh is preserved.
                std::string selectedName;
                if (catalog.SelectedAvd >= 0 && catalog.SelectedAvd < static_cast<int>(catalog.Avds.size())) {
                    selectedName = catalog.Avds[catalog.SelectedAvd].Name;
                }
                catalog.AvdNames = std::move(result.Names);
                catalog.Avds = std::move(result.Avds);
                // Existing options may have been edited while the worker was reading files.
                for (auto &[name, options]: result.Options) {
                    catalog.PerAvdOptions.try_emplace(name, std::move(options));
                }
                std::erase_if(catalog.PerAvdOptions, [&](const auto &entry) {
                    return std::ranges::find(catalog.AvdNames, entry.first) == catalog.AvdNames.end();
                });
                catalog.SelectedAvd = catalog.Avds.empty() ? -1 : 0;
                for (int i = 0; i < static_cast<int>(catalog.Avds.size()); ++i) {
                    if (catalog.Avds[i].Name == selectedName) {
                        catalog.SelectedAvd = i;
                        break;
                    }
                }
                catalog.PreviousSelectedAvd = -1;
                catalog.FilteredIndices.clear();
                context.DiskUsage.PerAvdCache.clear();
                if (!context.DiskUsage.Loading.load()) {
                    context.DiskUsage.LastScan = {};
                    context.DiskUsage.Ready = false;
                }
            }
        } catch (const std::exception &e) {
            Log::Error("AVD refresh failed: ", e.what());
            if (!catalog.RefreshCancel->load() && !catalog.RefreshPending) {
                catalog.RefreshError = "Could not refresh AVDs. The emulator command failed or timed out.";
            }
        }
        if (catalog.RefreshPending) {
            RefreshAvds(context);
        }
    }

    void LoadAvdOptions(Context &context, const std::string &avdName) {
        const std::string configPath = GetOptionsConfigPath(avdName);
        context.Catalog.PerAvdOptions[avdName] = LoadOptionsFromFile(configPath);
    }

    void SaveAvdOptions(Context &context, const std::string &avdName) {
        if (!context.Catalog.PerAvdOptions.contains(avdName)) {
            return;
        }

        const std::string configPath = GetOptionsConfigPath(avdName);
        SaveOptionsToFile(configPath, context.Catalog.PerAvdOptions[avdName]);
    }

    std::vector<EmulatorOption> &GetDefaultAvdOptions(Context &context) {
        if (context.Catalog.SelectedAvd >= 0 && context.Catalog.SelectedAvd < context.Catalog.Avds.size()) {
            const std::string &avdName = context.Catalog.Avds[context.Catalog.SelectedAvd].Name;

            if (!context.Catalog.PerAvdOptions.contains(avdName)) {
                LoadAvdOptions(context, avdName);
            }
            return context.Catalog.PerAvdOptions[avdName];
        }

        // Fallback
        static std::vector<EmulatorOption> defaultOptions = GetEmulatorOptions();
        return defaultOptions;
    }
}
