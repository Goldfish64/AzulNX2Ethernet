#include "AzulNX2Ethernet.h"

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
  memset(txPackets, 0, sizeof (txPackets));
  
  //
  // Initialize transmit chain.
  // The NetXtreme II supports multiple pages each having multiple buffer descriptor entries.
  // This driver uses a single page for all BD entries.
  //
  // The final buffer descriptor is a pointer to the start of the chain.
  //
  txChain = (tx_bd_t*) txBuffer.buffer;
  
  txBdLast = &txChain[TX_USABLE_BD_COUNT];
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

UInt16 AzulNX2Ethernet::readTxCons() {
  UInt16 cons = statusBlock->status_tx_quick_consumer_index0;
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
    
    DBGLOG("Sent packet of %u bytes, current TX BD %u (actual %u)", mbuf_pkthdr_len(packet), txProd, TX_BD_INDEX(txProd));
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
  while (txCons <= txConsNew) {
    txIndex = TX_BD_INDEX(txCons);
    
    if (txPackets[txIndex] != NULL) {
      freePacket(txPackets[txIndex]);
      txPackets[txIndex] = NULL;
    }
    
    DBGLOG("TX packet %u (actual %u) is completed", txCons, txIndex);
    txFreeDescriptors++;
    txCons = TX_NEXT_BD(txCons);
  }
  
  txQueue->service(IOBasicOutputQueue::kServiceAsync);
}
