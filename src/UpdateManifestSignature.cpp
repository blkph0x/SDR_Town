#include "UpdateManifestSignature.h"

#include <QUrl>

#include <sodium.h>

#include <array>

namespace {

constexpr const char* kPlaceholderPublicKeyHex =
    "0000000000000000000000000000000000000000000000000000000000000000";

constexpr const char* kEmbeddedPublicKeyHex =
#include "../resources/update_manifest_ed25519_pub.inc"
    ;

std::array<unsigned char, 32> parseHexKey(const char* hex)
{
    std::array<unsigned char, 32> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        const char hi = hex[i * 2];
        const char lo = hex[i * 2 + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hiVal = nibble(hi);
        const int loVal = nibble(lo);
        if (hiVal < 0 || loVal < 0) return {};
        out[i] = static_cast<unsigned char>((hiVal << 4) | loVal);
    }
    return out;
}

bool publicKeyConfigured(const std::array<unsigned char, 32>& key)
{
    const auto placeholder = parseHexKey(kPlaceholderPublicKeyHex);
    return key != placeholder;
}

} // namespace

bool verifyUpdateManifestSignature(const QByteArray& manifestBytes, const QByteArray& signatureBytes)
{
    if (manifestBytes.isEmpty() || signatureBytes.isEmpty()) return false;
    if (sodium_init() < 0) return false;

    const auto publicKey = parseHexKey(kEmbeddedPublicKeyHex);
    if (!publicKeyConfigured(publicKey)) return false;

    const QByteArray signature = QByteArray::fromBase64(signatureBytes.trimmed());
    if (signature.size() != crypto_sign_BYTES) return false;

    return crypto_sign_ed25519_verify_detached(
               reinterpret_cast<const unsigned char*>(signature.constData()),
               reinterpret_cast<const unsigned char*>(manifestBytes.constData()),
               static_cast<unsigned long long>(manifestBytes.size()),
               publicKey.data()) == 0;
}

QString updateManifestSignatureUrlFor(const QString& manifestUrl)
{
    if (manifestUrl.endsWith(QStringLiteral("update.json"), Qt::CaseInsensitive)) {
        return manifestUrl.left(manifestUrl.size() - QStringLiteral("update.json").size()) +
            QStringLiteral("update.json.sig");
    }
    return {};
}
