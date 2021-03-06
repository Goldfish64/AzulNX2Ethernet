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

#include "AzulNX2Ethernet.h"

/*bool AzulNX2Ethernet::allocateTxRxBuffers() {
  
}*/

void AzulNX2Ethernet::initTxRxRegs() {
  UInt32 reg;
  
  //
  // Program TX buffer page size and max BDs read to 0x40.
  //
  reg = readReg32(NX2_TBDR_CONFIG);
  reg &= ~NX2_TBDR_CONFIG_PAGE_SIZE;
  reg |= (TX_PAGE_BITS - 8) << 24 | 0x40;
  writeReg32(NX2_TBDR_CONFIG, reg);
  DBGLOG("TBDR register configured to 0x%X", reg);
  
  //
  // Configure TX completion interrupt thresholds.
  //
  writeReg32(NX2_HC_TX_TICKS, (TX_INT_TICKS << 16) | TX_INT_TICKS);
  writeReg32(NX2_HC_TX_QUICK_CONS_TRIP, (TX_QUICK_CONS_TRIP << 16) | TX_QUICK_CONS_TRIP);
  
  //
  // Program RX page size.
  //
  writeReg32(NX2_RV2P_CONFIG, (RX_PAGE_BITS - 8) << 24);
  writeReg32(NX2_MQ_MAP_L2_5, readReg32(NX2_MQ_MAP_L2_5) | NX2_MQ_MAP_L2_5_ARM);
  
  //
  // Configure RX completion interrupt thresholds.
  //
  writeReg32(NX2_HC_RX_TICKS, (RX_INT_TICKS << 16) | RX_INT_TICKS);
  writeReg32(NX2_HC_RX_QUICK_CONS_TRIP, (RX_QUICK_CONS_TRIP << 16) | RX_QUICK_CONS_TRIP);
}

bool AzulNX2Ethernet::initTxRing() {
  tx_bd_t *txBdLast;
  
  //
  // Reset transmit indexes and allocation stats.
  //
  txProd            = 0;
  txCons            = 0;
  txProdBufferSize  = 0;
  txFreeDescriptors = TX_USABLE_BD_COUNT - 1;
  
  //
  // Initialize transmit chain.
  // The NetXtreme II supports multiple pages each having multiple buffer descriptor entries.
  // This driver uses a single page for all BD entries.
  //
  // The final buffer descriptor is a pointer to the start of the chain.
  //
  txChain = (tx_bd_t*) txBuffer.buffer;
  
  txBdLast         = &txChain[TX_USABLE_BD_COUNT];
  txBdLast->addrHi = ADDR_HI(txBuffer.physAddr);
  txBdLast->addrLo = ADDR_LO(txBuffer.physAddr);
  
  //
  // Initialize context block for L2 transmit chain.
  //
  // Context block is set to handle L2 transmit connections.
  // Address points to first BD in the transmit chain.
  //
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TYPE_XI, NX2_L2CTX_TX_TYPE_TYPE_L2_XI | NX2_L2CTX_TX_TYPE_SIZE_L2_XI);
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_CMD_TYPE_XI, NX2_L2CTX_TX_CMD_TYPE_TYPE_L2_XI | (8 << 16));
    
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_HI_XI, ADDR_HI(txBuffer.physAddr));
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_LO_XI, ADDR_LO(txBuffer.physAddr));
  } else {
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TYPE, NX2_L2CTX_TX_TYPE_TYPE_L2 | NX2_L2CTX_TX_TYPE_SIZE_L2);
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_CMD_TYPE, NX2_L2CTX_TX_CMD_TYPE_TYPE_L2 | (8 << 16));
    
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_HI, ADDR_HI(txBuffer.physAddr));
    writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_LO, ADDR_LO(txBuffer.physAddr));
  }
  
  DBGLOG("TX buffer configured at phys 0x%X size 0x%X (%u usable BDs)", txBuffer.physAddr, TX_PAGE_SIZE, TX_USABLE_BD_COUNT - 1);
  return true;
}

void AzulNX2Ethernet::freeTxRing() {
  // TODO:
}

