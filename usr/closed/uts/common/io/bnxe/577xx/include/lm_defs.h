/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    10/08/02 Hav Khauv        Inception.
 ******************************************************************************/

#ifndef _LM_DEFS_H
#define _LM_DEFS_H

#include "bcmtype.h"



/*******************************************************************************
 * Simple constants.
 ******************************************************************************/

#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif

#ifndef NULL
#define NULL    ((void *) 0)
#endif


/* Signatures for integrity checks. */
#define LM_DEVICE_SIG           0x6d635242      /* BRcm */
#define L2PACKET_RX_SIG         0x7872324c      /* L2rx */
#define L2PACKET_TX_SIG         0x7874324c      /* L2tx */
#define L4BUFFER_RX_SIG         0x7872344c      /* L4rx */
#define L4BUFFER_TX_SIG         0x7874344c      /* L4tx */
#define L4BUFFER_SIG            0x66754254      /* TBuf */
#define L4GEN_BUFFER_SIG        0x006e6567      /* gen  */
#define L4GEN_BUFFER_SIG_END    0x0067656e      /* neg  */

#define SIZEOF_SIG              16
#define SIG(_p)                 (*((u32_t *) ((u8_t *) (_p) - sizeof(u32_t))))
#define END_SIG(_p, _size)      (*((u32_t *) ((u8_t *) (_p) + (_size))))


/* This macro rounds the given value to the next word boundary if it
 * is not already at a word boundary. */
#define ALIGN_VALUE_TO_WORD_BOUNDARY(_v) \
    (((_v) + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1))

/* This macro determines the delta to the next alignment which is
 * either 1, 2, 4, 8, 16, 32, etc. */
#define ALIGN_DELTA_TO_BOUNDARY(_p, _a) \
        (((((u8_t *) (_p) - (u8_t *) 0) + ((_a) - 1)) & ~((_a) - 1)) - \
              ((u8_t *) (_p) - (u8_t *) 0))

/* This macro returns the pointer to the next alignment if the pointer
 * is not currently on the indicated alignment boundary. */
#define ALIGN_PTR_TO_BOUNDARY(_p, _a) \
        ((u8_t *) (_p) + ALIGN_DELTA_TO_BOUNDARY(_p, _a))



/*******************************************************************************
 * Status codes.
 ******************************************************************************/

typedef enum
{
    LM_STATUS_SUCCESS                 =     0,
    LM_STATUS_FAILURE                 =     1,
    LM_STATUS_RESOURCE                =     2,
    LM_STATUS_ABORTED                 =     3,
    LM_STATUS_PENDING                 =     4,
    LM_STATUS_PAUSED                  =     5,
    LM_STATUS_INVALID_PARAMETER       =     6,
    LM_STATUS_LINK_ACTIVE             =     7,
    LM_STATUS_LINK_DOWN               =     8,
    LM_STATUS_UNKNOWN_ADAPTER         =     9,
    LM_STATUS_UNKNOWN_PHY             =     10,
    LM_STATUS_UNKNOWN_MEDIUM          =     11,
    LM_STATUS_TOO_MANY_FRAGMENTS      =     12,
    LM_STATUS_BUFFER_TOO_SHORT        =     16,
    LM_STATUS_UPLOAD_IN_PROGRESS      =     17,
    LM_STATUS_BUSY                    =     18,
    LM_STATUS_INVALID_KEY             =     19,
    LM_STATUS_TIMEOUT                 =     20,
    LM_STATUS_REQUEST_NOT_ACCEPTED    =     21,
    LM_STATUS_CONNECTION_CLOSED       =     22,
    LM_STATUS_BAD_SIGNATURE           =     23,
    LM_STATUS_CONNECTION_RESET        =     24,
    LM_STATUS_EXISTING_OBJECT         =     25,
    LM_STATUS_OBJECT_NOT_FOUND        =     26,
    LM_STATUS_CONNECTION_RM_DISC      =     27,
    LM_STATUS_UNKNOWN_EVENT_CODE      =     28
} lm_status_t;


