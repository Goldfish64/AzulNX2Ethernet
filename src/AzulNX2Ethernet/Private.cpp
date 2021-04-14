#include "AzulNX2Ethernet.h"
#include "Firmware.h"

/**
 Reads the specified register using memory space.
 */
UInt32 AzulNX2Ethernet::readReg32(UInt32 offset) {
  return OSReadLittleInt32(baseAddr, offset);
}

/**
 Reads the specified register using PCI config space.
 */
UInt32 AzulNX2Ethernet::readRegIndr32(UInt32 offset) {
  pciNub->configWrite32(NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  return pciNub->configRead32(NX2_PCICFG_REG_WINDOW);
}

/**
 Reads the specified register from shared memory.
 */
UInt32 AzulNX2Ethernet::readShMem32(UInt32 offset) {
  return readRegIndr32(shMemBase + offset);
}

/**
 Writes the specified register using memory space.
 */
void AzulNX2Ethernet::writeReg32(UInt32 offset, UInt32 value) {
  OSWriteLittleInt32(baseAddr, offset, value);
}

/**
 Writes the specified register using PCI config space.
 */
void AzulNX2Ethernet::writeRegIndr32(UInt32 offset, UInt32 value) {
  pciNub->configWrite32(NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  pciNub->configWrite32(NX2_PCICFG_REG_WINDOW, value);
}

/**
 Writes the specified register from shared memory.
 */
void AzulNX2Ethernet::writeShMem32(UInt32 offset, UInt32 value) {
  writeRegIndr32(shMemBase + offset, value);
}

bool AzulNX2Ethernet::allocMemory() {
  IODMACommand::Segment64 seg;
  UInt64 offset = 0;
  UInt32 numSegs = 1;
  
  
  // TODO: 5708 has a max 40-bit DMA capability
  
  //
  // Allocate memory for status block.
  //
  stsBlockDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
                                                                  kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
                                                                  0x1000, // TODO
                                                                  0xFFFFFFFFFFFFF000ULL);
  stsBlockDesc->prepare();
  stsBlockData = stsBlockDesc->getBytesNoCopy();
  
  stsBlockCmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, 64, 0, IODMACommand::kMapped, 0, 1);
  stsBlockCmd->setMemoryDescriptor(stsBlockDesc);
  stsBlockCmd->gen64IOVMSegments(&offset, &seg, &numSegs);
  
  memset(stsBlockData, 0, 0x1000);
  
  //
  // Allocate memory for context if on a 5709/5716.
  //
  // We'll allocate an 8KB buffer, split into two 4KB pages.
  //
  if (isChip5709) {
    ctxBlockDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
                                                                   kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
                                                                   CTX_PAGE_SIZE * CTX_PAGE_CNT,
                                                                   0xFFFFFFFFFFFFF000ULL);
    ctxBlockDesc->prepare();
    ctxBlockData = ctxBlockDesc->getBytesNoCopy();
    
    ctxBlockCmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, 64, 0, IODMACommand::kMapped, 0, 1);
    ctxBlockCmd->setMemoryDescriptor(ctxBlockDesc);
    ctxBlockCmd->gen64IOVMSegments(&offset, &ctxBlockSeg, &numSegs);
    
    memset(ctxBlockData, 0, CTX_PAGE_SIZE * CTX_PAGE_CNT);
  }
  
  return true;
}

bool AzulNX2Ethernet::firmwareSync(UInt32 msgData) {
  UInt32 reg = 0;
  
  //
  // Increment the running sequence and send the message to the bootcode.
  //
  fwSyncSeq++;
  msgData |= fwSyncSeq;
  writeShMem32(NX2_DRV_MB, msgData);
  
  //
  // Wait for bootcode to acknowledge.
  //
  for (int i = 0; i < FW_ACK_TIME_OUT_MS; i++) {
    reg = readShMem32(NX2_FW_MB);
    if ((reg & NX2_FW_MSG_ACK) == (msgData & NX2_DRV_MSG_SEQ)) {
      IOLog("PASS... (0x%X) (0x%X)\n", reg, msgData);
      return true;
    }
    
    IOLog("waiting... (0x%X) (0x%X)\n", reg, msgData);
    IODelay(50);
  }
  
  //
  // Notify bootcode if we time out.
  //
  if (((reg & NX2_FW_MSG_ACK) != (msgData & NX2_DRV_MSG_SEQ)) &&
      ((msgData & NX2_DRV_MSG_DATA) != NX2_DRV_MSG_DATA_WAIT0)) {
    msgData &= ~NX2_DRV_MSG_CODE;
    msgData |= NX2_DRV_MSG_CODE_FW_TIMEOUT;
    IOLog("TIMEOUT... (0x%X) (0x%X)\n", reg, msgData);
    
    writeShMem32(NX2_DRV_MB, msgData);
    return false;
  }
  
  
  
  return false;
}

