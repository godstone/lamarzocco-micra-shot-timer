#include "lm_auth.h"

#include <esp_random.h>
#include <string.h>
#include <time.h>

#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

static mbedtls_pk_context g_pk;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr;
static uint8_t g_secret[32];
static String g_installId, g_secretB64, g_privB64, g_pubB64, g_baseString;
static bool g_ready = false;
static bool g_rngReady = false;

static bool ensureRng() {
    if (g_rngReady) return true;
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr);
    const char *seed = "lamarzocco-display";
    if (mbedtls_ctr_drbg_seed(&g_ctr, mbedtls_entropy_func, &g_entropy, (const uint8_t *)seed,
                              strlen(seed)) != 0)
        return false;
    g_rngReady = true;
    return true;
}

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
    if (!ensureRng()) return false;
    mbedtls_pk_init(&g_pk);

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
    g_secretB64 = secretB64;
    g_privB64 = privKeyDerB64;
    g_ready = true;
    return true;
}

bool lmAuthGenerate() {
    if (!ensureRng()) return false;
    mbedtls_pk_init(&g_pk);
    if (mbedtls_pk_setup(&g_pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) return false;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(g_pk),
                            mbedtls_ctr_drbg_random, &g_ctr) != 0)
        return false;

    uint8_t buf[400];
    int len = mbedtls_pk_write_key_der(&g_pk, buf, sizeof(buf));  // written at the END of buf
    if (len < 0) return false;
    g_privB64 = b64(buf + sizeof(buf) - len, len);

    uint8_t pbuf[300];
    int plen = mbedtls_pk_write_pubkey_der(&g_pk, pbuf, sizeof(pbuf));
    if (plen < 0) return false;
    uint8_t *pub = pbuf + sizeof(pbuf) - plen;
    g_pubB64 = b64(pub, plen);

    g_installId = uuid4();

    // secret = sha256(installId . b64(pubDer) . b64(sha256(installId)))
    uint8_t ih[32];
    mbedtls_sha256((const uint8_t *)g_installId.c_str(), g_installId.length(), ih, 0);
    String triple = g_installId + "." + g_pubB64 + "." + b64(ih, 32);
    mbedtls_sha256((const uint8_t *)triple.c_str(), triple.length(), g_secret, 0);
    g_secretB64 = b64(g_secret, 32);

    // base string for the registration proof = installId . b64(sha256(pubDer))
    uint8_t ph[32];
    mbedtls_sha256(pub, plen, ph, 0);
    g_baseString = g_installId + "." + b64(ph, 32);

    g_ready = true;
    return !g_privB64.isEmpty() && !g_pubB64.isEmpty();
}

String lmAuthInstallId() { return g_installId; }
String lmAuthSecretB64() { return g_secretB64; }
String lmAuthPrivKeyB64() { return g_privB64; }
String lmAuthPubKeyB64() { return g_pubB64; }
String lmAuthBaseString() { return g_baseString; }
String lmAuthProof(const String &base) { return makeProof(base); }

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
