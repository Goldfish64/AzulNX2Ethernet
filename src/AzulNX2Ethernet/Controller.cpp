#include "AzulNX2Ethernet.h"


bool AzulNX2Ethernet::prepareController() {
  UInt32 reg;
  
  //
  // Get chip ID. Non-production revisions are not supported.
  //
  chipId = readReg32(NX2_MISC_ID);
  switch (NX2_CHIP_ID) {
    case NX2_CHIP_ID_5706_A0:
    case NX2_CHIP_ID_5706_A1:
    case NX2_CHIP_ID_5708_A0:
    case NX2_CHIP_ID_5708_B0:
    case NX2_CHIP_ID_5709_A0:
    case NX2_CHIP_ID_5709_B0:
    case NX2_CHIP_ID_5709_B1:
    case NX2_CHIP_ID_5709_B2:
      SYSLOG("Unsupported controller revision 0x%X", NX2_CHIP_ID);
      return false;
  }
  

  
  pciNub->setBusMasterEnable(true);
  pciNub->setMemoryEnable(true);
  
  pciVendorId    = readReg16(kIOPCIConfigVendorID);
  pciDeviceId    = readReg16(kIOPCIConfigDeviceID);
  pciSubVendorId = readReg16(kIOPCIConfigSubSystemVendorID);
  pciSubDeviceId = readReg16(kIOPCIConfigSubSystemID);
  
  char modelName[64];
  snprintf(modelName, sizeof (modelName), "%s %s", getDeviceVendor(), getDeviceModel());
  
  SYSLOG("Controller is %s", modelName);
  
  if (pciNub->getFunctionNumber() > 0)
    return false;

  setProperty("model", modelName);
  
  //
  // Enable the REG_WINDOW register for indirect reads/writes, and enable mailbox word swapping.
  // The NetXtreme II is a big-endian system, and we are little-endian.
  //
  pciNub->configWrite32(NX2_PCICFG_MISC_CONFIG,
                        NX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
                        NX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP);
  
  //
  // Determine base address for shared memory region. Newer versions of bootcode use a signature
  // and a dynamic offset, where older versions used a fixed address.
  //
  // For dual port devices, the offset for the secondary port is different than the primary one.
  //
  reg = readRegIndr32(NX2_SHM_HDR_SIGNATURE);
  if ((reg & NX2_SHM_HDR_SIGNATURE_SIG_MASK) == NX2_SHM_HDR_SIGNATURE_SIG) {
    shMemBase = readRegIndr32(NX2_SHM_HDR_ADDR_0 + (pciNub->getFunctionNumber() << 2));
  } else {
    shMemBase = HOST_VIEW_SHMEM_BASE;
  }
  
  DBGLOG("Shared memory is at 0x%08X", shMemBase);
  
  //
  // Allocate status, statistics, transmit, and receive buffers.
  //
  if (!allocDmaBuffer(&statusBuffer, 0x1000, PAGESIZE_16)) {
    return false;
  }
  if (!allocDmaBuffer(&statsBuffer, 0x1000, PAGESIZE_16)) {
    freeDmaBuffer(&statusBuffer);
    return false;
  }
  if (!allocDmaBuffer(&txBuffer, TX_PAGE_SIZE, PAGESIZE_4K)) {
    freeDmaBuffer(&statusBuffer);
    freeDmaBuffer(&statsBuffer);
    return false;
  }
  if (!allocDmaBuffer(&rxBuffer, RX_PAGE_SIZE, PAGESIZE_4K)) {
    freeDmaBuffer(&statusBuffer);
    freeDmaBuffer(&statsBuffer);
    freeDmaBuffer(&txBuffer);
    return false;
  }
  
  //
  // 5709 and 5716 do not have on-chip context memory.
  // It is required to allocate host memory for this purpose.
  //
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    if (!allocDmaBuffer(&contextBuffer, CTX_PAGE_SIZE * CTX_PAGE_CNT, PAGESIZE_4K)) {
      freeDmaBuffer(&statusBuffer);
      freeDmaBuffer(&statsBuffer);
      freeDmaBuffer(&txBuffer);
      freeDmaBuffer(&rxBuffer);
      return false;
    }
  }
  
  return true;
}

