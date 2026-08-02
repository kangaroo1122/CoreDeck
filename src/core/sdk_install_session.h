#ifndef COREDECK_SDK_INSTALL_SESSION_H
#define COREDECK_SDK_INSTALL_SESSION_H

#include <filesystem>
#include <memory>
#include <string>

#include "sdk_install_transaction.h"

namespace CoreDeck {
    class SdkInstallSession {
    public:
        bool BeginFresh(const std::filesystem::path &target, std::string &error);
        void UseExisting(const std::filesystem::path &root);
        void Reset();

        const std::filesystem::path &TargetRoot() const { return m_TargetRoot; }
        const std::filesystem::path &ActiveRoot() const;
        bool IsTransactional() const { return m_Transaction != nullptr; }
        bool Commit(std::string &error);

    private:
        std::filesystem::path m_TargetRoot;
        std::unique_ptr<SdkInstallTransaction> m_Transaction;
    };
}

#endif // COREDECK_SDK_INSTALL_SESSION_H
