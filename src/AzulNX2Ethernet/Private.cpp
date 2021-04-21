#include "AzulNX2Ethernet.h"
#include "Firmware.h"

//
// Device mappings.
//
static const struct nx2_device_type {
  UInt16        vendorId;
  UInt16        deviceId;
  UInt16        subVendorId;
  UInt16        subDeviceId;
  const char    *vendor;
  const char    *model;
} deviceNames[] = {
  //
  // BCM5706C controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5706, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5706 Gigabit Ethernet" },
  
  //
  // BCM5706S controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5706S, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5706S Gigabit Ethernet" },
  
  //
  // BCM5708C controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5708, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5708 Gigabit Ethernet" },
  
  //
  // BCM5708S controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5708S, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5708S Gigabit Ethernet" },
  
  //
  // BCM5709C controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5709, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5709 Gigabit Ethernet" },
  
  //
  // BCM5709S controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5709S, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5709S Gigabit Ethernet" },
  
  //
  // BCM5716 controllers.
  //
  { BRCM_VENDORID, BRCM_DEVICEID_BCM5716, PCI_ANY_ID, PCI_ANY_ID,
    "QLogic", "NetXtreme II BCM5716 Gigabit Ethernet" },
};

void AzulNX2Ethernet::logPrint(const char *format, ...) {
  char tmp[1024];
  tmp[0] = '\0';
  va_list va;
  va_start(va, format);
  vsnprintf(tmp, sizeof (tmp), format, va);
  va_end(va);
  
  IOLog("AzulNX2Ethernet: %s\n", tmp);
}

/**
 Reads the specified register using memory space.
 */
UInt16 AzulNX2Ethernet::readReg16(UInt32 offset) {
  return OSReadLittleInt16(baseAddr, offset);
}

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

UInt32 AzulNX2Ethernet::readContext32(UInt32 cid, UInt32 offset) {
  UInt32 reg = 0;
  
  if (isChip5709) {
    writeReg32(NX2_CTX_CTX_CTRL, (cid + offset) | NX2_CTX_CTX_CTRL_READ_REQ);
    
    for (int i = 0; i < 100; i++) {
      reg = readReg32(NX2_CTX_CTX_CTRL);
     // IOLog("Context write %X\n", reg);
      if ((reg & NX2_CTX_CTX_CTRL_READ_REQ) == 0) {
       // IOLog("Context write done!\n");
        return readReg32(NX2_CTX_CTX_DATA);
      }
      IODelay(5);
    }
  }
  
  return 0;
}

void AzulNX2Ethernet::writeReg16(UInt32 offset, UInt16 value) {
  OSWriteLittleInt16(baseAddr, offset, value);
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

void AzulNX2Ethernet::writeContext32(UInt32 cid, UInt32 offset, UInt32 value) {
  UInt32 reg = 0;
  DBGLOG("Context write (cid = 0x%X, offset = 0x%X, value = 0x%X)", cid, offset, value);
  
  if (isChip5709) {
    writeReg32(NX2_CTX_CTX_DATA, value);
    writeReg32(NX2_CTX_CTX_CTRL, (cid + offset) | NX2_CTX_CTX_CTRL_WRITE_REQ);
    
    for (int i = 0; i < 100; i++) {
      reg = readReg32(NX2_CTX_CTX_CTRL);
     // IOLog("Context write %X\n", reg);
      if ((reg & NX2_CTX_CTX_CTRL_WRITE_REQ) == 0) {
       // IOLog("Context write done!\n");
        return;
      }
      IODelay(5);
    }
  }
}

const char* AzulNX2Ethernet::getDeviceVendor() const {
  for (UInt32 i = 0; i < sizeof (deviceNames); i++) {
    if (pciVendorId == deviceNames[i].vendorId && pciDeviceId == deviceNames[i].deviceId &&
        (pciSubVendorId == deviceNames[i].subVendorId || deviceNames[i].subVendorId == PCI_ANY_ID) &&
        (pciDeviceId == deviceNames[i].subDeviceId || deviceNames[i].subDeviceId == PCI_ANY_ID)) {
      return deviceNames[i].vendor;
    }
  }
  
  return NULL;
}

const char* AzulNX2Ethernet::getDeviceModel() const {
  for (UInt32 i = 0; i < sizeof (deviceNames); i++) {
    if (pciVendorId == deviceNames[i].vendorId && pciDeviceId == deviceNames[i].deviceId &&
        (pciSubVendorId == deviceNames[i].subVendorId || deviceNames[i].subVendorId == PCI_ANY_ID) &&
        (pciDeviceId == deviceNames[i].subDeviceId || deviceNames[i].subDeviceId == PCI_ANY_ID)) {
      return deviceNames[i].model;
    }
  }
  
  return NULL;
}

void AzulNX2Ethernet::enableInterrupts(bool coalNow) {
  writeReg32(NX2_PCICFG_INT_ACK_CMD,
             NX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
             NX2_PCICFG_INT_ACK_CMD_MASK_INT |
             lastStatusIndex);
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_INDEX_VALID | lastStatusIndex);
  
  if (coalNow) {
    writeReg32(NX2_HC_COMMAND, readReg32(NX2_HC_COMMAND) | NX2_HC_COMMAND_COAL_NOW);
  }
}