UInt16 AzulNX2Ethernet::readTxCons() { // TODO: make inline
  UInt16 cons = statusBlock->txConsumer0;
  if ((cons & TX_USABLE_BD_COUNT) == TX_USABLE_BD_COUNT) {
    cons++;
  }
  
  return cons;
}

UInt32 AzulNX2Ethernet::sendTxPacket(mbuf_t packet) {
  IOPhysicalSegment   segments[TX_MAX_SEG_COUNT];
  UInt32              segmentCount;
  UInt32              bdChecksumFlags;
  UInt16              bdFlags = 0;
  UInt16              bdVlanTag = 0;
  
  UInt16              txIndex = 0;
  
  if (txFreeDescriptors == 0) {
    DBGLOG("No free TX BDs are currently available!");
    return kIOReturnOutputStall;
  }
  
  //
  // Get physical segments of outgoing packet, and ensure it can fit into the current available BDs.
  //
  segmentCount = txCursor->getPhysicalSegmentsWithCoalesce(packet, segments, TX_MAX_SEG_COUNT);
  if (segmentCount == 0) {
    freePacket(packet);
    DBGLOG("Failed to get outgoing packet segments");
    return kIOReturnOutputDropped;
  }
  
  if (segmentCount < txFreeDescriptors) {
    //
    // Add applicable checksum offload and VLAN tag flags.
    // First segment also gets a start flag.
    //
    getChecksumDemand(packet, kChecksumFamilyTCPIP, &bdChecksumFlags);
    if (bdChecksumFlags & kChecksumIP) {
      bdFlags |= TX_BD_FLAGS_IP_CKSUM;
    }
    if (bdChecksumFlags & (kChecksumTCP | kChecksumUDP)) {
      bdFlags |= TX_BD_FLAGS_TCP_UDP_CKSUM;
    }

    if (getVlanTagDemand(packet, (UInt32 *)&bdVlanTag)) {
      bdFlags |= TX_BD_FLAGS_VLAN_TAG;
    }
    bdFlags |= TX_BD_FLAGS_START;
    
    //
    // Fill BDs with packet segments.
    //
    for (int i = 0; i < segmentCount; i++) {
      //
      // Hardware maintains a separate index from the driver.
      // The hardware index continues to increment until it rolls over.
      //
      txIndex = TX_BD_INDEX(txProd);
      
      //
      // Add end flag if final segment.
      //
      if (i == segmentCount - 1) {
        bdFlags |= TX_BD_FLAGS_END;
      }
      
      txChain[txIndex].addrHi   = ADDR_HI(segments[i].location);
      txChain[txIndex].addrLo   = ADDR_LO(segments[i].location);
      txChain[txIndex].length   = (UInt32) segments[i].length;
      txChain[txIndex].flags    = bdFlags;
      txChain[txIndex].vlanTag  = bdVlanTag;
      
      //
      // Next BD will normally be +1, but the final BD is reserved to be a pointer to the start of the chain.
      //
      txProdBufferSize += txChain[txIndex].length;
      txProd            = TX_NEXT_BD(txProd);
    }
    
    //
    // Packet is stored for freeing on completion later on.
    // This is always the final BD used for the packet.
    //
    txPackets[txIndex] = packet;
    txFreeDescriptors -= segmentCount;
    
    //
    // Notify hardware of new TX BDs.
    //
    writeReg16(MB_GET_CID_ADDR(TX_CID) +
               NX2_L2MQ_TX_HOST_BIDX, txProd);
    writeReg32(MB_GET_CID_ADDR(TX_CID) +
               NX2_L2MQ_TX_HOST_BSEQ, txProdBufferSize);
    
    //DBGLOG("Sent packet of %u bytes, current TX BD %u (actual %u)", mbuf_pkthdr_len(packet), txProd, TX_BD_INDEX(txProd));
    return kIOReturnOutputSuccess;
  }
  
  DBGLOG("Not enough free TX BDs are currently available!");
  return kIOReturnOutputStall;
}

