#include "AzulNX2Ethernet.h"

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
      break;
    }
    
    IODelay(10);
  }
  
  //
  // Notify bootcode if we time out.
  //
  if ((reg & NX2_FW_MSG_ACK) == (msgData & NX2_DRV_MSG_SEQ) &&
      (msgData & NX2_DRV_MSG_DATA) != NX2_DRV_MSG_DATA_WAIT0) {
    msgData &= ~NX2_DRV_MSG_CODE;
    msgData |= NX2_DRV_MSG_CODE_FW_TIMEOUT;
    
    writeShMem32(NX2_DRV_MB, msgData);
    return false;
  }
  
  return true;
}
