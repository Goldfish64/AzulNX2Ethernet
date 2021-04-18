#include "AzulNX2Ethernet.h"

UInt32 AzulNX2Ethernet::readPhyReg32(UInt32 offset) {
  UInt32 reg = 0;
  
  writeReg32(NX2_EMAC_MDIO_COMM,
             NX2_MIPHY(phyAddress) |
             NX2_MIREG(offset) |
             NX2_EMAC_MDIO_COMM_COMMAND_READ |
             NX2_EMAC_MDIO_COMM_DISEXT |
             NX2_EMAC_MDIO_COMM_START_BUSY);
  
  for (int i = 0; i < NX2_PHY_TIMEOUT; i++) {
    IODelay(10);
    
    reg = readReg32(NX2_EMAC_MDIO_COMM);
    if (!(reg & NX2_EMAC_MDIO_COMM_START_BUSY)) {
      IODelay(5);
      
      reg = readReg32(NX2_EMAC_MDIO_COMM);
      reg &= NX2_EMAC_MDIO_COMM_DATA;
      return reg;
    }
  }
  
  SYSLOG("PHY timeout while reading register 0x%X!", offset);
  return UINT32_MAX;
}

void AzulNX2Ethernet::addNetworkMedium(UInt32 index, UInt32 type, UInt32 speed) {
  IONetworkMedium *medium = IONetworkMedium::medium(type, speed * MBit, 0, index);
  if (medium != NULL) {
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
  }
}

void AzulNX2Ethernet::createMediumDictionary() {
  //
  // Create medium dictionary with all possible speeds.
  //
  mediumDict = OSDictionary::withCapacity(kMediumTypeCount);
  
  addNetworkMedium(kMediumTypeIndexAuto, kIOMediumEthernetAuto, kLinkSpeedNone);
  addNetworkMedium(kMediumTypeIndex10HD, kIOMediumEthernet10BaseT | kIOMediumOptionHalfDuplex, kLinkSpeed10);
  addNetworkMedium(kMediumTypeIndex10FD, kIOMediumEthernet10BaseT | kIOMediumOptionFullDuplex, kLinkSpeed10);
  addNetworkMedium(kMediumTypeIndex100HD, kIOMediumEthernet100BaseTX | kIOMediumOptionHalfDuplex, kLinkSpeed100);
  addNetworkMedium(kMediumTypeIndex100FD, kIOMediumEthernet100BaseTX | kIOMediumOptionFullDuplex, kLinkSpeed100);
  addNetworkMedium(kMediumTypeIndex1000HD, kIOMediumEthernet1000BaseT | kIOMediumOptionHalfDuplex, kLinkSpeed1000);
  addNetworkMedium(kMediumTypeIndex1000FD, kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, kLinkSpeed1000);
  
  publishMediumDictionary(mediumDict);
}

bool AzulNX2Ethernet::probePHY() {
  //
  // Default PHY address is 1 for copper-based controllers.
  //
  phyAddress = 1;
  
  UInt32 oui1 = readPhyReg32(0x02);
  UInt32 oui2 = readPhyReg32(0x03);
  
  DBGLOG("0x%X 0%X", oui1, oui2);
  
  return true;
}

void AzulNX2Ethernet::updatePHYMediaState() {
  UInt32 speed = readPhyReg32(PHY_AUX_STATUS);
  if (speed == PHY_FAIL) {
    return;
  }
  
  bool link = speed & PHY_AUX_STATUS_LINK_UP;
  speed    &= PHY_AUX_STATUS_SPEED_MASK;
  
  switch (speed) {
    case PHY_AUX_STATUS_SPEED_10HD:
      currentMediumIndex  = kMediumTypeIndex10HD;
      mediaState.duplex   = kLinkDuplexHalf;
      mediaState.speed    = kLinkSpeed10;
      break;
      
    case PHY_AUX_STATUS_SPEED_10FD:
      currentMediumIndex  = kMediumTypeIndex10FD;
      mediaState.duplex   = kLinkDuplexFull;
      mediaState.speed    = kLinkSpeed10;
      break;
      
    case PHY_AUX_STATUS_SPEED_100HD:
      currentMediumIndex  = kMediumTypeIndex100HD;
      mediaState.duplex   = kLinkDuplexHalf;
      mediaState.speed    = kLinkSpeed100;
      break;
      
    case PHY_AUX_STATUS_SPEED_100FD:
      currentMediumIndex  = kMediumTypeIndex100FD;
      mediaState.duplex   = kLinkDuplexFull;
      mediaState.speed    = kLinkSpeed100;
      break;
      
    case PHY_AUX_STATUS_SPEED_1000HD:
      currentMediumIndex  = kMediumTypeIndex1000HD;
      mediaState.duplex   = kLinkDuplexHalf;
      mediaState.speed    = kLinkSpeed1000;
      break;
      
    case PHY_AUX_STATUS_SPEED_1000FD:
      currentMediumIndex  = kMediumTypeIndex1000FD;
      mediaState.duplex   = kLinkDuplexFull;
      mediaState.speed    = kLinkSpeed1000;
      break;
      
    default:
      mediaState.duplex   = kLinkDuplexNone;
      mediaState.speed    = kLinkSpeedNone;
      break;
  }
  
  if (link) {
    setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive,
                  IONetworkMedium::getMediumWithIndex(mediumDict, currentMediumIndex));
    SYSLOG("Link is up at %u Mbps, %s duplex", mediaState.speed, mediaState.duplex == kLinkDuplexFull ? "full" : "half");
  } else {
    setLinkStatus(kIONetworkLinkValid, 0);
    SYSLOG("Link is down");
  }
}

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


void AzulNX2Ethernet::handlePHYInterrupt(status_block_t *stsBlock) {
  bool newLink = stsBlock->attnBits & STATUS_ATTN_BITS_LINK_STATE;
  
  if (newLink) {
    writeReg32(NX2_PCICFG_STATUS_BIT_SET_CMD, STATUS_ATTN_BITS_LINK_STATE);
  } else {
    writeReg32(NX2_PCICFG_STATUS_BIT_CLEAR_CMD, STATUS_ATTN_BITS_LINK_STATE);
  }
  writeReg32(NX2_EMAC_STATUS, NX2_EMAC_STATUS_LINK_CHANGE);
  
  updatePHYMediaState();  
  
  writeReg32(NX2_HC_COMMAND, readReg32(NX2_HC_COMMAND) | NX2_HC_COMMAND_COAL_NOW_WO_INT);
  readReg32(NX2_HC_COMMAND);
}
