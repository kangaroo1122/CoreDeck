#include "sdk_repository.h"

#include <array>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>

#include <tinyxml2.h>

namespace CoreDeck {
    namespace {
        constexpr std::string_view DownloadRoot = "https://dl.google.com/android/repository/";

        std::string Trim(std::string value) {
            const auto isSpace = [](const unsigned char character) { return std::isspace(character) != 0; };
            value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
            return value;
        }

        bool IsSha1(const std::string &value) {
            return value.size() == 40 && std::ranges::all_of(value, [](const unsigned char character) {
                return std::isxdigit(character) != 0;
            });
        }

        std::string_view LocalName(const char *qualifiedName) {
            if (qualifiedName == nullptr) {
                return {};
            }
            const std::string_view name(qualifiedName);
            const std::size_t separator = name.find(':');
            return separator == std::string_view::npos ? name : name.substr(separator + 1);
        }

        const tinyxml2::XMLElement *Child(const tinyxml2::XMLElement *parent, const std::string_view name) {
            if (parent == nullptr) {
                return nullptr;
            }
            for (auto *child = parent->FirstChildElement(); child != nullptr; child = child->NextSiblingElement()) {
                if (LocalName(child->Name()) == name) {
                    return child;
                }
            }
            return nullptr;
        }

        std::string Text(const tinyxml2::XMLElement *parent, const std::string_view name) {
            const tinyxml2::XMLElement *element = Child(parent, name);
            return element != nullptr && element->GetText() != nullptr ? Trim(element->GetText()) : "";
        }

        bool ParseNonNegative(const std::string &text, std::uintmax_t &value) {
            if (text.empty()) {
                return false;
            }
            const char *begin = text.data();
            const char *end = begin + text.size();
            const auto [position, error] = std::from_chars(begin, end, value);
            return error == std::errc{} && position == end;
        }

        bool ParseRevision(const tinyxml2::XMLElement *package, std::array<std::uintmax_t, 3> &revision) {
            const tinyxml2::XMLElement *element = Child(package, "revision");
            if (element == nullptr || !ParseNonNegative(Text(element, "major"), revision[0])) {
                return false;
            }
            for (std::size_t index = 1; index < revision.size(); ++index) {
                const std::string value = Text(element, index == 1 ? "minor" : "micro");
                if (!value.empty() && !ParseNonNegative(value, revision[index])) {
                    return false;
                }
            }
            return true;
        }

        std::optional<CommandLineToolsPackage> ParseArchive(
            const tinyxml2::XMLElement *archive,
            const std::string &hostOs,
            const std::string &hostArch
        ) {
            if (Text(archive, "host-os") != hostOs ||
                (!hostArch.empty() && Text(archive, "host-arch") != hostArch)) {
                return std::nullopt;
            }

            const tinyxml2::XMLElement *complete = Child(archive, "complete");
            const tinyxml2::XMLElement *checksum = Child(complete, "checksum");
            if (complete == nullptr || checksum == nullptr || checksum->Attribute("type") == nullptr ||
                std::string_view(checksum->Attribute("type")) != "sha1" || checksum->GetText() == nullptr) {
                return std::nullopt;
            }

            CommandLineToolsPackage package;
            const std::string url = Text(complete, "url");
            if (url.empty() || !ParseNonNegative(Text(complete, "size"), package.SizeBytes) || package.SizeBytes == 0) {
                return std::nullopt;
            }
            package.DownloadUrl = url.starts_with("https://") ? url : std::string(DownloadRoot) + url;
            package.Sha1 = Trim(checksum->GetText());
            return IsSha1(package.Sha1) ? std::optional(std::move(package)) : std::nullopt;
        }

        void VisitPackages(
            const tinyxml2::XMLElement *element,
            const std::string &hostOs,
            const std::string &hostArch,
            std::array<std::uintmax_t, 3> &selectedRevision,
            bool &hasSelection,
            std::optional<CommandLineToolsPackage> &selected
        ) {
            for (auto *current = element; current != nullptr; current = current->NextSiblingElement()) {
                if (LocalName(current->Name()) == "remotePackage") {
                    const char *path = current->Attribute("path");
                    const char *obsolete = current->Attribute("obsolete");
                    if (path != nullptr && std::string_view(path) == "cmdline-tools;latest" &&
                        !(obsolete != nullptr && std::string_view(obsolete) == "true")) {
                        std::array<std::uintmax_t, 3> revision{};
                        if (ParseRevision(current, revision) && (!hasSelection || revision > selectedRevision)) {
                            const tinyxml2::XMLElement *archives = Child(current, "archives");
                            for (auto *archive = archives == nullptr ? nullptr : archives->FirstChildElement();
                                 archive != nullptr;
                                 archive = archive->NextSiblingElement()) {
                                if (LocalName(archive->Name()) != "archive") {
                                    continue;
                                }
                                auto package = ParseArchive(archive, hostOs, hostArch);
                                if (package.has_value()) {
                                    selectedRevision = revision;
                                    hasSelection = true;
                                    selected = std::move(package);
                                    break;
                                }
                            }
                        }
                    }
                }
                VisitPackages(current->FirstChildElement(), hostOs, hostArch, selectedRevision, hasSelection, selected);
            }
        }
    }

    std::optional<CommandLineToolsPackage> ParseCommandLineToolsRepository(
        const std::string &xml,
        const std::string &hostOs,
        const std::string &hostArch
    ) {
        tinyxml2::XMLDocument document;
        if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS || document.RootElement() == nullptr) {
            return std::nullopt;
        }

        std::array<std::uintmax_t, 3> selectedRevision{};
        bool hasSelection = false;
        std::optional<CommandLineToolsPackage> selected;
        VisitPackages(document.RootElement(), hostOs, hostArch, selectedRevision, hasSelection, selected);
        return selected;
    }
}
