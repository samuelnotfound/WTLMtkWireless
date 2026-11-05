// SPDX-License-Identifier: ISC AND BSD-3-Clause-Clear
// Shared fixed fields from WTL's authentication and association exchange.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint16_t mtk_join_le16(const uint8_t *bytes)
{
    return uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
}

struct MtkAuthResponse {
    uint16_t algorithm, transaction, status;
    bool accepted() const { return algorithm == 0 && transaction == 2 && status == 0; }
};

static inline bool mtk_auth_response(const uint8_t *body, size_t length, MtkAuthResponse &reply)
{
    if (length < 6) return false;
    reply = {mtk_join_le16(body), mtk_join_le16(body + 2), mtk_join_le16(body + 4)};
    return true;
}

struct MtkAssocResponse {
    uint16_t status, aid;
    bool accepted() const { return status == 0 && aid >= 1 && aid <= 2007; }
};

static inline bool mtk_assoc_response(const uint8_t *body, size_t length, MtkAssocResponse &reply)
{
    if (length < 6) return false;
    reply = {mtk_join_le16(body + 2), uint16_t(mtk_join_le16(body + 4) & 0x3fff)};
    return true;
}

// The caller supplies space for the fixed fields and the validated SSID.
static inline uint8_t *mtk_assoc_prefix(uint8_t *out, bool shortPreamble, bool privacy,
                                       const char *ssid, uint8_t length)
{
    const uint16_t capability = 0x0401 | (shortPreamble ? 0x20 : 0) | (privacy ? 0x10 : 0);
    out[0] = uint8_t(capability);
    out[1] = uint8_t(capability >> 8);
    out[2] = 10;
    out[3] = 0;
    out[4] = 0;
    out[5] = length;
    memcpy(out + 6, ssid, length);
    return out + 6 + length;
}
