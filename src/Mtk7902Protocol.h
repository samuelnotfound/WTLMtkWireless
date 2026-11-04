// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "MtkUserApi.h"
#include <string.h>

// Preserve the distinction between an AP rejection and no response. Neither is a wrong password.
static inline uint32_t mtk7902_join_result(const char *stage) {
    if (!strncmp(stage, "scan", 4)) return MTK_JOIN_NOT_FOUND;
    if (!strncmp(stage, "security", 8)) return MTK_JOIN_SECURITY;
    if (!strncmp(stage, "auth-timeout", 12)) return MTK_JOIN_AUTH_TIMEOUT;
    if (!strncmp(stage, "auth-fail", 9)) return MTK_JOIN_AUTH_REJECT;
    if (!strncmp(stage, "assoc-timeout", 13)) return MTK_JOIN_ASSOC_TIMEOUT;
    if (!strncmp(stage, "assoc-fail", 10)) return MTK_JOIN_ASSOC_REJECT;
    if (!strncmp(stage, "hs-fail", 7)) return MTK_JOIN_HANDSHAKE_TIMEOUT;
    return MTK_JOIN_ROC;
}

static inline bool mtk7902_valid_join(const mtk_join_params *p) {
    if (!p || p->version != MTK_USER_API_VERSION || !p->ssid_len || p->ssid_len > 32 || p->key_type != MTK_KEY_PMK)
        return false; // current Connac2 backend implements WPA2-PSK/CCMP only
    // The radio's selector uses a C string. Preserve the complete SSID and validate an optional unicast BSSID.
    for (unsigned i = 0; i < p->ssid_len; ++i) if (!p->ssid[i]) return false;
    if (p->bssid[0] & 1) return false;
    return true;
}