void AzulNX2Ethernet::handleTxInterrupt(UInt16 txConsNew) {
  UInt16 txIndex;
  
  //
  // Free any newly completed packets.
  //
  while (txCons != txConsNew) {
    txIndex = TX_BD_INDEX(txCons);
    
    if (txPackets[txIndex] != NULL) {
      freePacket(txPackets[txIndex]);
      txPackets[txIndex] = NULL;
    }
    
    txFreeDescriptors++;
    txCons = TX_NEXT_BD(txCons);
  }
  
  txQueue->service(IOBasicOutputQueue::kServiceAsync);
}

bool AzulNX2Ethernet::initRxRing() {
  rx_bd_t           *rxBdLast;
  
  //
  // Reset transmit indexes and allocation stats.
  //
  rxProd            = 0;
  rxCons            = 0;
  rxProdBufferSize  = 0;
  rxFreeDescriptors = 0;
  
  //
  // Initialize receive chain.
  // The NetXtreme II supports multiple pages each having multiple buffer descriptor entries.
  // This driver uses a single page for all BD entries.
  //
  // The final buffer descriptor is a pointer to the start of the chain.
  //
  rxChain = (rx_bd_t*) rxBuffer.buffer;
  
  rxBdLast         = &rxChain[RX_USABLE_BD_COUNT];
  rxBdLast->addrHi = ADDR_HI(rxBuffer.physAddr);
  rxBdLast->addrLo = ADDR_LO(rxBuffer.physAddr);
  
  //
  // Allocate packets in RX chain, skipping already allocated packets.
  //
  for (UInt16 i = 0; i < RX_USABLE_BD_COUNT; i++) {
    if (!initRxDescriptor(i, false)) {
      return false;
    }
  }
  
  //
  // Initialize context block for L2 receive chain.
  //
  // Context block is set to handle L2 receive connections.
  // Address points to first BD in the receive chain.
  //
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_CTX_TYPE,
                 NX2_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE | NX2_L2CTX_RX_CTX_TYPE_SIZE_L2 | (0x02 << NX2_L2CTX_RX_BD_PRE_READ_SHIFT));
  
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_HI, ADDR_HI(rxBuffer.physAddr));
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_LO, ADDR_LO(rxBuffer.physAddr));
  
  writeReg16(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BDIDX, rxProd);
  writeReg32(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BSEQ, rxProdBufferSize);
  
  DBGLOG("RX buffer configured at phys 0x%X size 0x%X (%u usable BDs)", rxBuffer.physAddr, RX_PAGE_SIZE, RX_USABLE_BD_COUNT - 1);
  return true;
}

bool AzulNX2Ethernet::initRxDescriptor(UInt16 index, bool forceAllocate) {
  IOPhysicalSegment segment;
  UInt32            segmentCount;
  
  //
  // Allocate and configure packet.
  //
  if (rxPackets[index] == NULL || forceAllocate) {
    rxPackets[index] = allocatePacket(MAX_PACKET_SIZE);
    if (rxPackets[index] == NULL) {
      return false;
    }
  }
  
  segmentCount = rxCursor->getPhysicalSegmentsWithCoalesce(rxPackets[index], &segment, RX_MAX_SEG_COUNT);
  if (segmentCount != RX_MAX_SEG_COUNT) {
    return false;
  }
  
  rxChain[index].addrHi   = ADDR_HI(segment.location);
  rxChain[index].addrLo   = ADDR_LO(segment.location);
  rxChain[index].flags    = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
  rxChain[index].length   = (UInt32)segment.length;
  
  rxProdBufferSize   += rxChain[index].length;
  rxProd              = RX_NEXT_BD(rxProd);
  return true;
}

void AzulNX2Ethernet::freeRxRing() {
  //
  // Free any allocated packets.
  //
  for (int i = 0; i < RX_USABLE_BD_COUNT; i++) {
    if (rxPackets[i] == NULL) {
      continue;
    }
    
    freePacket(rxPackets[i]);
  }
}

UInt16 AzulNX2Ethernet::readRxCons() { // TODO: make inline
  UInt16 cons = statusBlock->rxConsumer0;
  if ((cons & RX_USABLE_BD_COUNT) == RX_USABLE_BD_COUNT) {
    cons++;
  }
  
  return cons;
}

