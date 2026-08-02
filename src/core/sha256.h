//
// Created by CoreDeck contributors on 02/08/2026.
//

#ifndef COREDECK_SHA256_H
#define COREDECK_SHA256_H

#include <string>
#include <string_view>

namespace CoreDeck {
    std::string Sha256Hex(std::string_view input);

    std::string Sha256File(const std::string &path);

    bool EqualsIgnoreCaseHex(std::string_view left, std::string_view right);
}

#endif // COREDECK_SHA256_H