/*******************************************************************************
 * Receive filter masks.
 ******************************************************************************/

typedef u32_t lm_rx_mask_t;

#define LM_RX_MASK_ACCEPT_NONE                  0x0000
#define LM_RX_MASK_ACCEPT_UNICAST               0x0001
#define LM_RX_MASK_ACCEPT_MULTICAST             0x0002
#define LM_RX_MASK_ACCEPT_ALL_MULTICAST         0x0004
#define LM_RX_MASK_ACCEPT_BROADCAST             0x0008
#define LM_RX_MASK_ACCEPT_ERROR_PACKET          0x0010

#define LM_RX_MASK_PROMISCUOUS_MODE             0x10000



/*******************************************************************************
 * Flow control.
 ******************************************************************************/

typedef u32_t lm_flow_control_t;

#define LM_FLOW_CONTROL_NONE                    0x00
#define LM_FLOW_CONTROL_RECEIVE_PAUSE           0x01
#define LM_FLOW_CONTROL_TRANSMIT_PAUSE          0x02

/* This value can be or-ed with RECEIVE_PAUSE and TRANSMIT_PAUSE.  If the
 * auto-negotiation is disabled and the RECEIVE_PAUSE and TRANSMIT_PAUSE
 * bits are set, then flow control is enabled regardless of link partner's
 * flow control capability.  Otherwise, if this bit is set, then flow
 * is negotiated with the link partner.  Values 0x80000000 and 0x80000003 are
 * equivalent. */
#define LM_FLOW_CONTROL_AUTO_PAUSE              0x80000000



/*******************************************************************************
 * media type.
 ******************************************************************************/

typedef u32_t lm_medium_t;

#define LM_MEDIUM_AUTO_DETECT                   0x0000

#define LM_MEDIUM_TYPE_UNKNOWN                  0x0000
#define LM_MEDIUM_TYPE_BNC                      0x0001
#define LM_MEDIUM_TYPE_UTP                      0x0002
#define LM_MEDIUM_TYPE_FIBER                    0x0003
#define LM_MEDIUM_TYPE_SERDES                   0x0004
#define LM_MEDIUM_TYPE_SERDES_SGMII             0x0005
#define LM_MEDIUM_TYPE_XGXS                     0x0006
#define LM_MEDIUM_TYPE_XGXS_SGMII               0x0007
#define LM_MEDIUM_TYPE_XMAC_LOOPBACK            0x0008
#define LM_MEDIUM_TYPE_UMAC_LOOPBACK            0x0009
#define LM_MEDIUM_TYPE_EXT_LOOPBACK             0x00f6
#define LM_MEDIUM_TYPE_EXT_PHY_LOOPBACK         0x00f7
#define LM_MEDIUM_TYPE_SERDES_LOOPBACK          0x00f8
#define LM_MEDIUM_TYPE_XGXS_LOOPBACK            0x00f9
#define LM_MEDIUM_TYPE_XGXS_10_LOOPBACK         0x00fa
#define LM_MEDIUM_TYPE_BMAC_LOOPBACK            0x00fb
#define LM_MEDIUM_TYPE_EMAC_LOOPBACK            0x00fc
#define LM_MEDIUM_TYPE_PHY_LOOPBACK             0x00fd
#define LM_MEDIUM_TYPE_MAC_LOOPBACK             0x00fe
#define LM_MEDIUM_TYPE_NULL                     0x00ff
#define LM_MEDIUM_TYPE_MASK                     0x00ff
#define GET_MEDIUM_TYPE(m)                      ((m) & LM_MEDIUM_TYPE_MASK)
#define SET_MEDIUM_TYPE(m, t) \
    (m) = ((m) & ~LM_MEDIUM_TYPE_MASK) | (t)

#define LM_MEDIUM_SPEED_AUTONEG                 0x0000

