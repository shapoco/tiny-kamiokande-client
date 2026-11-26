#ifndef TKRP2_MBEDTLS_CONFIG_H
#define TKRP2_MBEDTLS_CONFIG_H

// mbedTLS 2.28 (Pico SDK 同梱) の設定。TLS 1.2 のクライアントだけ
// (pico-examples の mbedtls_config_examples_common.h を元に、サーバの ECDHE-RSA と X25519 を足し、使わない曲線を外した)。
// 受信のレコード長は既定の 16 KB のまま: サーバは max_fragment_length (4 KB) に応じるが、
// mbedTLS 2.28 は複数のレコードに分かれたハンドシェイクを受け取れず、証明書の連鎖 (現在 4079 バイト) が
// 4 KB を超えた時点で繋がらなくなるため。
// 時刻 (MBEDTLS_HAVE_TIME_DATE) は持たないので、証明書の有効期限は確かめない (RTC が無い)

#include <limits.h>

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT  // pico_mbedtls の mbedtls_hardware_poll (pico_rand)
#define MBEDTLS_HAVE_TIME             // セッションチケットの寿命 (time())

#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048  // 送るのは小さな GET だけ

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

// 楕円曲線: X25519 (サーバの ECDHE)、P-256 / P-384 (ECDSA の証明書。ISRG Root X2 は P-384)
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

// 鍵交換: ECDHE-RSA (現在のサーバ)、ECDHE-ECDSA
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_SESSION_TICKETS        // 繋ぎ直しを短いハンドシェイクで済ませる
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

#define MBEDTLS_AES_C
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

#endif
