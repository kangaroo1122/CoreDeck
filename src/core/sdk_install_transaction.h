#ifndef COREDECK_SDK_INSTALL_TRANSACTION_H
#define COREDECK_SDK_INSTALL_TRANSACTION_H

#include <filesystem>
#include <memory>
#include <string>

namespace CoreDeck {
    class SdkInstallTransaction {
    public:
        static std::unique_ptr<SdkInstallTransaction> Begin(
            const std::filesystem::path &target,
            std::string &error
        );

        ~SdkInstallTransaction();

        SdkInstallTransaction(const SdkInstallTransaction &) = delete;
        SdkInstallTransaction &operator=(const SdkInstallTransaction &) = delete;

        const std::filesystem::path &TargetRoot() const { return m_Target; }
        const std::filesystem::path &StagingRoot() const { return m_Staging; }

        bool Commit(std::string &error);

    private:
        SdkInstallTransaction(std::filesystem::path target, std::filesystem::path staging);

        std::filesystem::path m_Target;
        std::filesystem::path m_Staging;
        bool m_Committed = false;
    };
}

#endif // COREDECK_SDK_INSTALL_TRANSACTION_H
