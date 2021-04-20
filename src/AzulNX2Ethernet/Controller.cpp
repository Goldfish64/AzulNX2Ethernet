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
  
  if (pciNub->getFunctionNumber() > 0)
    return false;
  
  pciVendorId    = readReg16(kIOPCIConfigVendorID);
  pciDeviceId    = readReg16(kIOPCIConfigDeviceID);
  pciSubVendorId = readReg16(kIOPCIConfigSubSystemVendorID);
  pciSubDeviceId = readReg16(kIOPCIConfigSubSystemID);
  
  char modelName[64];
  snprintf(modelName, sizeof (modelName), "%s %s", getDeviceVendor(), getDeviceModel());
  
  SYSLOG("Controller is %s", modelName);

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
  
  if (isChip5709) {
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
    if (isChip5709) {
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
  
  allocMemory();
  
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
  
  if (isChip5709) {
    reg |= NX2_MQ_CONFIG_BIN_MQ_MODE;
  }
  writeReg32(NX2_MQ_CONFIG, reg);
  
  reg = 0x10000 + (MAX_CID_CNT * MB_KERNEL_CTX_SIZE);
  writeReg32(NX2_MQ_KNL_BYP_WIND_START, reg);
  writeReg32(NX2_MQ_KNL_WIND_END, reg);
  
  reg = 4 << 24;
  writeReg32(NX2_RV2P_CONFIG, reg);
  
  reg = readReg32(NX2_TBDR_CONFIG);
  reg &= ~NX2_TBDR_CONFIG_PAGE_SIZE;
  reg |= 4 << 24 | 0x40;
  writeReg32(NX2_TBDR_CONFIG, reg);
  
  fetchMacAddress();
  
  writeReg32(NX2_HC_STATUS_ADDR_L, (stsBlockSeg.fIOVMAddr & 0xFFFFFFFF));
  writeReg32(NX2_HC_STATUS_ADDR_H, stsBlockSeg.fIOVMAddr >> 32);
  
  writeReg32(NX2_HC_STATISTICS_ADDR_L, (statsBlockSeg.fIOVMAddr & 0xFFFFFFFF));
  writeReg32(NX2_HC_STATISTICS_ADDR_H, statsBlockSeg.fIOVMAddr >> 32);
  
  writeReg32(NX2_HC_TX_TICKS,
        (1 << 16) | 1);
  
  writeReg32(NX2_HC_STAT_COLLECT_TICKS, 0xbb8);
  
  writeReg32(NX2_HC_CONFIG, NX2_HC_CONFIG_RX_TMR_MODE | NX2_HC_CONFIG_TX_TMR_MODE |
             NX2_HC_CONFIG_COLLECT_STATS);
  
  writeReg32(NX2_HC_COMMAND, NX2_HC_COMMAND_CLR_STAT_NOW);
  writeReg32(NX2_HC_ATTN_BITS_ENABLE, 0x1);//0xFFFFFFFF);//0x1 | (1<<18));
  
  if (isChip5709) {
    reg = readReg32(NX2_MISC_NEW_CORE_CTL);
    reg |= NX2_MISC_NEW_CORE_CTL_DMA_ENABLE;
    writeReg32(NX2_MISC_NEW_CORE_CTL, reg);
  }
  
  firmwareSync(NX2_DRV_MSG_DATA_WAIT2 | NX2_DRV_MSG_CODE_RESET);
  
  if (isChip5709) {
    writeReg32(NX2_MISC_ENABLE_SET_BITS, NX2_MISC_ENABLE_DEFAULT_XI);
  }
  readReg32(NX2_MISC_ENABLE_SET_BITS);
  IODelay(20);
  
  reg = NX2_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE |
  NX2_L2CTX_RX_CTX_TYPE_SIZE_L2 |
  (0x02 << NX2_L2CTX_RX_BD_PRE_READ_SHIFT);
  
 // writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_CTX_TYPE, reg);
  
  writeReg32(NX2_MQ_MAP_L2_5, readReg32(NX2_MQ_MAP_L2_5) | NX2_MQ_MAP_L2_5_ARM);
  
  rx_bd *rd = (rx_bd*)rxBlockData;
  rx_bd *rd2 = &rd[255];
  
  reg = rxBlockSeg.fIOVMAddr >> 32;
 // writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_HI, reg);
  reg = rxBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  //writeContext32(GET_CID_ADDR(RX_CID), NX2_L2CTX_RX_NX_BDHADDR_LO, reg);
  
  rd2->rx_bd_haddr_hi = rxBlockSeg.fIOVMAddr >> 32;
  rd2->rx_bd_haddr_lo = rxBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  
  //writeReg16(MB_GET_CID_ADDR(RX_CID) + NX2_L2MQ_RX_HOST_BDIDX, <#UInt16 value#>)
  

  tx_bd *bd = (tx_bd*)txBlockData;
  tx_bd *bd2 = &bd[255];
  
  bd2->tx_bd_haddr_hi = txBlockSeg.fIOVMAddr >> 32;
  bd2->tx_bd_haddr_lo = txBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  
  reg = NX2_L2CTX_TX_TYPE_TYPE_L2_XI | NX2_L2CTX_TX_TYPE_SIZE_L2_XI;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TYPE_XI, reg);
  reg = NX2_L2CTX_TX_CMD_TYPE_TYPE_L2_XI | (8 << 16);
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_CMD_TYPE_XI, reg);
  
  reg = txBlockSeg.fIOVMAddr >> 32;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_HI_XI, reg);
  reg = txBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  writeContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TBDR_BHADDR_LO_XI, reg);
  

  
  enableInterrupts(true);
  
  probePHY();
  
  resetPHY();
  enablePHYAutoMDIX();
  enablePHYLoopback();
  enablePHYAutoNegotiation();
  
  
  IOSleep(1000);
  
  IOSleep(15000);
  updatePHYMediaState();

  
 // memset(bd, 0, 0x10);
  
  status_block_t *stsBlock = (status_block_t*)stsBlockData;
  IOLog("cleanred\n");
  IOLog("TX STS %X\n", stsBlock->status_tx_quick_consumer_index0);
  
  const char bees[] = "According to all known laws of aviation, there is no way a bee should be able to fly. Its wings are too small to get its fat little body off the ground. The bee, of course, flies anyway because bees don't care what humans think is impossible.";
  
  uint8_t *dd = (uint8_t*)rxBlockData;
  dd[0] = 0xff;
  dd[1] = 0xff;
  dd[2] = 0xff;
  dd[3] = 0xff;
  dd[4] = 0xff;
  dd[5] = 0xff;
  dd[6] = ethAddress.bytes[0];
  dd[7] = ethAddress.bytes[1];
  dd[8] = ethAddress.bytes[2];
  dd[9] = ethAddress.bytes[3];
  dd[10] = ethAddress.bytes[4];
  dd[11] = ethAddress.bytes[5];
  dd[12] = 0x08;
  dd[13] = 0x06;
  dd[14] = 0x00;
  dd[15] = 0x01;
  dd[16] = 0x08;
  dd[17] = 0x00;
  dd[18] = 0x06;
  dd[19] = 0x04;
  dd[20] = 0x00;
  dd[21] = 0x01;
  dd[22] = ethAddress.bytes[0];
  dd[23] = ethAddress.bytes[1];
  dd[24] = ethAddress.bytes[2];
  dd[25] = ethAddress.bytes[3];
  dd[26] = ethAddress.bytes[4];
  dd[27] = ethAddress.bytes[5];
  dd[28] = 0x45;
  dd[29] = 0x45;
  dd[30] = 0x45;
  dd[31] = 0x45;
  dd[32] = 0x00;
  dd[33] = 0x00;
  dd[34] = 0x00;
  dd[35] = 0x00;
  dd[36] = 0x00;
  dd[37] = 0x00;
  dd[38] = 0xc0;
  dd[39] = 0xa8;
  dd[40] = 0x3c;
  dd[41] = 0x33;
  
  memcpy(&dd[42], bees, sizeof (bees));
  
  /*dd[42] = 0x41;
  dd[43] = 0x4E;
  dd[44] = 0x4E;
  dd[45] = 0x45;
  dd[46] = 0x20;
  dd[47] = 0x42;
  dd[48] = 0x4F;
  dd[49] = 0x4F;
  dd[50] = 0x4E;
  dd[51] = 0x43;
  dd[52] = 0x48;
  dd[53] = 0x55;
  dd[54] = 0x59;*/
  
  
  bd->tx_bd_haddr_hi = rxBlockSeg.fIOVMAddr >> 32;
  bd->tx_bd_haddr_lo = rxBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  SYSLOG("BD data is %X, stats data is %X", rxBlockSeg.fIOVMAddr, statsBlockSeg.fIOVMAddr);
  bd->tx_bd_mss_nbytes = 42 + sizeof (bees);
  bd->tx_bd_flags = TX_BD_FLAGS_START | TX_BD_FLAGS_END;
  
  bd++;
  bd->tx_bd_haddr_hi = rxBlockSeg.fIOVMAddr >> 32;
  bd->tx_bd_haddr_lo = rxBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  SYSLOG("BD data is %X, stats data is %X", rxBlockSeg.fIOVMAddr, statsBlockSeg.fIOVMAddr);
  bd->tx_bd_mss_nbytes = 42 + sizeof (bees);
  bd->tx_bd_flags = TX_BD_FLAGS_START | TX_BD_FLAGS_END;
  bd++;
  bd->tx_bd_haddr_hi = rxBlockSeg.fIOVMAddr >> 32;
  bd->tx_bd_haddr_lo = rxBlockSeg.fIOVMAddr & 0xFFFFFFFF;
  SYSLOG("BD data is %X, stats data is %X", rxBlockSeg.fIOVMAddr, statsBlockSeg.fIOVMAddr);
  bd->tx_bd_mss_nbytes = 42 + sizeof (bees);
  bd->tx_bd_flags = TX_BD_FLAGS_START | TX_BD_FLAGS_END;
  
  writeReg16(MB_GET_CID_ADDR(TX_CID) +
             NX2_L2MQ_TX_HOST_BIDX, 3);
  writeReg32(MB_GET_CID_ADDR(TX_CID) +
             NX2_L2MQ_TX_HOST_BSEQ, (42 + sizeof (bees)) * 3);
  
  //writeContext32(MB_GET_CID_ADDR(TX_CID), NX2_L2MQ_TX_HOST_BIDX, 1);
  //writeContext32(MB_GET_CID_ADDR(TX_CID), NX2_L2MQ_TX_HOST_BSEQ, 48);
  
  
 // SYSLOG("%X TX", readReg32(MB_GET_CID_ADDR(TX_CID) +
   //         NX2_L2MQ_TX_HOST_BSEQ));
  
  SYSLOG("CTX %X %X %X", readContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_TYPE_XI), readContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_HOST_BIDX_XI), readContext32(GET_CID_ADDR(TX_CID), NX2_L2CTX_TX_HOST_BSEQ_XI));
  
  IOSleep(1000);
  
  UInt32 *stats = (UInt32*)statsBlockData;
  UInt32 *status = (UInt32*)stsBlockData;

  
  IOLog("TX %X %X %X %X %X\n", stats[0x10], stats[0x14], stats[0x18], stats[0x1c], stats[0x50]);
  IOLog("STS %X %X HCS %X\n", status[0], status[3], readReg32(NX2_HC_STATUS));
  
  IOLog("TX STS %X\n", stsBlock->status_tx_quick_consumer_index0);
  IOLog("RX STS %X\n", stsBlock->status_rx_quick_consumer_index0);
  
  //IOLog("PHY ESR %X\n", readPhyReg32(0x11));
  
  IOLog("MQ cmd %X sts %X badrd %X\n", readReg32(NX2_MQ_COMMAND), readReg32(NX2_MQ_STATUS), readReg32(NX2_MQ_BAD_RD_ADDR));
  
  IOLog("%X %X EMAC OUT OCTETS\n", readReg32(NX2_EMAC_TX_STAT_IFHCOUTOCTETS), readReg32(NX2_EMAC_TX_STAT_IFHCOUTBADOCTETS));
  
  DBGLOG("EMAC reg is %X", readReg32(NX2_EMAC_MODE));
  
  for (int i = 0; i < 54; i++) {
    IOLog("%X", stats[i]);
  }
  
 // SYSLOG("TXP %X %X", readReg32(NX2_TXP_CPU_STATE), readReg32(NX2_TXP_CPU_EVENT_MASK));
  
  
  return true;
}
