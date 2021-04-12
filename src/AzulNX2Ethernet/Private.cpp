#include "AzulNX2Ethernet.h"

/**
 Reads the specified register using memory space.
 */
UInt32 AzulNX2Ethernet::readReg32 (UInt32 offset) {
  return OSReadLittleInt32 (baseAddr, offset);
}

/**
 Reads the specified register using PCI config space.
 */
UInt32 AzulNX2Ethernet::readRegIndr32 (UInt32 offset) {
  pciNub->configWrite32 (NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  return pciNub->configRead32 (NX2_PCICFG_REG_WINDOW);
}

/**
 Reads the specified register from shared memory.
 */
UInt32 AzulNX2Ethernet::readShMem32 (UInt32 offset) {
  return readRegIndr32 (shMemBase + offset);
}

/**
 Writes the specified register using memory space.
 */
void AzulNX2Ethernet::writeReg32 (UInt32 offset, UInt32 value) {
  OSWriteLittleInt32 (baseAddr, offset, value);
}

/**
 Writes the specified register using PCI config space.
 */
void AzulNX2Ethernet::writeRegIndr32 (UInt32 offset, UInt32 value) {
  pciNub->configWrite32 (NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  pciNub->configWrite32 (NX2_PCICFG_REG_WINDOW, value);
}

/**
 Writes the specified register from shared memory.
 */
void AzulNX2Ethernet::writeShMem32 (UInt32 offset, UInt32 value) {
  writeRegIndr32 (shMemBase + offset, value);
}
