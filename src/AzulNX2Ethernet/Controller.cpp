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
  setProperty("model", modelName);
  SYSLOG("Controller is %s, revision %c%d", modelName, ((NX2_CHIP_ID & 0xF000) >> 12) + 'A', (NX2_CHIP_ID & 0x0FF0) >> 4);
  
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
  // Create memory cursors for TX and RX.
  //
  txCursor = IOMbufNaturalMemoryCursor::withSpecification(MAX_PACKET_SIZE, TX_MAX_SEG_COUNT);
  rxCursor = IOMbufNaturalMemoryCursor::withSpecification(MAX_PACKET_SIZE, RX_MAX_SEG_COUNT);
  if (txCursor == NULL || rxCursor == NULL) {
    return false;
  }
  
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
  
  //
  // Set status and statistics block addresses.
  //
  writeReg32(NX2_HC_STATUS_ADDR_H, ADDR_HI(statusBuffer.physAddr));
  writeReg32(NX2_HC_STATUS_ADDR_L, ADDR_LO(statusBuffer.physAddr));
  writeReg32(NX2_HC_STATISTICS_ADDR_H, ADDR_HI(statsBuffer.physAddr));
  writeReg32(NX2_HC_STATISTICS_ADDR_L, ADDR_LO(statsBuffer.physAddr));
  writeReg32(NX2_HC_STAT_COLLECT_TICKS, 0xbb8);
  
  statusBlock = (status_block_t*) statusBuffer.buffer;
  
  writeReg32(NX2_HC_CONFIG, NX2_HC_CONFIG_RX_TMR_MODE | NX2_HC_CONFIG_TX_TMR_MODE |
             NX2_HC_CONFIG_COLLECT_STATS);
  
  setMacAddress();
  
  writeReg32(NX2_HC_COMMAND, NX2_HC_COMMAND_CLR_STAT_NOW);
  writeReg32(NX2_HC_ATTN_BITS_ENABLE, 0x1);//0xFFFFFFFF);//0x1 | (1<<18));
#define NX2_RXP_PM_CTRL      0x0e00d0
  /* Set the perfect match control register to default. */
  writeRegIndr32(NX2_RXP_PM_CTRL, 0);
  
  setRxMode(false);
  
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

  initTxRing();
  initRxRing();
  
  enableInterrupts(true);
  
  probePHY();
  
  resetPHY();
  enablePHYAutoMDIX();
 // enablePHYLoopback();
  enablePHYAutoNegotiation();
  
  
  enableInterrupts(true); // Required after reset due to 10/100 issues?
  
  isEnabled = true;
  return true;
}