void AzulNX2Ethernet::disableInterrupts() {
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_MASK_INT);
  readReg32(NX2_PCICFG_INT_ACK_CMD);
}

bool AzulNX2Ethernet::allocDmaBuffer(azul_nx2_dma_buf_t *dmaBuf, size_t size, UInt32 alignment) {
  IOBufferMemoryDescriptor  *bufDesc;
  IODMACommand              *dmaCmd;
  IODMACommand::Segment64   seg64;
  UInt64                    offset = 0;
  UInt32                    numSegs = 1;
  
  mach_vm_address_t         physMask;
  UInt8                     dmaBits;
  
  //
  // The BCM5708 cannot use more than 40 bit addresses.
  //
  physMask  = NX2_CHIP_NUM == NX2_CHIP_NUM_5708 ? DMA_BITS_40 : DMA_BITS_64;
  physMask &= ~(alignment - 1);
  dmaBits   = NX2_CHIP_NUM == NX2_CHIP_NUM_5708 ? 40 : 64;
  
  //
  // Create DMA buffer with required specifications and get physical address.
  //
  bufDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache, size, physMask);
  if (bufDesc == NULL) {
    SYSLOG("Failed to allocate DMA buffer memory of %u bytes", size);
    return false;
  }
  
  bufDesc->prepare();
  dmaCmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, dmaBits, 0, IODMACommand::kMapped, 0, alignment);
  if (dmaCmd == NULL) {
    SYSLOG("Failed to allocate IODMACommand object of %u bytes", size);
    
    bufDesc->complete();
    bufDesc->release();
    return false;
  }
  
  dmaCmd->setMemoryDescriptor(bufDesc);
  if (dmaCmd->gen64IOVMSegments(&offset, &seg64, &numSegs) != kIOReturnSuccess) {
    SYSLOG("Failed to generate physical address segment for buffer of %u bytes", size);
    
    dmaCmd->release();
    bufDesc->complete();
    bufDesc->release();
    return false;
  }
  
  dmaBuf->bufDesc = bufDesc;
  dmaBuf->dmaCmd = dmaCmd;
  dmaBuf->physAddr = seg64.fIOVMAddr;
  dmaBuf->buffer = bufDesc->getBytesNoCopy();
  dmaBuf->size = size;
  
  memset(dmaBuf->buffer, 0, dmaBuf->size);
  DBGLOG("Mapped buffer of %u bytes to 0x%llX", dmaBuf->size, dmaBuf->physAddr);
  return true;
}

bool AzulNX2Ethernet::allocMemory() {
  
  allocDmaBuffer(&statusBuffer, 0x1000, 16);
  allocDmaBuffer(&statsBuffer, 0x1000, 16);
  allocDmaBuffer(&contextBuffer, CTX_PAGE_SIZE * CTX_PAGE_CNT, 4096);
  allocDmaBuffer(&transmitBuffer, 0x1000, 4096);
  allocDmaBuffer(&receiveBuffer, 0x1000, 4096);
  
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
    ctxAddr = contextBuffer.physAddr;
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
      
      ctxAddr += 0x1000;
    }
    
  }
  
  return true;
}