bool AzulNX2Ethernet::resetController(UInt32 resetCode) {
  bool success  = false;
  UInt32 reg    = 0;
  
  //
  // Ensure all pending PCI transactions are completed.
  //
  writeReg32(NX2_MISC_ENABLE_CLR_BITS,
             NX2_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE |
             NX2_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE |
             NX2_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE |
             NX2_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE);
  readReg32(NX2_MISC_ENABLE_CLR_BITS);
  IODelay(10);
  
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    reg = readReg32(NX2_MISC_NEW_CORE_CTL);
    reg &= ~NX2_MISC_NEW_CORE_CTL_DMA_ENABLE;
    writeReg32(NX2_MISC_NEW_CORE_CTL, reg);
  }
  
  do {
    //
    // Prepare firmware for software reset.
    //
    if (!firmwareSync(NX2_DRV_MSG_DATA_WAIT0 | resetCode)) {
      IOLog("AzulNX2Ethernet: reset signal timeout!\n");
      break;
    }
    writeShMem32(NX2_DRV_RESET_SIGNATURE, NX2_DRV_RESET_SIGNATURE_MAGIC);
    readReg32(NX2_MISC_ID);
    
    //
    // Reset controller. 5709 and 5716 use a different reset method.
    //
    // 5709/5716 requires 500 uS to reset.
    // Others require 30 uS.
    //
    if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
      writeReg32(NX2_MISC_COMMAND, NX2_MISC_COMMAND_SW_RESET);
      readReg32(NX2_MISC_COMMAND);
      IOSleep(50);
      
      pciNub->configWrite32(NX2_PCICFG_MISC_CONFIG,
                            NX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
                            NX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP);
    } else {
      writeReg32(NX2_PCICFG_MISC_CONFIG,
                 NX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
                 NX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
                 NX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP);
      
      for (int i = 0; i < 10; i++) {
        reg = readReg32(NX2_PCICFG_MISC_CONFIG);
        if ((reg & (NX2_PCICFG_MISC_CONFIG_CORE_RST_REQ | NX2_PCICFG_MISC_CONFIG_CORE_RST_BSY)) == 0) {
          break;
        }
        
        IODelay(10);
      }
      
      //
      // Verify reset completed.
      //
      if ((reg & (NX2_PCICFG_MISC_CONFIG_CORE_RST_REQ | NX2_PCICFG_MISC_CONFIG_CORE_RST_BSY))) {
        IOLog("AzulNX2Ethernet: reset timeout!\n");
        break;
      }
    }
    
    //
    // Ensure byte swapping is configured.
    //
    reg = readReg32(NX2_PCI_SWAP_DIAG0);
    if (reg != 0x01020304) {
      IOLog("AzulNX2Ethernet: byte swapping is invalid!\n");
      break;;
    }
    
    //
    // Wait for firmware to initialize again.
    //
    IOLog("AzulNX2Ethernet: NX2_DRV_MSG_DATA_WAIT1\n");
    if (!firmwareSync(NX2_DRV_MSG_DATA_WAIT1 | resetCode)) {
      IOLog("AzulNX2Ethernet: failed 33\n");
      break;
    }
    
    success = true;
    IOLog("AzulNX2Ethernet: reset completed\n");
  } while (false);
  
  //
  // TODO: Restore EMAC for ASF and IPMI.
  //
  
  

  return success;
}

