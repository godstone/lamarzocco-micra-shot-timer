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

// Parse the embedded installation key (base64 secret + base64 DER secp256r1 private key) and
// init the RNG. Returns false if the key can't be parsed. Call once after time is set.
bool lmAuthBegin(const char *installId, const char *secretB64, const char *privKeyDerB64);

// Build the four signed headers for one request. Returns false on signing failure.
bool lmAuthHeaders(LmSignedHeaders &out);