void AzulNX2Ethernet::handleRxInterrupt(UInt16 rxConsNew) {
  UInt16                rxIndex;
  mbuf_t                inputPacket;

  rx_l2_header_t        *l2Header;
  UInt16                packetLength;
  
  UInt32                checksumValidMask = 0;
  
  //
  // Process any newly received packets.
  //
  while (rxCons != rxConsNew) {
    rxIndex = RX_BD_INDEX(rxCons);
    rxCons  = RX_NEXT_BD(rxCons);
    
    //
    // Incoming packets have a header structure in front of the actual packet, plus two bytes.
    //
    inputPacket = rxPackets[rxIndex];
    l2Header = (rx_l2_header_t*) mbuf_data(inputPacket);
    packetLength = l2Header->packetLength - kIOEthernetCRCSize;
    
    //
    // Don't send garbage to the OS.
    //
    if (packetLength > MAX_PACKET_SIZE || l2Header->errors != 0) {
      DBGLOG("RX error start len %u sts %u err %u idx %u", packetLength, l2Header->status, l2Header->errors, rxIndex);
      freePacket(inputPacket);
      initRxDescriptor(rxIndex, true);
      continue;
    }
    
    if (l2Header->status & L2_FHDR_STATUS_IP_DATAGRAM) {
      checksumValidMask |= kChecksumIP;
    }
    if (l2Header->status & L2_FHDR_STATUS_TCP_SEGMENT) {
      checksumValidMask |= kChecksumTCP;
    }
    if (l2Header->status & L2_FHDR_STATUS_UDP_DATAGRAM) {
      checksumValidMask |= kChecksumUDP;
    }
    
    //
    // Trim off front header and rear ethernet CRC.
    //
    mbuf_adj(inputPacket, sizeof (rx_l2_header_t) + RX_HEADER_PAD);
    mbuf_adj(inputPacket, -kIOEthernetCRCSize);
    
    //
    // Submit packet and prepare the descriptor for the next use.
    //
    setChecksumResult(inputPacket, kChecksumFamilyTCPIP, (kChecksumIP | kChecksumTCP | kChecksumUDP), checksumValidMask);
    ethInterface->inputPacket(inputPacket, packetLength, IOEthernetInterface::kInputOptionQueuePacket);
    
    initRxDescriptor(rxIndex, true);
  }
  
  writeReg16(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BDIDX, rxProd);
  writeReg32(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BSEQ, rxProdBufferSize);
  
  //
  // When using a queued input method (kInputOptionQueuePacket in inputPacket),
  // the queue must be flushed at the end of the interrupt handler.
  //
  ethInterface->flushInputQueue();
}

void AzulNX2Ethernet::setRxMode(bool promiscuous) {
  UInt32 sortMode = 1 | NX2_RPM_SORT_USER0_BC_EN;
  
  //
  // RX mode defaults to normal traffic sorting only.
  //
  rxMode = NX2_EMAC_RX_MODE_SORT_MODE;
  
  if (promiscuous) {
    rxMode |= NX2_EMAC_RX_MODE_PROMISCUOUS;
    sortMode  |= NX2_RPM_SORT_USER0_PROM_EN;
    
    DBGLOG("Promiscuous mode will be enabled");
  }
  
  //
  // Set RX and sort mode.
  //
  DBGLOG("Setting RX mode 0x%X, sort mode 0x%X", rxMode, sortMode);
  writeReg32(NX2_EMAC_RX_MODE, rxMode);
  
  writeReg32(NX2_RPM_SORT_USER0, 0);
  writeReg32(NX2_RPM_SORT_USER0, sortMode);
  writeReg32(NX2_RPM_SORT_USER0, sortMode | NX2_RPM_SORT_USER0_ENA);
}

void AzulNX2Ethernet::setMacAddress() {
  //
  // Program MAC address filtering for incoming packets.
  //
  writeReg32(NX2_EMAC_MAC_MATCH0,
             (ethAddress.bytes[0] << 8) | ethAddress.bytes[1]);
  writeReg32(NX2_EMAC_MAC_MATCH1,
             (ethAddress.bytes[2] << 24) | (ethAddress.bytes[3] << 16) |
             (ethAddress.bytes[4] << 8)  | ethAddress.bytes[5]);
}
