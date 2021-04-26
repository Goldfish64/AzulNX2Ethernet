/*
 *
 * Copyright (c) 2021 Goldfish64
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __HW_BUFFERS_H__
#define __HW_BUFFERS_H__

#define PAGESIZE_16     16
#define PAGESIZE_4K     4096

#define MAX_PACKET_SIZE           (kIOEthernetMaxPacketSize + 4)

//
// Status block structure.
//
typedef struct {
  UInt32 attnBits;
    #define STATUS_ATTN_BITS_LINK_STATE    (1L<<0)
    #define STATUS_ATTN_BITS_TX_SCHEDULER_ABORT  (1L<<1)
    #define STATUS_ATTN_BITS_TX_BD_READ_ABORT  (1L<<2)
    #define STATUS_ATTN_BITS_TX_BD_CACHE_ABORT  (1L<<3)
    #define STATUS_ATTN_BITS_TX_PROCESSOR_ABORT  (1L<<4)
    #define STATUS_ATTN_BITS_TX_DMA_ABORT    (1L<<5)
    #define STATUS_ATTN_BITS_TX_PATCHUP_ABORT  (1L<<6)
    #define STATUS_ATTN_BITS_TX_ASSEMBLER_ABORT  (1L<<7)
    #define STATUS_ATTN_BITS_RX_PARSER_MAC_ABORT  (1L<<8)
    #define STATUS_ATTN_BITS_RX_PARSER_CATCHUP_ABORT (1L<<9)
    #define STATUS_ATTN_BITS_RX_MBUF_ABORT    (1L<<10)
    #define STATUS_ATTN_BITS_RX_LOOKUP_ABORT  (1L<<11)
    #define STATUS_ATTN_BITS_RX_PROCESSOR_ABORT  (1L<<12)
    #define STATUS_ATTN_BITS_RX_V2P_ABORT    (1L<<13)
    #define STATUS_ATTN_BITS_RX_BD_CACHE_ABORT  (1L<<14)
    #define STATUS_ATTN_BITS_RX_DMA_ABORT    (1L<<15)
    #define STATUS_ATTN_BITS_COMPLETION_ABORT  (1L<<16)
    #define STATUS_ATTN_BITS_HOST_COALESCE_ABORT  (1L<<17)
    #define STATUS_ATTN_BITS_MAILBOX_QUEUE_ABORT  (1L<<18)
    #define STATUS_ATTN_BITS_CONTEXT_ABORT    (1L<<19)
    #define STATUS_ATTN_BITS_CMD_SCHEDULER_ABORT  (1L<<20)
    #define STATUS_ATTN_BITS_CMD_PROCESSOR_ABORT  (1L<<21)
    #define STATUS_ATTN_BITS_MGMT_PROCESSOR_ABORT  (1L<<22)
    #define STATUS_ATTN_BITS_MAC_ABORT    (1L<<23)
    #define STATUS_ATTN_BITS_TIMER_ABORT    (1L<<24)
    #define STATUS_ATTN_BITS_DMAE_ABORT    (1L<<25)
    #define STATUS_ATTN_BITS_FLSH_ABORT    (1L<<26)
    #define STATUS_ATTN_BITS_GRC_ABORT    (1L<<27)
    #define STATUS_ATTN_BITS_PARITY_ERROR    (1L<<31)

  UInt32 attnBitsAck;
  UInt16 txConsumer1;
  UInt16 txConsumer0;
  UInt16 txConsumer3;
  UInt16 txConsumer2;
  UInt16 rxConsumer1;
  UInt16 rxConsumer0;
  UInt16 rxConsumer3;
  UInt16 rxConsumer2;
  UInt16 rxConsumer5;
  UInt16 rxConsumer4;
  UInt16 rxConsumer7;
  UInt16 rxConsumer6;
  UInt16 rxConsumer9;
  UInt16 rxConsumer8;
  UInt16 rxConsumer11;
  UInt16 rxConsumer10;
  UInt16 rxConsumer13;
  UInt16 rxConsumer12;
  UInt16 rxConsumer15;
  UInt16 rxConsumer14;
  UInt16 cmdConsumer;
  UInt16 completionProducer;
  UInt16 unused;
  UInt16 index;
} status_block_t;

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
