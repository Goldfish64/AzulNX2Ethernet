#include "AzulNX2Ethernet.h"

UInt32 AzulNX2Ethernet::readReg32 (UInt32 offset) {
  return OSReadLittleInt32 (baseAddr, offset);
}

UInt32 AzulNX2Ethernet::readRegIndr32 (UInt32 offset) {
  writeReg32 (NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  return readReg32 (NX2_PCICFG_REG_WINDOW);
}

UInt32 AzulNX2Ethernet::readShMem32 (UInt32 offset) {
  return readRegIndr32 (shMemBase + offset);
}

void AzulNX2Ethernet::writeReg32 (UInt32 offset, UInt32 value) {
  OSWriteLittleInt32 (baseAddr, offset, value);
}

void AzulNX2Ethernet::writeRegIndr32 (UInt32 offset, UInt32 value) {
  writeReg32 (NX2_PCICFG_REG_WINDOW_ADDRESS, offset);
  writeReg32 (NX2_PCICFG_REG_WINDOW, value);
}

void AzulNX2Ethernet::writeShMem32 (UInt32 offset, UInt32 value) {
  writeRegIndr32 (shMemBase + offset, value);
}
