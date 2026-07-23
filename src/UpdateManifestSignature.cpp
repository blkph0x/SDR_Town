#include "UpdateManifestSignature.h"

#include <QMessageAuthenticationCode>
#include <QUrl>

namespace {

QByteArray embeddedUpdateManifestHmacKey()
{
    // Release pipeline rotates this via scripts/sign_update_manifest.ps1 + resources/update_manifest_hmac.key
    static const char kHex[] =
        "a4f2c91e8b7d603548e1f9a2b6c3d0e7f5a8192c4e6b0d3f7a9281c5e0b4d6f8";
    return QByteArray::fromHex(kHex);
}

} // namespace

bool verifyUpdateManifestSignature(const QByteArray& manifestBytes, const QByteArray& signatureBytes)
{
    if (manifestBytes.isEmpty() || signatureBytes.isEmpty()) return false;
    const QByteArray key = embeddedUpdateManifestHmacKey();
    if (key.isEmpty()) return false;
    const QByteArray mac = QMessageAuthenticationCode::hash(
        manifestBytes, key, QCryptographicHash::Sha256);
    const QByteArray provided = QByteArray::fromBase64(signatureBytes.trimmed());
    return !provided.isEmpty() && mac == provided;
}

QString updateManifestSignatureUrlFor(const QString& manifestUrl)
{
    if (manifestUrl.endsWith(QStringLiteral("update.json"), Qt::CaseInsensitive)) {
        return manifestUrl.left(manifestUrl.size() - QStringLiteral("update.json").size()) +
            QStringLiteral("update.json.sig");
    }
    return {};
}