#define LM_MEDIUM_SPEED_UNKNOWN                 0x0000
#define LM_MEDIUM_SPEED_10MBPS                  0x0100
#define LM_MEDIUM_SPEED_100MBPS                 0x0200
#define LM_MEDIUM_SPEED_1000MBPS                0x0300
#define LM_MEDIUM_SPEED_2500MBPS                0x0400
#define LM_MEDIUM_SPEED_10GBPS                  0x0600
#define LM_MEDIUM_SPEED_12GBPS                  0x0700
#define LM_MEDIUM_SPEED_12_5GBPS                0x0800
#define LM_MEDIUM_SPEED_13GBPS                  0x0900
#define LM_MEDIUM_SPEED_15GBPS                  0x0a00
#define LM_MEDIUM_SPEED_16GBPS                  0x0b00
#define LM_MEDIUM_SPEED_20GBPS                  0x0c00
#define LM_MEDIUM_SPEED_SEQ_START               0x1d00  // 100Mbps
#define LM_MEDIUM_SPEED_SEQ_END                 0x8000  // 10Gbps
#define LM_MEDIUM_SPEED_AUTONEG_1G_FALLBACK     0x8300  /* Serdes */
#define LM_MEDIUM_SPEED_AUTONEG_2_5G_FALLBACK   0x8400  /* Serdes */
#define LM_MEDIUM_SPEED_HARDWARE_DEFAULT        0xff00  /* Serdes nvram def. */
#define LM_MEDIUM_SPEED_MASK                    0xff00
#define GET_MEDIUM_SPEED(m)                     ((m) & LM_MEDIUM_SPEED_MASK)
#define SET_MEDIUM_SPEED(m, s) \
    (m) = ((m) & ~LM_MEDIUM_SPEED_MASK) | (s)

#define LM_MEDIUM_FULL_DUPLEX                   0x00000
#define LM_MEDIUM_HALF_DUPLEX                   0x10000
#define GET_MEDIUM_DUPLEX(m)                    ((m) & LM_MEDIUM_HALF_DUPLEX)
#define SET_MEDIUM_DUPLEX(m, d) \
    (m) = ((m) & ~LM_MEDIUM_HALF_DUPLEX) | (d)

#define LM_MEDIUM_SELECTIVE_AUTONEG             0x01000000
#define GET_MEDIUM_AUTONEG_MODE(m)              ((m) & 0xff000000)

typedef struct _lm_link_settings_t
{
    u32_t flag;
    #define LINK_FLAG_SELECTIVE_AUTONEG_MASK                    0x0f
    #define LINK_FLAG_SELECTIVE_AUTONEG_ONE_SPEED               0x01
    #define LINK_FLAG_SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS    0x02
    #define LINK_FLAG_WIRE_SPEED                                0x10

    lm_medium_t req_medium;
    lm_flow_control_t flow_ctrl;

    u32_t _reserved;
} lm_link_settings_t;



/*******************************************************************************
 * Power state.
 ******************************************************************************/

typedef enum
{
    LM_POWER_STATE_D0 = 0,
    LM_POWER_STATE_D1 = 1,
    LM_POWER_STATE_D2 = 2,
    LM_POWER_STATE_D3 = 3
} lm_power_state_t;



/*******************************************************************************
 * offloading.
 ******************************************************************************/

typedef u32_t lm_offload_t;

