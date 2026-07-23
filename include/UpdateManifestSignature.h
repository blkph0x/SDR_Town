#pragma once

#include <QByteArray>
#include <QString>

// Verify ECDSA-P256-SHA256 signature over the raw update.json bytes.
// Public key is embedded from resources/update_manifest_public.pem.
bool verifyUpdateManifestSignature(const QByteArray& manifestBytes, const QByteArray& signatureDer);

QString updateManifestSignatureUrlFor(const QString& manifestUrl);
