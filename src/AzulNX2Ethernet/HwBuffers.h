#ifndef __HW_BUFFERS_H__
#define __HW_BUFFERS_H__

#define PAGESIZE_16     16
#define PAGESIZE_4K     4096

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

#define TX_PAGE_SIZE                0x4000
#define TX_PAGE_BITS                14
#define TX_MAX_BD_COUNT             (TX_PAGE_SIZE / sizeof (tx_bd_t))
#define TX_USABLE_BD_COUNT          (TX_MAX_BD_COUNT - 1)
#define TX_MAX_SEG_COUNT            512

#define TX_NEXT_BD(x)               (((x) & TX_USABLE_BD_COUNT) == (TX_USABLE_BD_COUNT - 1)) ? (x) + 2 : (x) + 1
#define TX_BD_INDEX(x)              ((x) & TX_USABLE_BD_COUNT)

#define TX_INT_TICKS                80
#define TX_QUICK_CONS_TRIP          20

#endif