#define LM_OFFLOAD_NONE                         0x00000000
#define LM_OFFLOAD_TX_IP_CKSUM                  0x00000001
#define LM_OFFLOAD_RX_IP_CKSUM                  0x00000002
#define LM_OFFLOAD_TX_TCP_CKSUM                 0x00000004
#define LM_OFFLOAD_RX_TCP_CKSUM                 0x00000008
#define LM_OFFLOAD_TX_UDP_CKSUM                 0x00000010
#define LM_OFFLOAD_RX_UDP_CKSUM                 0x00000020
#define LM_OFFLOAD_IPV4_TCP_LSO                 0x00000040
#define LM_OFFLOAD_IPV6_TCP_LSO                 0x00000080
#define LM_OFFLOAD_CHIMNEY                      0x00000100
#define LM_OFFLOAD_IPV6_CHIMNEY                 0x00000200
#define LM_OFFLOAD_TX_TCP6_CKSUM                0x00001000
#define LM_OFFLOAD_RX_TCP6_CKSUM                0x00002000
#define LM_OFFLOAD_TX_UDP6_CKSUM                0x00004000
#define LM_OFFLOAD_RX_UDP6_CKSUM                0x00008000



/*******************************************************************************
 * RSS Hash Types
 ******************************************************************************/

typedef u32_t lm_rss_hash_t;

#define LM_RSS_HASH_IPV4                       0x00000100
#define LM_RSS_HASH_TCP_IPV4                   0x00000200
#define LM_RSS_HASH_IPV6                       0x00000400
#define LM_RSS_HASH_IPV6_EX                    0x00000800
#define LM_RSS_HASH_TCP_IPV6                   0x00001000
#define LM_RSS_HASH_TCP_IPV6_EX                0x00002000



/*******************************************************************************
 * Chip reset reasons.
 ******************************************************************************/

typedef enum
{
    LM_REASON_NONE                       =  0,
    LM_REASON_DRIVER_RESET               =  1,
    LM_REASON_DRIVER_UNLOAD              =  2,
    LM_REASON_DRIVER_SHUTDOWN            =  3,
    LM_REASON_WOL_SUSPEND                =  4,
    LM_REASON_NO_WOL_SUSPEND             =  5,
    LM_REASON_DIAG                       =  6,
    LM_REASON_DRIVER_UNLOAD_POWER_DOWN   =  7,   /* Power down phy/serdes */
    LM_REASON_ERROR_RECOVERY             =  8
} lm_reason_t;



/*******************************************************************************
 * Wake up mode.
 ******************************************************************************/

typedef u32_t lm_wake_up_mode_t;

#define LM_WAKE_UP_MODE_NONE                    0
#define LM_WAKE_UP_MODE_MAGIC_PACKET            1
#define LM_WAKE_UP_MODE_NWUF                    2
#define LM_WAKE_UP_MODE_LINK_CHANGE             4



/*******************************************************************************
 * Event code.
 ******************************************************************************/
typedef enum
{
    LM_EVENT_CODE_LINK_CHANGE               = 0,
    LM_EVENT_CODE_PAUSE_OFFLOAD             = 1,
    LM_EVENT_CODE_RESUME_OFFLOAD            = 2,
    LM_EVENT_CODE_STOP_CHIP_ACCESS          = 3, /* For Error Recovery Flow */
    LM_EVENT_CODE_RESTART_CHIP_ACCESS       = 4,  /* For Error Recovery Flow */
    LM_EVENT_CODE_UPLOAD_ALL                = 5
} lm_event_code_t;





/*******************************************************************************
 * Transmit control flags.
 ******************************************************************************/

typedef u32_t lm_tx_flag_t;

#define LM_TX_FLAG_INSERT_VLAN_TAG              0x01
#define LM_TX_FLAG_COMPUTE_IP_CKSUM             0x02
#define LM_TX_FLAG_COMPUTE_TCP_UDP_CKSUM        0x04
#define LM_TX_FLAG_TCP_LSO_FRAME                0x08
#define LM_TX_FLAG_TCP_LSO_SNAP_FRAME           0x10
#define LM_TX_FLAG_COAL_NOW                     0x20
#define LM_TX_FLAG_DONT_COMPUTE_CRC             0x40
#define LM_TX_FLAG_SKIP_MBQ_WRITE               0x80
#define LM_TX_FLAG_IPV6_PACKET                  0x100
#define LM_TX_FLAG_VLAN_TAG_EXISTS              0x200

