#include "sdk_install_transaction.h"

#include <chrono>

namespace CoreDeck {
    namespace {
        bool IsMissingOrEmpty(const std::filesystem::path &path, std::string &error) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                if (ec) {
                    error = ec.message();
                    return false;
                }
                return true;
            }
            if (!std::filesystem::is_directory(path, ec)) {
                error = ec ? ec.message() : "SDK target is not a directory.";
                return false;
            }
            if (!std::filesystem::is_empty(path, ec)) {
                error = ec ? ec.message() : "SDK target is not empty.";
                return false;
            }
            return true;
        }
    }

    SdkInstallTransaction::SdkInstallTransaction(
        std::filesystem::path target,
        std::filesystem::path staging
    ) : m_Target(std::move(target)), m_Staging(std::move(staging)) {}

    std::unique_ptr<SdkInstallTransaction> SdkInstallTransaction::Begin(
        const std::filesystem::path &target,
        std::string &error
    ) {
        error.clear();
        if (target.empty() || !IsMissingOrEmpty(target, error)) {
            if (error.empty()) {
                error = "SDK target is invalid.";
            }
            return nullptr;
        }

        std::error_code ec;
        const auto parent = target.parent_path().empty() ? std::filesystem::current_path(ec) : target.parent_path();
        if (ec) {
            error = ec.message();
            return nullptr;
        }
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = ec.message();
            return nullptr;
        }

        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto staging = parent / ("." + target.filename().string() + "-coredeck-installing-" + suffix);
        std::filesystem::create_directories(staging, ec);
        if (ec) {
            error = ec.message();
            return nullptr;
        }
        return std::unique_ptr<SdkInstallTransaction>(new SdkInstallTransaction(target, staging));
    }

    SdkInstallTransaction::~SdkInstallTransaction() {
        if (!m_Committed) {
            std::error_code ec;
            std::filesystem::remove_all(m_Staging, ec);
        }
    }

    bool SdkInstallTransaction::Commit(std::string &error) {
        if (m_Committed) {
            return true;
        }
        error.clear();
        std::error_code ec;
        if (!std::filesystem::exists(m_Staging, ec) || ec) {
            error = ec ? ec.message() : "SDK staging directory is missing.";
            return false;
        }
        if (std::filesystem::exists(m_Target, ec)) {
            if (ec || !std::filesystem::is_empty(m_Target, ec) || ec) {
                error = ec ? ec.message() : "SDK target changed while installation was running.";
                return false;
            }
            std::filesystem::remove(m_Target, ec);
            if (ec) {
                error = ec.message();
                return false;
            }
        }
        std::filesystem::rename(m_Staging, m_Target, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        m_Committed = true;
        return true;
    }
}
