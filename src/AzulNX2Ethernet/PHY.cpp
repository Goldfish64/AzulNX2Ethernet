#include "AzulNX2Ethernet.h"

void AzulNX2Ethernet::fetchMacAddress() {
  //
  // Pull MAC address from shared memory as this is the fastest.
  // The NetXtreme also contains the MAC address in NVRAM.
  //
  UInt32 macLow   = readShMem32(NX2_PORT_HW_CFG_MAC_LOWER);
  UInt32 macHigh  = readShMem32(NX2_PORT_HW_CFG_MAC_UPPER);
  
  ethAddress.bytes[0] = (UInt8) (macHigh >> 8);
  ethAddress.bytes[1] = (UInt8) (macHigh >> 0);
  ethAddress.bytes[2] = (UInt8) (macLow >> 24);
  ethAddress.bytes[3] = (UInt8) (macLow >> 16);
  ethAddress.bytes[4] = (UInt8) (macLow >> 8);
  ethAddress.bytes[5] = (UInt8) (macLow >> 0);
  
  IOLog("AzulNX2Ethernet: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
        ethAddress.bytes[0], ethAddress.bytes[1], ethAddress.bytes[2],
        ethAddress.bytes[3], ethAddress.bytes[4], ethAddress.bytes[5]);
}