typedef struct _lm_pkt_tx_info_t
{
    lm_tx_flag_t flags;

    u16_t vlan_tag;
    u16_t lso_mss;
    u16_t lso_ip_hdr_len;
    u16_t lso_tcp_hdr_len;
    u32_t lso_payload_len;

    /* Everest only fields. */
    u32_t lso_tcp_send_seq;
    u16_t lso_ipid;
    u16_t tcp_pseudo_csum;
    u8_t  lso_tcp_flags;
    u8_t  tcp_nonce_sum_bit;
    u16_t  _reserved1;

    u8_t dst_mac_addr[8];
    s8_t cs_any_offset;

    u32_t _unused[4];
} lm_pkt_tx_info_t;



/*******************************************************************************
 * Receive control flags.
 ******************************************************************************/

typedef u32_t lm_rx_flag_t;

#define LM_RX_FLAG_VALID_VLAN_TAG               0x01
#define LM_RX_FLAG_VALID_HASH_VALUE             0x10

#define LM_RX_FLAG_IS_IPV4_DATAGRAM             0x0100
#define LM_RX_FLAG_IS_IPV6_DATAGRAM             0x0200
#define LM_RX_FLAG_IP_CKSUM_IS_GOOD             0x0400
#define LM_RX_FLAG_IP_CKSUM_IS_BAD              0x0800

#define LM_RX_FLAG_IS_UDP_DATAGRAM              0x1000
#define LM_RX_FLAG_UDP_CKSUM_IS_GOOD            0x2000
#define LM_RX_FLAG_UDP_CKSUM_IS_BAD             0x4000

#define LM_RX_FLAG_IS_TCP_SEGMENT               0x010000
#define LM_RX_FLAG_TCP_CKSUM_IS_GOOD            0x020000
#define LM_RX_FLAG_TCP_CKSUM_IS_BAD             0x040000

typedef struct _lm_pkt_rx_info_t
{
    lm_rx_flag_t flags;

    u32_t size;

    u16_t vlan_tag;
    u16_t _pad;

    /* Virtual address corresponding to the first byte of the first SGL entry.
     * This is the starting location of the packet which may begin with some
     * control information. */
    u8_t *mem_virt;
    u32_t mem_size;

    u32_t _unused[4];
} lm_pkt_rx_info_t;



/*******************************************************************************
 * various type of counters.
 ******************************************************************************/

typedef enum
{
    LM_STATS_BASE                       = 0x686b3000,
    LM_STATS_FRAMES_XMITTED_OK          = 0x686b3001,
    LM_STATS_FRAMES_RECEIVED_OK         = 0x686b3002,
    LM_STATS_ERRORED_TRANSMIT_CNT       = 0x686b3003,
    LM_STATS_ERRORED_RECEIVE_CNT        = 0x686b3004,
    LM_STATS_RCV_CRC_ERROR              = 0x686b3005,
    LM_STATS_ALIGNMENT_ERROR            = 0x686b3006,
    LM_STATS_SINGLE_COLLISION_FRAMES    = 0x686b3007,
    LM_STATS_MULTIPLE_COLLISION_FRAMES  = 0x686b3008,
    LM_STATS_FRAMES_DEFERRED            = 0x686b3009,
    LM_STATS_MAX_COLLISIONS             = 0x686b300a,
    LM_STATS_RCV_OVERRUN                = 0x686b300b,
    LM_STATS_XMIT_UNDERRUN              = 0x686b300c,
    LM_STATS_UNICAST_FRAMES_XMIT        = 0x686b300d,
    LM_STATS_MULTICAST_FRAMES_XMIT      = 0x686b300e,
    LM_STATS_BROADCAST_FRAMES_XMIT      = 0x686b300f,
    LM_STATS_UNICAST_FRAMES_RCV         = 0x686b3010,
    LM_STATS_MULTICAST_FRAMES_RCV       = 0x686b3011,
    LM_STATS_BROADCAST_FRAMES_RCV       = 0x686b3012,
    LM_STATS_RCV_NO_BUFFER_DROP         = 0x686b3013,
    LM_STATS_BYTES_RCV                  = 0x686b3014,
    LM_STATS_BYTES_XMIT                 = 0x686b3015,
    LM_STATS_IP4_OFFLOAD                = 0x686b3016,
    LM_STATS_TCP_OFFLOAD                = 0x686b3017,
    LM_STATS_IF_IN_DISCARDS             = 0x686b3018,
    LM_STATS_IF_IN_ERRORS               = 0x686b3019,
    LM_STATS_IF_OUT_ERRORS              = 0x686b301a,
    LM_STATS_IP6_OFFLOAD                = 0x686b301b,
    LM_STATS_TCP6_OFFLOAD               = 0x686b301c,
    LM_STATS_XMIT_DISCARDS              = 0x686b301d,
    LM_STATS_DIRECTED_BYTES_RCV         = 0x686b301e,
    LM_STATS_MULTICAST_BYTES_RCV        = 0x686b301f,
    LM_STATS_BROADCAST_BYTES_RCV        = 0x686b3020,
    LM_STATS_DIRECTED_BYTES_XMIT        = 0x686b3021,
    LM_STATS_MULTICAST_BYTES_XMIT       = 0x686b3022,
    LM_STATS_BROADCAST_BYTES_XMIT       = 0x686b3023,
} lm_stats_t;

