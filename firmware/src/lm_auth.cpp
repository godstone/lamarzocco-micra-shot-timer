#include "lm_auth.h"

#include <esp_random.h>
#include <string.h>
#include <time.h>

#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

static mbedtls_pk_context g_pk;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr;
static uint8_t g_secret[32];
static String g_installId;
static bool g_ready = false;

// Base64-encode into a String.
static String b64(const uint8_t *data, size_t len) {
    size_t needed = 0;
    mbedtls_base64_encode(nullptr, 0, &needed, data, len);
    uint8_t buf[200];
    if (needed > sizeof(buf)) return String();
    size_t olen = 0;
    if (mbedtls_base64_encode(buf, sizeof(buf), &olen, data, len) != 0) return String();
    buf[olen] = 0;
    return String((char *)buf);
}

// Lowercase UUID v4 from hardware RNG.
static String uuid4() {
    uint8_t b[16];
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = esp_random();
        memcpy(b + i, &r, 4);
    }
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    char s[37];
    snprintf(s, sizeof(s),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
             b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14],
             b[15]);
    return String(s);
}

// LM "Y5.e" proof: mix the base string into a copy of the 32-byte secret, then SHA256 + base64.
static String makeProof(const String &base) {
    uint8_t work[32];
    memcpy(work, g_secret, 32);
    const uint8_t *s = (const uint8_t *)base.c_str();
    size_t n = base.length();
    for (size_t i = 0; i < n; i++) {
        int bv = s[i];
        int idx = bv % 32;
        int sh = work[(idx + 1) % 32] & 7;
        int x = bv ^ work[idx];
        int rot = ((x << sh) | (x >> (8 - sh))) & 0xFF;  // sh==0 -> x>>8 == 0, so rot==x
        work[idx] = (uint8_t)rot;
    }
    uint8_t h[32];
    mbedtls_sha256(work, 32, h, 0);
    return b64(h, 32);
}

bool lmAuthBegin(const char *installId, const char *secretB64, const char *privKeyDerB64) {
    mbedtls_pk_init(&g_pk);
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr);
    const char *seed = "lamarzocco-display";
    if (mbedtls_ctr_drbg_seed(&g_ctr, mbedtls_entropy_func, &g_entropy, (const uint8_t *)seed,
                              strlen(seed)) != 0)
        return false;

    size_t olen = 0;
    if (mbedtls_base64_decode(g_secret, sizeof(g_secret), &olen, (const uint8_t *)secretB64,
                              strlen(secretB64)) != 0 ||
        olen != 32)
        return false;

    uint8_t der[300];
    size_t derlen = 0;
    if (mbedtls_base64_decode(der, sizeof(der), &derlen, (const uint8_t *)privKeyDerB64,
                              strlen(privKeyDerB64)) != 0)
        return false;

    if (mbedtls_pk_parse_key(&g_pk, der, derlen, nullptr, 0, mbedtls_ctr_drbg_random, &g_ctr) != 0)
        return false;

    g_installId = installId;
    g_ready = true;
    return true;
}

bool lmAuthHeaders(LmSignedHeaders &out) {
    if (!g_ready) return false;

    String nonce = uuid4();
    char ts[24];
    snprintf(ts, sizeof(ts), "%llu", (unsigned long long)time(nullptr) * 1000ULL);

    String proofInput = g_installId + "." + nonce + "." + ts;
    String proof = makeProof(proofInput);
    if (proof.isEmpty()) return false;
    String sigData = proofInput + "." + proof;

    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)sigData.c_str(), sigData.length(), hash, 0);

    uint8_t sig[128];
    size_t siglen = 0;
    if (mbedtls_pk_sign(&g_pk, MBEDTLS_MD_SHA256, hash, 32, sig, sizeof(sig), &siglen,
                        mbedtls_ctr_drbg_random, &g_ctr) != 0)
        return false;

    out.installId = g_installId;
    out.timestamp = ts;
    out.nonce = nonce;
    out.signature = b64(sig, siglen);
    return !out.signature.isEmpty();
}