bool AzulNX2Ethernet::initContext() {
  UInt32 reg = 0;
  UInt64 ctxAddr = 0;
  
  //
  // 5709/5716 use host memory for the context. 5706/5708 have onboard memory.
  //
  if (isChip5709) {
    writeReg32(NX2_CTX_COMMAND,
               NX2_CTX_COMMAND_ENABLED |
               NX2_CTX_COMMAND_MEM_INIT |
               (1 << 12) | // FLUSH_AHEAD value of 0x1
               (4 << 16)); // PAGE_SIZE of 4K.
    
    //
    // Wait for memory initialization to complete.
    //
    for (int i = 0; i < CTX_INIT_RETRY_COUNT; i++) {
      reg = readReg32(NX2_CTX_COMMAND);
      if (!(reg & NX2_CTX_COMMAND_MEM_INIT)) {
        break;;
      }
      IODelay(2);
    }
    if ((reg & NX2_CTX_COMMAND_MEM_INIT)) {
      return false;
    }
    
    //
    // Add host pages to controller context.
    //
    ctxAddr = ctxBlockSeg.fIOVMAddr;
    for (UInt32 i = 0; i < CTX_PAGE_CNT; i++) {
      //
      // Populate page.
      //
      writeReg32(NX2_CTX_HOST_PAGE_TBL_DATA0,
                 (ctxAddr & 0xFFFFFFF0) | NX2_CTX_HOST_PAGE_TBL_DATA0_VALID);
      writeReg32(NX2_CTX_HOST_PAGE_TBL_DATA1, ctxAddr >> 32);
      writeReg32(NX2_CTX_HOST_PAGE_TBL_CTRL, i | NX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ);
      
      //
      // Verify page was written succesfully.
      //
      for (int j = 0; j < 10; j++) {
        reg = readReg32(NX2_CTX_HOST_PAGE_TBL_CTRL);
        if ((reg & NX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ) == 0) {
          break;
        }
        IODelay(5);
      }
      
      if ((reg & NX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ) != 0) {
        return false;
      }
    }
    
  }
  
  return true;
}

bool AzulNX2Ethernet::initCpus() {
  
  firmwareRv2p = bnx2_rv2p_09_6_0_17_fw;
  firmwareMips = bnx2_mips_09_6_2_1b_fw;
  
  nx2_rv2p_fw_file_t *rv2pFirmware = (nx2_rv2p_fw_file_t*) firmwareRv2p;
  
  //
  // Initialize RV2P processors.
  //
  loadRv2pFirmware(&rv2pFirmware->proc1, RV2P_PROC1);
  loadRv2pFirmware(&rv2pFirmware->proc2, RV2P_PROC2);
  IOLog("AzulNX2Ethernet: RV2P firmware loaded\n");
  
  
  //
  // Initialize additional processors.
  //
  
  return true;
}

UInt32 AzulNX2Ethernet::processRv2pFixup(UInt32 rv2pProc, UInt32 index, UInt32 fixup, UInt32 rv2pCode) {
  switch (index) {
    case 0:
      rv2pCode &= ~RV2P_BD_PAGE_SIZE_MSK;
      rv2pCode |= RV2P_BD_PAGE_SIZE;
      break;
  }
  return rv2pCode;
}

void AzulNX2Ethernet::loadRv2pFirmware(const nx2_rv2p_fw_file_entry_t *rv2pEntry, UInt32 rv2pProcessor) {
  UInt32 length   = OSSwapBigToHostInt32(rv2pEntry->rv2p.length);
  UInt32 offset   = OSSwapBigToHostInt32(rv2pEntry->rv2p.offset);
  UInt32 *codePtr = (UInt32*) &firmwareRv2p[offset];
  
  UInt32 cmd, addr, reset;
  UInt32 fixup, code;
  
  if (rv2pProcessor == RV2P_PROC1) {
    cmd   = NX2_RV2P_PROC1_ADDR_CMD_RDWR;
    addr  = NX2_RV2P_PROC1_ADDR_CMD;
    reset = NX2_RV2P_COMMAND_PROC1_RESET;
  } else {
    cmd   = NX2_RV2P_PROC2_ADDR_CMD_RDWR;
    addr  = NX2_RV2P_PROC2_ADDR_CMD;
    reset = NX2_RV2P_COMMAND_PROC2_RESET;
  }
  
  //
  // Load instructions and fixups into processor.
  //
  for (UInt32 i = 0; i < length; i += 8) {
    writeReg32(NX2_RV2P_INSTR_HIGH, *codePtr);
    codePtr++;
    writeReg32(NX2_RV2P_INSTR_LOW, *codePtr);
    codePtr++;
    
    writeReg32(addr, (i / 8) | cmd);
  }
  
  codePtr = (UInt32*) &firmwareRv2p[offset];
  for (UInt32 i = 0; i < sizeof (rv2pEntry->fixups); i++) {
    fixup = OSSwapBigToHostInt32(rv2pEntry->fixups[i]);
    if (fixup && ((fixup * 4) < length)) {
      code = OSSwapBigToHostInt32(codePtr[fixup - 1]);
      writeReg32(NX2_RV2P_INSTR_HIGH, code);
      code = OSSwapBigToHostInt32(codePtr[fixup]);
      code = processRv2pFixup(rv2pProcessor, i, fixup, code);
      writeReg32(NX2_RV2P_INSTR_LOW, code);
      
      writeReg32(addr, (fixup / 2) | cmd);
    }
  }
  
  //
  // Reset processor.
  //
  writeReg32(NX2_RV2P_COMMAND, reset);
}