void AzulNX2Ethernet::initCpus() {
  
  firmwareRv2p = bnx2_rv2p_09_6_0_17_fw;
  firmwareMips = bnx2_mips_09_6_2_1b_fw;
  
  nx2_rv2p_fw_file_t *rv2pFirmware = (nx2_rv2p_fw_file_t*) firmwareRv2p;
  
  //
  // Initialize RV2P processors.
  //
  loadRv2pFirmware(RV2P_PROC1, &rv2pFirmware->proc1);
  loadRv2pFirmware(RV2P_PROC2, &rv2pFirmware->proc2);
  IOLog("AzulNX2Ethernet: RV2P firmware loaded\n");
  
  //
  // Initialize additional processors.
  //
  initCpuRxp();
  initCpuTxp();
  initCpuTpat();
  initCpuCom();
  initCpuCp();
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

void AzulNX2Ethernet::loadRv2pFirmware(UInt32 rv2pProcessor, const nx2_rv2p_fw_file_entry_t *rv2pEntry) {
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

void AzulNX2Ethernet::loadCpuFirmware(const cpu_reg_t *cpuReg, const nx2_mips_fw_file_entry_t *mipsEntry) {
  UInt32 address, length, fwOffset, offset;
  UInt32 *codePtr;
  
  stopCpu(cpuReg);
  
  //
  // Load Text region.
  // All fields are big endian, and the controller assumes all data coming in
  // is little endian, so we need to byteswap everything.
  //
  address   = OSSwapBigToHostInt32(mipsEntry->text.address);
  length    = OSSwapBigToHostInt32(mipsEntry->text.length);
  fwOffset  = OSSwapBigToHostInt32(mipsEntry->text.offset);
  codePtr   = (UInt32*) &firmwareMips[fwOffset];
  offset    = cpuReg->spadBase + (address - cpuReg->mipsViewBase);
  
  if (length) {
    for (UInt32 i = 0; i < length / 4; i++, offset += 4) {
      writeRegIndr32(offset, OSSwapBigToHostInt32(codePtr[i]));
    }
  }
  
  //
  // Load Data region.
  //
  address   = OSSwapBigToHostInt32(mipsEntry->data.address);
  length    = OSSwapBigToHostInt32(mipsEntry->data.length);
  fwOffset  = OSSwapBigToHostInt32(mipsEntry->data.offset);
  codePtr   = (UInt32*) &firmwareMips[fwOffset];
  offset    = cpuReg->spadBase + (address - cpuReg->mipsViewBase);
  
  if (length) {
    for (UInt32 i = 0; i < length / 4; i++, offset += 4) {
      writeRegIndr32(offset, OSSwapBigToHostInt32(codePtr[i]));
    }
  }
  
  //
  // Load Read-only Data region.
  //
  address   = OSSwapBigToHostInt32(mipsEntry->roData.address);
  length    = OSSwapBigToHostInt32(mipsEntry->roData.length);
  fwOffset  = OSSwapBigToHostInt32(mipsEntry->roData.offset);
  codePtr   = (UInt32*) &firmwareMips[fwOffset];
  offset    = cpuReg->spadBase + (address - cpuReg->mipsViewBase);
  
  if (length) {
    for (UInt32 i = 0; i < length / 4; i++, offset += 4) {
      writeRegIndr32(offset, OSSwapBigToHostInt32(codePtr[i]));
    }
  }
  
  //
  // Clear prefetch and set starting address.
  //
  address = OSSwapBigToHostInt32(mipsEntry->startAddress);
  writeRegIndr32(cpuReg->inst, 0);
  writeRegIndr32(cpuReg->pc, address);
  IOLog("AzulNX2Ethernet: MIPS CPU 0x%X will start at 0x%X\n", cpuReg->mode, address);
}

void AzulNX2Ethernet::startCpu(const cpu_reg_t *cpuReg) {
  //
  // Start specified CPU.
  //
  UInt32 reg = readRegIndr32(cpuReg->mode);
  reg &= ~cpuReg->modeValueHalt;
  
  writeRegIndr32(cpuReg->state, cpuReg->stateValueClear);
  writeRegIndr32(cpuReg->mode, reg);
}

void AzulNX2Ethernet::stopCpu(const cpu_reg_t *cpuReg) {
  //
  // Stop specified CPU.
  //
  UInt32 reg = readRegIndr32(cpuReg->mode);
  reg |= cpuReg->modeValueHalt;
  
  writeRegIndr32(cpuReg->mode, reg);
  writeRegIndr32(cpuReg->state, cpuReg->stateValueClear);
}

void AzulNX2Ethernet::initCpuRxp() {
  cpu_reg_t           rxpCpuReg;
  nx2_mips_fw_file_t  *mipsFirmware = (nx2_mips_fw_file_t*) firmwareMips;
  
  rxpCpuReg.mode              = NX2_RXP_CPU_MODE;
  rxpCpuReg.modeValueHalt     = NX2_RXP_CPU_MODE_SOFT_HALT;
  rxpCpuReg.modeValueSstep    = NX2_RXP_CPU_MODE_STEP_ENA;
  rxpCpuReg.state             = NX2_RXP_CPU_STATE;
  rxpCpuReg.stateValueClear   = 0xFFFFFF;
  rxpCpuReg.gpr0              = NX2_RXP_CPU_REG_FILE;
  rxpCpuReg.evmask            = NX2_RXP_CPU_EVENT_MASK;
  rxpCpuReg.pc                = NX2_RXP_CPU_PROGRAM_COUNTER;
  rxpCpuReg.inst              = NX2_RXP_CPU_INSTRUCTION;
  rxpCpuReg.bp                = NX2_RXP_CPU_HW_BREAKPOINT;
  rxpCpuReg.spadBase          = NX2_RXP_SCRATCH;
  rxpCpuReg.mipsViewBase      = 0x8000000;
  
  loadCpuFirmware(&rxpCpuReg, &mipsFirmware->rxp);
  startCpu(&rxpCpuReg);
  IOLog("AzulNX2Ethernet: Started RX processor\n");
}

void AzulNX2Ethernet::initCpuTxp() {
  cpu_reg_t           txpCpuReg;
  nx2_mips_fw_file_t  *mipsFirmware = (nx2_mips_fw_file_t*) firmwareMips;
  
  txpCpuReg.mode              = NX2_TXP_CPU_MODE;
  txpCpuReg.modeValueHalt     = NX2_TXP_CPU_MODE_SOFT_HALT;
  txpCpuReg.modeValueSstep    = NX2_TXP_CPU_MODE_STEP_ENA;
  txpCpuReg.state             = NX2_TXP_CPU_STATE;
  txpCpuReg.stateValueClear   = 0xFFFFFF;
  txpCpuReg.gpr0              = NX2_TXP_CPU_REG_FILE;
  txpCpuReg.evmask            = NX2_TXP_CPU_EVENT_MASK;
  txpCpuReg.pc                = NX2_TXP_CPU_PROGRAM_COUNTER;
  txpCpuReg.inst              = NX2_TXP_CPU_INSTRUCTION;
  txpCpuReg.bp                = NX2_TXP_CPU_HW_BREAKPOINT;
  txpCpuReg.spadBase          = NX2_TXP_SCRATCH;
  txpCpuReg.mipsViewBase      = 0x8000000;
  
  loadCpuFirmware(&txpCpuReg, &mipsFirmware->txp);
  startCpu(&txpCpuReg);
  IOLog("AzulNX2Ethernet: Started TX processor\n");
  
  SYSLOG("TXP PC %X", readRegIndr32(NX2_TXP_CPU_PROGRAM_COUNTER));
}

void AzulNX2Ethernet::initCpuTpat() {
  cpu_reg_t           tpatCpuReg;
  nx2_mips_fw_file_t  *mipsFirmware = (nx2_mips_fw_file_t*) firmwareMips;
  
  tpatCpuReg.mode             = NX2_TPAT_CPU_MODE;
  tpatCpuReg.modeValueHalt    = NX2_TPAT_CPU_MODE_SOFT_HALT;
  tpatCpuReg.modeValueSstep   = NX2_TPAT_CPU_MODE_STEP_ENA;
  tpatCpuReg.state            = NX2_TPAT_CPU_STATE;
  tpatCpuReg.stateValueClear  = 0xFFFFFF;
  tpatCpuReg.gpr0             = NX2_TPAT_CPU_REG_FILE;
  tpatCpuReg.evmask           = NX2_TPAT_CPU_EVENT_MASK;
  tpatCpuReg.pc               = NX2_TPAT_CPU_PROGRAM_COUNTER;
  tpatCpuReg.inst             = NX2_TPAT_CPU_INSTRUCTION;
  tpatCpuReg.bp               = NX2_TPAT_CPU_HW_BREAKPOINT;
  tpatCpuReg.spadBase         = NX2_TPAT_SCRATCH;
  tpatCpuReg.mipsViewBase     = 0x8000000;
  
  loadCpuFirmware(&tpatCpuReg, &mipsFirmware->tpat);
  startCpu(&tpatCpuReg);
  IOLog("AzulNX2Ethernet: Started TX patch-up processor\n");
}

void AzulNX2Ethernet::initCpuCom() {
  cpu_reg_t           comCpuReg;
  nx2_mips_fw_file_t  *mipsFirmware = (nx2_mips_fw_file_t*) firmwareMips;
  
  comCpuReg.mode              = NX2_COM_CPU_MODE;
  comCpuReg.modeValueHalt     = NX2_COM_CPU_MODE_SOFT_HALT;
  comCpuReg.modeValueSstep    = NX2_COM_CPU_MODE_STEP_ENA;
  comCpuReg.state             = NX2_COM_CPU_STATE;
  comCpuReg.stateValueClear   = 0xFFFFFF;
  comCpuReg.gpr0              = NX2_COM_CPU_REG_FILE;
  comCpuReg.evmask            = NX2_COM_CPU_EVENT_MASK;
  comCpuReg.pc                = NX2_COM_CPU_PROGRAM_COUNTER;
  comCpuReg.inst              = NX2_COM_CPU_INSTRUCTION;
  comCpuReg.bp                = NX2_COM_CPU_HW_BREAKPOINT;
  comCpuReg.spadBase          = NX2_COM_SCRATCH;
  comCpuReg.mipsViewBase      = 0x8000000;
  
  loadCpuFirmware(&comCpuReg, &mipsFirmware->com);
  startCpu(&comCpuReg);
  IOLog("AzulNX2Ethernet: Started Completion processor\n");
}

void AzulNX2Ethernet::initCpuCp() {
  cpu_reg_t           cpCpuReg;
  nx2_mips_fw_file_t  *mipsFirmware = (nx2_mips_fw_file_t*) firmwareMips;
  
  cpCpuReg.mode               = NX2_CP_CPU_MODE;
  cpCpuReg.modeValueHalt      = NX2_CP_CPU_MODE_SOFT_HALT;
  cpCpuReg.modeValueSstep     = NX2_CP_CPU_MODE_STEP_ENA;
  cpCpuReg.state              = NX2_CP_CPU_STATE;
  cpCpuReg.stateValueClear    = 0xFFFFFF;
  cpCpuReg.gpr0               = NX2_CP_CPU_REG_FILE;
  cpCpuReg.evmask             = NX2_CP_CPU_EVENT_MASK;
  cpCpuReg.pc                 = NX2_CP_CPU_PROGRAM_COUNTER;
  cpCpuReg.inst               = NX2_CP_CPU_INSTRUCTION;
  cpCpuReg.bp                 = NX2_CP_CPU_HW_BREAKPOINT;
  cpCpuReg.spadBase           = NX2_CP_SCRATCH;
  cpCpuReg.mipsViewBase       = 0x8000000;
  
  loadCpuFirmware(&cpCpuReg, &mipsFirmware->cp);
  startCpu(&cpCpuReg);
  IOLog("AzulNX2Ethernet: Started Command processor\n");
}
