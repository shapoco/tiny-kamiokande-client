#ifndef TKRP2_LWIPOPTS_H
#define TKRP2_LWIPOPTS_H

// lwIP の設定 (pico_cyw43_arch_lwip_threadsafe_background。pico-examples の pico_w/wifi に倣う)。
// 通信の処理は WiFi を初期化したコア (core1) の割り込みで動く。
// TLS のレコード (最大 16 KB) は全部届くまで復号できないので、受信の窓と pbuf のプールは 16 KB 以上を持つ

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define MEM_LIBC_MALLOC 0  // 割り込みから確保するので libc の malloc は使わない
#define MEM_ALIGNMENT 4
#define MEM_SIZE 8000      // 送信の pbuf と TLS の接続の状態
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_ARP_QUEUE 10
#define PBUF_POOL_SIZE 16  // 受信 (1 個 TCP_MSS + ヘッダ)。TLS の 1 レコード分 + 余裕
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define TCP_MSS 1460
#define TCP_WND 16384
#define TCP_SND_BUF (2 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

// TLS (altcp_tls + mbedTLS)。証明書は必ず検証する (既定は OPTIONAL で、失敗しても繋がってしまう)
#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define ALTCP_MBEDTLS_AUTHMODE MBEDTLS_SSL_VERIFY_REQUIRED

#endif
