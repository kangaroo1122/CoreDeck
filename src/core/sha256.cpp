//
// Created by CoreDeck contributors on 02/08/2026.
//

#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>

namespace CoreDeck {
    namespace {
        constexpr std::array<std::uint32_t, 64> ROUND_CONSTANTS{
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        std::uint32_t RotateRight(const std::uint32_t value, const int count) {
            return (value >> count) | (value << (32 - count));
        }

        class Sha256 {
        public:
            void Update(const std::uint8_t *data, const std::size_t size) {
                m_TotalBytes += size;
                std::size_t offset = 0;
                while (offset < size) {
                    const std::size_t copied = std::min(size - offset, m_Buffer.size() - m_BufferSize);
                    std::copy_n(data + offset, copied, m_Buffer.data() + m_BufferSize);
                    m_BufferSize += copied;
                    offset += copied;
                    if (m_BufferSize == m_Buffer.size()) {
                        ProcessBlock(m_Buffer.data());
                        m_BufferSize = 0;
                    }
                }
            }

            std::string Finalize() {
                const std::uint64_t bitLength = m_TotalBytes * 8;
                m_Buffer[m_BufferSize++] = 0x80U;
                if (m_BufferSize > 56) {
                    std::fill(m_Buffer.begin() + static_cast<std::ptrdiff_t>(m_BufferSize), m_Buffer.end(), 0U);
                    ProcessBlock(m_Buffer.data());
                    m_BufferSize = 0;
                }
                std::fill(m_Buffer.begin() + static_cast<std::ptrdiff_t>(m_BufferSize), m_Buffer.begin() + 56, 0U);
                for (int i = 0; i < 8; ++i) {
                    m_Buffer[56 + i] = static_cast<std::uint8_t>(bitLength >> (56 - (i * 8)));
                }
                ProcessBlock(m_Buffer.data());

                constexpr char HEX[] = "0123456789abcdef";
                std::string result;
                result.reserve(64);
                for (const std::uint32_t value: m_State) {
                    for (int shift = 28; shift >= 0; shift -= 4) {
                        result.push_back(HEX[(value >> shift) & 0x0fU]);
                    }
                }
                return result;
            }

        private:
            void ProcessBlock(const std::uint8_t *block) {
                std::array<std::uint32_t, 64> words{};
                for (int i = 0; i < 16; ++i) {
                    words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                               static_cast<std::uint32_t>(block[i * 4 + 3]);
                }
                for (int i = 16; i < 64; ++i) {
                    const std::uint32_t s0 = RotateRight(words[i - 15], 7) ^ RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
                    const std::uint32_t s1 = RotateRight(words[i - 2], 17) ^ RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
                    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
                }

                std::uint32_t a = m_State[0];
                std::uint32_t b = m_State[1];
                std::uint32_t c = m_State[2];
                std::uint32_t d = m_State[3];
                std::uint32_t e = m_State[4];
                std::uint32_t f = m_State[5];
                std::uint32_t g = m_State[6];
                std::uint32_t h = m_State[7];

                for (int i = 0; i < 64; ++i) {
                    const std::uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
                    const std::uint32_t choose = (e & f) ^ (~e & g);
                    const std::uint32_t temp1 = h + sum1 + choose + ROUND_CONSTANTS[i] + words[i];
                    const std::uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
                    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t temp2 = sum0 + majority;

                    h = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }

                m_State[0] += a;
                m_State[1] += b;
                m_State[2] += c;
                m_State[3] += d;
                m_State[4] += e;
                m_State[5] += f;
                m_State[6] += g;
                m_State[7] += h;
            }

            std::array<std::uint32_t, 8> m_State{
                0x6a09e667U,
                0xbb67ae85U,
                0x3c6ef372U,
                0xa54ff53aU,
                0x510e527fU,
                0x9b05688cU,
                0x1f83d9abU,
                0x5be0cd19U,
            };
            std::array<std::uint8_t, 64> m_Buffer{};
            std::size_t m_BufferSize = 0;
            std::uint64_t m_TotalBytes = 0;
        };
    }

    std::string Sha256Hex(const std::string_view input) {
        Sha256 hash;
        hash.Update(reinterpret_cast<const std::uint8_t *>(input.data()), input.size());
        return hash.Finalize();
    }

    std::string Sha256File(const std::string &path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {};
        }

        Sha256 hash;
        std::array<char, 64 * 1024> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read > 0) {
                hash.Update(
                    reinterpret_cast<const std::uint8_t *>(buffer.data()),
                    static_cast<std::size_t>(read)
                );
            }
        }
        if (!input.eof()) {
            return {};
        }
        return hash.Finalize();
    }

    bool EqualsIgnoreCaseHex(const std::string_view left, const std::string_view right) {
        if (left.size() != right.size()) {
            return false;
        }
        return std::ranges::equal(left, right, [](const char a, const char b) {
            const auto lower = [](const char value) {
                return value >= 'A' && value <= 'F' ? static_cast<char>(value - 'A' + 'a') : value;
            };
            return lower(a) == lower(b);
        });
    }
}
