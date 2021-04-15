#include "AzulNX2Ethernet.h"


bool AzulNX2Ethernet::prepareController() {
  UInt32 reg;
  
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
  
  IOLog("AzulNX2Ethernet: Shared memory is at 0x%08X\n", shMemBase);
  
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
  
  // TODO: disable interrupts here.
  
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
  
  
  return true;
}
