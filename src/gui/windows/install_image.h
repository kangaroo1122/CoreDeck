//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#ifndef COREDECK_INSTALL_IMAGE_H
#define COREDECK_INSTALL_IMAGE_H

#include "../context.h"
#include "gui/widgets.h"

namespace CoreDeck {
    // Call after cancellation has been requested for all modules.
    void ShutdownImageInstallationWork(Context &context);

    LabeledIconStyle SystemImageTypeStyleForVariant(const std::string &variant);

    LabeledIconStyle SystemImageTypeStyleFor(const SystemImage &img);

    LabeledIconStyle SystemImageTypeStyleFor(const RemoteSystemImage &img);

    std::string SystemImageDisplayName(const std::string &apiLevel, const std::string &fallback);

    std::string SystemImagePreviewLabel(const SystemImage &img);

    void BuildInstallImageWindow(Context &context);
    void RefreshSystemImageLists(Context &context, const std::string &selectedPackagePath = "");
}

#endif // COREDECK_INSTALL_IMAGE_H
