#ifndef __HW_BUFFERS_H__
#define __HW_BUFFERS_H__

#define PAGESIZE_16     16
#define PAGESIZE_4K     4096

#define MAX_PACKET_SIZE           (kIOEthernetMaxPacketSize + 4)

//
// Transmit buffer descriptor.
//
typedef struct {
  UInt32 addrHi;
  UInt32 addrLo;
  UInt32 length;
  UInt16 flags;
#define TX_BD_FLAGS_CONN_FAULT      BIT(0)
#define TX_BD_FLAGS_TCP_UDP_CKSUM   BIT(1)
#define TX_BD_FLAGS_IP_CKSUM        BIT(2)
#define TX_BD_FLAGS_VLAN_TAG        BIT(3)
#define TX_BD_FLAGS_COAL_NOW        BIT(4)
#define TX_BD_FLAGS_DONT_GEN_CRC    BIT(5)
#define TX_BD_FLAGS_END             BIT(6)
#define TX_BD_FLAGS_START           BIT(7)
#define TX_BD_FLAGS_SW_OPTION_WORD  (0x1F<<8)
#define TX_BD_FLAGS_SW_FLAGS        BIT(13)
#define TX_BD_FLAGS_SW_SNAP         BIT(14)
#define TX_BD_FLAGS_SW_LSO          BIT(15)
  UInt16 vlanTag;
} tx_bd_t;

#define TX_PAGE_BITS                14
#define TX_PAGE_SIZE                BIT(TX_PAGE_BITS)
#define TX_MAX_BD_COUNT             (TX_PAGE_SIZE / sizeof (tx_bd_t))
#define TX_USABLE_BD_COUNT          (TX_MAX_BD_COUNT - 1)
#define TX_MAX_SEG_COUNT            512

#define TX_NEXT_BD(x)               (((x) & TX_USABLE_BD_COUNT) == (TX_USABLE_BD_COUNT - 1)) ? (x) + 2 : (x) + 1
#define TX_BD_INDEX(x)              ((x) & TX_USABLE_BD_COUNT)

#define TX_INT_TICKS                80
#define TX_QUICK_CONS_TRIP          20

//
// Receive buffer descriptor.
//
typedef struct {
  UInt32 addrHi;
  UInt32 addrLo;
  UInt32 length;
  UInt32 flags;
#define RX_BD_FLAGS_NOPUSH          BIT(0)
#define RX_BD_FLAGS_DUMMY           BIT(1)
#define RX_BD_FLAGS_END             BIT(2)
#define RX_BD_FLAGS_START           BIT(3)
} rx_bd_t;

typedef struct {
  UInt16 status;
    #define L2_FHDR_STATUS_RULE_CLASS       (0x7<<0)
    #define L2_FHDR_STATUS_RULE_P2          BIT(3)
    #define L2_FHDR_STATUS_RULE_P3          BIT(4)
    #define L2_FHDR_STATUS_RULE_P4          BIT(5)
    #define L2_FHDR_STATUS_L2_VLAN_TAG      BIT(6)
    #define L2_FHDR_STATUS_L2_LLC_SNAP      BIT(7)
    #define L2_FHDR_STATUS_RSS_HASH         BIT(8)
    #define L2_FHDR_STATUS_IP_DATAGRAM      BIT(13)
    #define L2_FHDR_STATUS_TCP_SEGMENT      BIT(14)
    #define L2_FHDR_STATUS_UDP_DATAGRAM     BIT(15)
  UInt16 errors;
    #define L2_FHDR_STATUS_SPLIT            BIT(0)
    #define L2_FHDR_ERRORS_BAD_CRC          BIT(1)
    #define L2_FHDR_ERRORS_PHY_DECODE       BIT(2)
    #define L2_FHDR_ERRORS_ALIGNMENT        BIT(3)
    #define L2_FHDR_ERRORS_TOO_SHORT        BIT(4)
    #define L2_FHDR_ERRORS_GIANT_FRAME      BIT(5)
    #define L2_FHDR_ERRORS_IPV4_BAD_LEN     BIT(6)
    #define L2_FHDR_ERRORS_TCP_XSUM         BIT(12)
    #define L2_FHDR_ERRORS_UDP_XSUM         BIT(15)

  UInt32 hash;
  UInt16 vlanTag;
  UInt16 packetLength;
  UInt16 tcpUdpChecksum;
  UInt16 ipChecksum;
} rx_l2_header_t;

#define RX_PAGE_BITS                14
#define RX_PAGE_SIZE                BIT(RX_PAGE_BITS)
#define RX_MAX_BD_COUNT             (RX_PAGE_SIZE / sizeof (rx_bd_t))
#define RX_USABLE_BD_COUNT          (RX_MAX_BD_COUNT - 1)
#define RX_MAX_SEG_COUNT            1

#define RX_NEXT_BD(x)               (((x) & RX_USABLE_BD_COUNT) == (RX_USABLE_BD_COUNT - 1)) ? (x) + 2 : (x) + 1
#define RX_BD_INDEX(x)              ((x) & RX_USABLE_BD_COUNT)

#define RX_INT_TICKS                18
#define RX_QUICK_CONS_TRIP          6

#endif
