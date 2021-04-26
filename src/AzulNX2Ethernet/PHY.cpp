/*
 *
 * Copyright (c) 2021 Goldfish64
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "AzulNX2Ethernet.h"

IOReturn AzulNX2Ethernet::readPhyReg16(UInt8 offset, UInt16 *value) {
  IOReturn status = kIOReturnTimeout;
  UInt32 reg;
  
  //
  // Handle clause 45 PHYs here.
  //
  
  //
  // Disable auto polling.
  //
  
  //
  // Read from the specified PHY register.
  //
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
      
      *value = reg & 0xFFFF;
      status = kIOReturnSuccess;
      break;
    }
  }
  
  //
  // Re-enable auto polling.
  //
  
  if (IORETURN_ERR(status)) {
    SYSLOG("PHY timeout while reading register 0x%X!", offset);
  }
  return status;
}

IOReturn AzulNX2Ethernet::writePhyReg16(UInt8 offset, UInt16 value) {
  IOReturn status = kIOReturnTimeout;
  UInt32 reg;
  
  //
  // Handle clause 45 PHYs here.
  //
  
  //
  // Disable auto polling.
  //
  
  //
  // Write to the specified PHY register.
  //
  writeReg32(NX2_EMAC_MDIO_COMM,
             NX2_MIPHY(phyAddress) |
             NX2_MIREG(offset) |
             NX2_EMAC_MDIO_COMM_COMMAND_WRITE |
             NX2_EMAC_MDIO_COMM_DISEXT |
             NX2_EMAC_MDIO_COMM_START_BUSY |
             value);
  
  for (int i = 0; i < NX2_PHY_TIMEOUT; i++) {
    IODelay(10);
    
    reg = readReg32(NX2_EMAC_MDIO_COMM);
    if (!(reg & NX2_EMAC_MDIO_COMM_START_BUSY)) {
      IODelay(5);
      status = kIOReturnSuccess;
      break;
    }
  }
  
  //
  // Re-enable auto polling.
  //
  
  if (IORETURN_ERR(status)) {
    SYSLOG("PHY timeout while writing register 0x%X!", offset);
  }
  return status;
}

bool AzulNX2Ethernet::probePHY() {
  //
  // Default PHY address is 1 for copper-based controllers.
  //
  phyAddress = 1;
  
  //UInt32 oui1 = readPhyReg32(0x02);
  //UInt32 oui2 = readPhyReg32(0x03);
  
 // DBGLOG("0x%X 0%X", oui1, oui2);
  
  return true;
}

IOReturn AzulNX2Ethernet::resetPHY() {
  IOReturn status;
  UInt16 reg;
  
  //
  // Write reset bit and wait up to 100ms for PHY to reset.
  //
  status = writePhyReg16(PHY_MII_CONTROL, PHY_MII_CONTROL_RESET);
  if (IORETURN_ERR(status)) {
    return status;
  }
  
  for (int i = 0; i < 10000; i++) {
    status = readPhyReg16(PHY_MII_CONTROL, &reg);
    if (IORETURN_ERR(status)) {
      return status;
    }
    status = kIOReturnTimeout;
    
    if ((reg & PHY_MII_CONTROL_RESET) == 0) {
      status = kIOReturnSuccess;
      break;
    }
    IODelay(10);
  }
  
  if (IORETURN_ERR(status)) {
    SYSLOG("PHY link did not reset in a timely fashion");
    return kIOReturnTimeout;
  }
  
  DBGLOG("PHY has been reset");

  return status;
}

IOReturn AzulNX2Ethernet::enablePHYLoopback() {
  IOReturn status;
  UInt16 reg;
  
  status = writePhyReg16(PHY_MII_CONTROL, PHY_MII_CONTROL_LOOPBACK);
  if (IORETURN_ERR(status)) {
    return status;
  }
  
  //
  // Wait for PHY link to come down.
  //
  for (int i = 0; i < 1500; i++) {
    status = readPhyReg16(PHY_MII_STATUS, &reg);
    if (IORETURN_ERR(status)) {
      return status;
    }
    status = kIOReturnTimeout;
    
    if ((reg & PHY_MII_STATUS_LINK_UP) == 0) {
      status = kIOReturnSuccess;
      break;
    }
    IODelay(10);
  }
  
  if (IORETURN_ERR(status)) {
    SYSLOG("PHY link did not come down in a timely fashion while enabling loopback mode");
    return kIOReturnTimeout;
  } else {
    DBGLOG("PHY loopback mode is now enabled");
  }
  return status;
}

IOReturn AzulNX2Ethernet::enablePHYAutoMDIX() {
  IOReturn status;
  UInt16 reg;
  
  //
  // Get value of misc control shadow register.
  //
  status = writePhyReg16(PHY_AUX_CONTROL_SHADOW, PHY_AUX_CONTROL_SHADOW_SELECT_MISC_CONTROL);
  if (IORETURN_ERR(status)) {
    return status;
  }
  status = readPhyReg16(PHY_AUX_CONTROL_SHADOW, &reg);
  if (IORETURN_ERR(status)) {
    return status;
  }
  
  //
  // Enable Wirespeed, Auto MDIX crossover detection, and writing to the register.
  //
  reg |= PHY_MISC_CONTROL_WIRESPEED_ENABLE | /*PHY_MISC_CONTROL_FORCE_AUTO_MDIX |*/ PHY_MISC_CONTROL_WRITE_ENABLE;
  status = writePhyReg16(PHY_AUX_CONTROL_SHADOW, reg);
  if (IORETURN_ERR(status)) {
    return status;
  }
  
  //
  // Ensure ADMIX is enabled.
  //
 /* status = readPhyReg16(PHY_EXTENDED_CONTROL, &reg);
  if (IORETURN_ERR(status)) {
    return status;
  }
  reg &= ~(PHY_EXTENDED_CONTROL_DISABLE_AUTO_MDIX);
  status = writePhyReg16(PHY_EXTENDED_CONTROL, reg);*/
  
  if (!(IORETURN_ERR(status))) {
    DBGLOG("PHY auto MDIX crossover is now enabled");
  }
  
  return status;
}

