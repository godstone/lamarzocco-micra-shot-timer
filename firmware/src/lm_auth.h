// La Marzocco request signing (ported from pylamarzocco util/_authentication.py).
// Every cloud request needs these signed headers; the signature is ECDSA-SHA256 over
// "installationId.nonce.timestamp.proof", where proof is LM's custom byte-mix + SHA256.
#pragma once

#include <Arduino.h>

struct LmSignedHeaders {
    String installId;
    String timestamp;  // ms since epoch
    String nonce;      // lowercase uuid v4
    String signature;  // base64 DER ECDSA signature
};

// Parse an existing installation key (base64 secret + base64 DER secp256r1 private key) and
// init the RNG. Returns false if the key can't be parsed.
bool lmAuthBegin(const char *installId, const char *secretB64, const char *privKeyDerB64);

// Generate a brand-new installation key on-device (EC secp256r1 keypair + derived secret +
// random installation id) and make it active for signing. Returns false on failure.
// After this, use lmAuthInstallId/SecretB64/PrivKeyB64 to persist it, and register it with
// the cloud using lmAuthPubKeyB64() + lmAuthProof(lmAuthBaseString()).
bool lmAuthGenerate();

// Accessors for the active key (valid after lmAuthBegin or lmAuthGenerate).
String lmAuthInstallId();
String lmAuthSecretB64();
String lmAuthPrivKeyB64();
String lmAuthPubKeyB64();   // base64 DER SubjectPublicKeyInfo (for /auth/init "pk")
String lmAuthBaseString();  // installationId.b64(sha256(pubDer)) — for the registration proof
String lmAuthProof(const String &base);  // LM "Y5.e" proof of `base` with the active secret

// Build the four signed headers for one request. Returns false on signing failure.
bool lmAuthHeaders(LmSignedHeaders &out);
