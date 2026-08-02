#include "sdk_install_session.h"

namespace CoreDeck {
    bool SdkInstallSession::BeginFresh(const std::filesystem::path &target, std::string &error) {
        Reset();
        auto transaction = SdkInstallTransaction::Begin(target, error);
        if (!transaction) {
            return false;
        }
        m_TargetRoot = target;
        m_Transaction = std::move(transaction);
        return true;
    }

    void SdkInstallSession::UseExisting(const std::filesystem::path &root) {
        Reset();
        m_TargetRoot = root;
    }

    void SdkInstallSession::Reset() {
        m_Transaction.reset();
        m_TargetRoot.clear();
    }

    const std::filesystem::path &SdkInstallSession::ActiveRoot() const {
        return m_Transaction ? m_Transaction->StagingRoot() : m_TargetRoot;
    }

    bool SdkInstallSession::Commit(std::string &error) {
        if (!m_Transaction) {
            return true;
        }
        if (!m_Transaction->Commit(error)) {
            return false;
        }
        m_Transaction.reset();
        return true;
    }
}