#define NUM_OF_LM_STATS                       36



/*******************************************************************************
 * 64-bit value.
 ******************************************************************************/

typedef union _lm_u64_t
{
    struct _lm_u64_as_u32_t
    {
        #ifdef BIG_ENDIAN_HOST
        u32_t high;
        u32_t low;
        #else
        u32_t low;
        u32_t high;
        #endif
    } as_u32;

    u64_t as_u64;

    void *as_ptr;
} lm_u64_t;


typedef lm_u64_t lm_address_t;


/* 64-bit increment.  The second argument is a 32-bit value. */
#define LM_INC64(result, addend32)          \
    {                                       \
        u32_t low;                          \
                                            \
        low = (result)->as_u32.low;         \
        (result)->as_u32.low += (addend32); \
        if((result)->as_u32.low < low)      \
        {                                   \
            (result)->as_u32.high++;        \
        }                                   \
    }


/* 64-bit decrement.  The second argument is a 32-bit value. */
#define LM_DEC64(result, addend32)          \
    {                                       \
        u32_t low;                          \
                                            \
        low = (result)->as_u32.low;         \
        (result)->as_u32.low -= (addend32); \
        if((result)->as_u32.low > low)      \
        {                                   \
            (result)->as_u32.high--;        \
        }                                   \
    }

/*******************************************************************************
 * IP4 and TCP offload stats.
 ******************************************************************************/

typedef struct _lm_ip4_offload_stats_t
{
    u64_t in_receives;
    u64_t in_delivers;
    u64_t out_requests;
    u32_t in_header_errors;
    u32_t in_discards;
    u32_t out_discards;
    u32_t out_no_routes;

    u32_t _pad[8];
} lm_ip4_offload_stats_t;


typedef struct _lm_tcp_offload_stats_t
{
    u64_t in_segments;
    u64_t out_segments;
    u32_t retran_segments;
    u32_t in_errors;
    u32_t out_resets;

    u32_t _pad[8];
} lm_tcp_offload_stats_t;



/*******************************************************************************
 * Host to network order conversion.
 ******************************************************************************/

#ifdef BIG_ENDIAN_HOST

#ifndef HTON16
#define HTON16(_val16)      (_val16)
#endif
#ifndef HTON32
#define HTON32(_val32)      (_val32)
#ifndef NTOH16
#endif
#define NTOH16(_val16)      (_val16)
#endif
#ifndef NTOH32
#define NTOH32(_val32)      (_val32)
#endif

#else

