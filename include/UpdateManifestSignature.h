#pragma once

#include <QByteArray>
#include <QString>

// Verify Ed25519 detached signature over raw update.json bytes.
// Only the public key is embedded; the private key stays on the release machine.
bool verifyUpdateManifestSignature(const QByteArray& manifestBytes, const QByteArray& signatureBytes);

QString updateManifestSignatureUrlFor(const QString& manifestUrl);