IOReturn AzulNX2Ethernet::enablePHYAutoNegotiation() {
  IOReturn status;
  
  //
  // Enable 10Mbps/100Mbps autonegotiation and pause advertisements.
  // Also enable 1Gbps autonegotiation advertisements.
  //
  status = writePhyReg16(PHY_AUTO_NEG_ADVERT,
                         PHY_AUTO_NEG_ADVERT_802_3 | PHY_AUTO_NEG_ADVERT_10HD | PHY_AUTO_NEG_ADVERT_10FD |
                         PHY_AUTO_NEG_ADVERT_100HD | PHY_AUTO_NEG_ADVERT_100FD) ;//| PHY_AUTO_NEG_ADVERT_PAUSE_CAP | PHY_AUTO_NEG_ADVERT_PAUSE_ASYM);
  if (IORETURN_ERR(status)) {
    return status;
  }

  status = writePhyReg16(PHY_1000BASET_CONTROL, PHY_1000BASET_CONTROL_ADVERT_1000HD | PHY_1000BASET_CONTROL_ADVERT_1000FD);
  if (IORETURN_ERR(status)) {
    return status;
  }
  
  //
  // Force auto negotiation to run.
  //
  status = writePhyReg16(PHY_MII_CONTROL, PHY_MII_CONTROL_AUTO_NEG_RESTART | PHY_MII_CONTROL_AUTO_NEG_ENABLE);
  if (!(IORETURN_ERR(status))) {
    DBGLOG("PHY auto negotiation has started");
  }
  
  return status;
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

void AzulNX2Ethernet::updatePHYMediaState() {
  UInt16 speed;
  UInt32 mode  = readReg32(NX2_EMAC_MODE);
  if (readPhyReg16(PHY_AUX_STATUS, &speed) != kIOReturnSuccess) {
    return;
  }
  
  bool link = speed & PHY_AUX_STATUS_LINK_UP;
  speed    &= PHY_AUX_STATUS_SPEED_MASK;
  mode     &= ~(NX2_EMAC_MODE_PORT | NX2_EMAC_MODE_HALF_DUPLEX |
                NX2_EMAC_MODE_MAC_LOOP | NX2_EMAC_MODE_FORCE_LINK |
                NX2_EMAC_MODE_25G);
  
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
  
  //
  // PHY link speed is dependent on the link speed.
  //
  if (link) {
    switch (mediaState.speed) {
      case kLinkSpeed10:
        if (NX2_CHIP_NUM == NX2_CHIP_NUM_5706) {
          mode |= NX2_EMAC_MODE_PORT_MII_10;
          DBGLOG("PHY link speed: MII 10Mb");
          break;
        }
        
      case kLinkSpeed100:
        mode |= NX2_EMAC_MODE_PORT_MII;
        DBGLOG("PHY link speed: MII");
        break;
        
      case kLinkSpeed1000:
      default:
        mode |= NX2_EMAC_MODE_PORT_GMII;
        DBGLOG("PHY link speed: GMII");
        break;
    }
    
    if (mediaState.duplex == kLinkDuplexHalf) {
      mode |= NX2_EMAC_MODE_HALF_DUPLEX;
      DBGLOG("PHY duplex mode: half");
    } else {
      DBGLOG("PHY duplex mode: full");
    }
  } else {
    DBGLOG("PHY link speed: none");
  }
  
  //
  // Update OS with link status.
  //
  writeReg32(NX2_EMAC_MODE, mode);
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