bool AzulNX2Ethernet::initControllerChip() {
  UInt32 reg = 0;
  
  disableInterrupts();
  
  // disable heartbeat
  writeShMem32(NX2_DRV_PULSE_MB, NX2_DRV_MSG_DATA_PULSE_CODE_ALWAYS_ALIVE);
  readShMem32(NX2_DRV_PULSE_MB);
  
  //
  // Configure DMA and byte-swapping.
  // The NetXtreme II is big endian and we are little endian.
  //
  reg = NX2_DMA_CONFIG_DATA_BYTE_SWAP |
        NX2_DMA_CONFIG_DATA_WORD_SWAP |
        NX2_DMA_CONFIG_CNTL_WORD_SWAP |
        DMA_READ_CHANS << 12 |
        DMA_WRITE_CHANS << 16;
  
  reg |= (0x2 << 20) | NX2_DMA_CONFIG_CNTL_PCI_COMP_DLY;
  
  // TODO: Add PCI-X and 5706 errata things here.
  
  writeReg32(NX2_DMA_CONFIG, reg);
  
  //
  // Enable RX V2P and context state machines.
  //
  writeReg32(NX2_MISC_ENABLE_SET_BITS,
             NX2_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE |
             NX2_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE |
             NX2_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE);
  
  //
  // Initialize context and start CPUs.
  //
  if (!initContext()) {
    return false;
  }
  initCpus();
  
  writeReg32(NX2_EMAC_ATTENTION_ENA, NX2_EMAC_ATTENTION_ENA_LINK);
  
  
  reg = readReg32(NX2_MQ_CONFIG);
  reg &= ~NX2_MQ_CONFIG_KNL_BYP_BLK_SIZE;
  reg |= NX2_MQ_CONFIG_KNL_BYP_BLK_SIZE_256;
  
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    reg |= NX2_MQ_CONFIG_BIN_MQ_MODE;
  }
  writeReg32(NX2_MQ_CONFIG, reg);
  
  reg = 0x10000 + (MAX_CID_CNT * MB_KERNEL_CTX_SIZE);
  writeReg32(NX2_MQ_KNL_BYP_WIND_START, reg);
  writeReg32(NX2_MQ_KNL_WIND_END, reg);
  

  
  initTxRxRegs();
  
  writeReg32(NX2_EMAC_RX_MTU_SIZE, MAX_PACKET_SIZE);
  
  fetchMacAddress();
  
  writeReg32(NX2_HC_STATUS_ADDR_L, (statusBuffer.physAddr & 0xFFFFFFFF));
  writeReg32(NX2_HC_STATUS_ADDR_H, statusBuffer.physAddr >> 32);
  statusBlock = (status_block_t*)statusBuffer.buffer;
  
  writeReg32(NX2_HC_STATISTICS_ADDR_L, (statsBuffer.physAddr & 0xFFFFFFFF));
  writeReg32(NX2_HC_STATISTICS_ADDR_H, statsBuffer.physAddr >> 32);

  
  writeReg32(NX2_HC_STAT_COLLECT_TICKS, 0xbb8);
  
  writeReg32(NX2_HC_CONFIG, NX2_HC_CONFIG_RX_TMR_MODE | NX2_HC_CONFIG_TX_TMR_MODE |
             NX2_HC_CONFIG_COLLECT_STATS);
  
  reg = (ethAddress.bytes[0] << 8) | ethAddress.bytes[1];
  writeReg32(NX2_EMAC_MAC_MATCH0, reg);
  reg = (ethAddress.bytes[2] << 24) | (ethAddress.bytes[3] << 16) |
  (ethAddress.bytes[4] << 8) | ethAddress.bytes[5];
  writeReg32(NX2_EMAC_MAC_MATCH1, reg);
  
  writeReg32(NX2_HC_COMMAND, NX2_HC_COMMAND_CLR_STAT_NOW);
  writeReg32(NX2_HC_ATTN_BITS_ENABLE, 0x1);//0xFFFFFFFF);//0x1 | (1<<18));