#ifndef HTON16
#define HTON16(_val16)      (((_val16 & 0xff00) >> 8) | ((_val16 & 0xff) << 8))
#endif
#ifndef HTON32
#define HTON32(_val32)      ((HTON16(_val32) << 16) | (HTON16(_val32 >> 16)))
#endif
#ifndef NTOH16
#define NTOH16(_val16)      HTON16(_val16)
#endif
#ifndef NTOH32
#define NTOH32(_val32)      HTON32(_val32)
#endif

#endif



/*******************************************************************************
 * Fragment structure.
 ******************************************************************************/

typedef struct _lm_frag_t
{
    lm_address_t addr;
    u32_t size;

    #if defined(_WIN64)     /* mirror the SCATTER_GATHER_ELEMENT structure. */
    u64_t _reserved;
    #else
    u32_t _reserved;
    #endif
} lm_frag_t;

typedef struct _lm_frag_list_t
{
    u32_t cnt;

    #if defined(_WIN64)     /* mirror the SCATTER_GATHER_LIST structure. */
    u64_t size;
    #else
    u32_t size;
    #endif

    lm_frag_t frag_arr[1];
} lm_frag_list_t;

/* a macro for declaring 'lm_frag_list_t' with various array sizes. */
#define DECLARE_FRAG_LIST_BUFFER_TYPE(_FRAG_LIST_TYPE_NAME, _MAX_FRAG_CNT)   \
    typedef struct _##_FRAG_LIST_TYPE_NAME                                   \
    {                                                                        \
        lm_frag_list_t list;                                                 \
        lm_frag_t frag_arr[_MAX_FRAG_CNT-1];                                 \
    } _FRAG_LIST_TYPE_NAME



/*******************************************************************************
 * Macro fore calculating the address of the base of the structure given its
 * type, and an address of a field within the structure.
 ******************************************************************************/

#define GET_CONTAINING_RECORD(address, type, field) \
    ((type *) ((u8_t *) (address) - (u8_t *) (&((type *) 0)->field)))



/*******************************************************************************
 * Simple macros.
 ******************************************************************************/

#define IS_ETH_BROADCAST(eth_addr)                                       \
    (((unsigned char *) (eth_addr))[0] == ((unsigned char) 0xff))

#define IS_ETH_MULTICAST(eth_addr)                                       \
    (((unsigned char *) (eth_addr))[0] & ((unsigned char) 0x01))

#define IS_ETH_ADDRESS_EQUAL(eth_addr1, eth_addr2)                       \
    ((((unsigned char *) (eth_addr1))[0] ==                              \
    ((unsigned char *) (eth_addr2))[0]) &&                               \
    (((unsigned char *) (eth_addr1))[1] ==                               \
    ((unsigned char *) (eth_addr2))[1]) &&                               \
    (((unsigned char *) (eth_addr1))[2] ==                               \
    ((unsigned char *) (eth_addr2))[2]) &&                               \
    (((unsigned char *) (eth_addr1))[3] ==                               \
    ((unsigned char *) (eth_addr2))[3]) &&                               \
    (((unsigned char *) (eth_addr1))[4] ==                               \
    ((unsigned char *) (eth_addr2))[4]) &&                               \
    (((unsigned char *) (eth_addr1))[5] ==                               \
    ((unsigned char *) (eth_addr2))[5]))

#define COPY_ETH_ADDRESS(src, dst)                                       \
    ((unsigned char *) (dst))[0] = ((unsigned char *) (src))[0];         \
    ((unsigned char *) (dst))[1] = ((unsigned char *) (src))[1];         \
    ((unsigned char *) (dst))[2] = ((unsigned char *) (src))[2];         \
    ((unsigned char *) (dst))[3] = ((unsigned char *) (src))[3];         \
    ((unsigned char *) (dst))[4] = ((unsigned char *) (src))[4];         \
    ((unsigned char *) (dst))[5] = ((unsigned char *) (src))[5];

#endif /* _LM_DEFS_H */