#define NX2_RXP_PM_CTRL      0x0e00d0
  /* Set the perfect match control register to default. */
  writeRegIndr32(NX2_RXP_PM_CTRL, 0);
  
  //writeReg32(NX2_EMAC_RX_MODE, NX2_RPM_SORT_USER0_PROM_EN | NX2_EMAC_RX_MODE_PROMISCUOUS);
  writeReg32(NX2_EMAC_RX_MODE, NX2_EMAC_RX_MODE_SORT_MODE);
  writeReg32(NX2_RPM_SORT_USER0, 0);
  writeReg32(NX2_RPM_SORT_USER0, 1 | NX2_RPM_SORT_USER0_BC_EN);
  writeReg32(NX2_RPM_SORT_USER0, 1 | NX2_RPM_SORT_USER0_BC_EN | NX2_RPM_SORT_USER0_ENA);
  
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    reg = readReg32(NX2_MISC_NEW_CORE_CTL);
    reg |= NX2_MISC_NEW_CORE_CTL_DMA_ENABLE;
    writeReg32(NX2_MISC_NEW_CORE_CTL, reg);
  }
  

  
  firmwareSync(NX2_DRV_MSG_DATA_WAIT2 | NX2_DRV_MSG_CODE_RESET);
  
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    writeReg32(NX2_MISC_ENABLE_SET_BITS, NX2_MISC_ENABLE_DEFAULT_XI);
  }
  readReg32(NX2_MISC_ENABLE_SET_BITS);
  IODelay(20);
  
 // initCpuRxp();
  
  initTxRing();
  initRxRing();
  

  /*
   * Page count must remain a power of 2 for all
   * of the math to work correctly.
   */
  #define DEFAULT_RX_PAGES    2
  #define MAX_RX_PAGES      8
  #define TOTAL_RX_BD_PER_PAGE  (0x1000 / sizeof(struct rx_bd))
  #define USABLE_RX_BD_PER_PAGE  (TOTAL_RX_BD_PER_PAGE - 1)
  #define MAX_RX_BD_AVAIL    (MAX_RX_PAGES * TOTAL_RX_BD_PER_PAGE)
  #define TOTAL_RX_BD_ALLOC    (TOTAL_RX_BD_PER_PAGE * sc->rx_pages)
  #define USABLE_RX_BD_ALLOC    (USABLE_RX_BD_PER_PAGE * sc->rx_pages)
  #define MAX_RX_BD_ALLOC    (TOTAL_RX_BD_ALLOC - 1)

  /* Advance to the next rx_bd, skipping any next page pointers. */
  #define NEXT_RX_BD(x) (((x) & USABLE_RX_BD_PER_PAGE) ==  \
      (USABLE_RX_BD_PER_PAGE - 1)) ? (x) + 2 : (x) + 1

  #define RX_CHAIN_IDX(x) ((x) & MAX_RX_BD_ALLOC)
  
  
 /* rx_bd *rd = (rx_bd*)receiveBuffer.buffer;
  rx_bd *rd2 = &rd[255];
  
  IOPhysicalSegment    segment;
  UInt32               count;
  
  UInt32 rxBseq;
  
  rxCursor = IOMbufNaturalMemoryCursor::withSpecification(kIOEthernetMaxPacketSize +  4,
                                                          1);
  
  UInt32 rxProd = 0;
  
  rxBseq = 0;
  for (int i = 0; i < 255; i++) {
    rxPackets[i] = allocatePacket(kIOEthernetMaxPacketSize + 4);
    
    count = rxCursor->getPhysicalSegmentsWithCoalesce(rxPackets[i], &segment, 1);
    if (count) {
      rd->rx_bd_haddr_hi = segment.location >> 32;
      rd->rx_bd_haddr_lo = segment.location & 0xFFFFFFFF;
      rd->rx_bd_flags = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
      rd->rx_bd_len = segment.length;
      rxBseq += segment.length;
      rd++;
      
      rxProd = NEXT_RX_BD(rxProd);
      SYSLOG("Added packet %u %u %X %X", i, rxProd, segment.location, segment.length);
      
    }
  }
  
  reg = NX2_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE |
  NX2_L2CTX_RX_CTX_TYPE_SIZE_L2 |
  (0x02 << NX2_L2CTX_RX_BD_PRE_READ_SHIFT);
  
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_CTX_TYPE, reg);
  
  writeReg32(NX2_MQ_MAP_L2_5, readReg32(NX2_MQ_MAP_L2_5) | NX2_MQ_MAP_L2_5_ARM);
  
  reg = receiveBuffer.physAddr >> 32;
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_HI, reg);
  reg = receiveBuffer.physAddr & 0xFFFFFFFF;
  writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_LO, reg);
  
  rd2->rx_bd_haddr_hi = receiveBuffer.physAddr >> 32;
  rd2->rx_bd_haddr_lo = receiveBuffer.physAddr & 0xFFFFFFFF;
  
  writeReg16(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BDIDX, rxProd);
  writeReg32(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BSEQ, rxBseq);*/
  

  

 /* tx_bd *bd = (tx_bd*)transmitBuffer.buffer;
  tx_bd *bd2 = &bd[255];
  
  bd2->tx_bd_haddr_hi = transmitBuffer.physAddr >> 32;
  bd2->tx_bd_haddr_lo = transmitBuffer.physAddr & 0xFFFFFFFF;
  
  reg = NX2_L2CTX_TX_TYPE_TYPE_L2_XI | NX2_L2CTX_TX_TYPE_SIZE_L2_XI;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TYPE_XI, reg);
  reg = NX2_L2CTX_TX_CMD_TYPE_TYPE_L2_XI | (8 << 16);
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_CMD_TYPE_XI, reg);
  
  reg = transmitBuffer.physAddr >> 32;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_HI_XI, reg);
  reg = transmitBuffer.physAddr & 0xFFFFFFFF;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_LO_XI, reg);*/
  

  
  enableInterrupts(true);
  
  probePHY();
  
  resetPHY();
  enablePHYAutoMDIX();
 // enablePHYLoopback();
  enablePHYAutoNegotiation();
  
  
  enableInterrupts(true); // Required after reset due to 10/100 issues?
  
  //txIndex = 0;
  //txSeq = 0;
  
  txCursor = IOMbufNaturalMemoryCursor::withSpecification(MAX_PACKET_SIZE,
                                                              TX_MAX_SEG_COUNT);
  
  isEnabled = true;
  return true;
}

bool AzulNX2Ethernet::initTransmitBuffers() {
  return 0;
}
