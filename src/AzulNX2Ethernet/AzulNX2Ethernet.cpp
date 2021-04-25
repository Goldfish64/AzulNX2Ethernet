#include "AzulNX2Ethernet.h"

OSDefineMetaClassAndStructors (AzulNX2Ethernet, super);

bool AzulNX2Ethernet::start(IOService *provider) {
  bool started = false;
  
  SYSLOG("start()");
  

  isEnabled = false;
  
  
  do {
    if (!super::start(provider)) {
      break;
    }
    
    started = true;
    
    //
    // Get parent PCI device nub.
    //
    pciNub = OSDynamicCast(IOPCIDevice, provider);
    if (pciNub == NULL) {
      break;
    }
    pciNub->retain();
    
    if (!pciNub->open(this)) {
      break;
    }
    
    //
    // Map the base address. The NetXtreme II uses BAR0 only.
    //
    baseMemoryMap = pciNub->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (baseMemoryMap == NULL) {
      break;
    }
    baseAddr = (volatile void *) baseMemoryMap->getVirtualAddress();
    
    if (!initEventSources(provider)) {
      SYSLOG("Failed to setup event sources!");
      break;
    }
    
    //
    // Reset and configure the NetXtreme II controller.
    //
    if (!prepareController()) {
      IOLog("AzulNX2Ethernet: Controller prep failed!\n");
      break;
    }
    if (!resetController(NX2_DRV_MSG_CODE_RESET)) {
      IOLog("AzulNX2Ethernet: Controller reset failed!\n");
      break;
    }
    if (!initControllerChip()) {
      IOLog("AzulNX2Ethernet: Controller initialization failed!\n");
      break;
    }
    
    if (started == false) {
      return false;
    }
    
    
  } while (false);
  
  if (pciNub != NULL) {
    pciNub->close(this);
  }

  bool result = started;
  

  
  result = attachInterface((IONetworkInterface **)&ethInterface);
  
  SYSLOG("rev %X", NX2_CHIP_ID);
  
  probePHY();
  createMediumDictionary();

  
  //enableInterrupts(true);
 // setLinkStatus(kIONetworkLinkValid);
  
  return result;
}

IOWorkLoop* AzulNX2Ethernet::getWorkLoop() const {
  return workLoop;
}

bool AzulNX2Ethernet::createWorkLoop() {
  workLoop = IOWorkLoop::workLoop();
  return workLoop != NULL;
}

IOOutputQueue* AzulNX2Ethernet::createOutputQueue() {
  return IOBasicOutputQueue::withTarget(this, 1000);
}

const OSString* AzulNX2Ethernet::newVendorString() const {
  return OSString::withCString(getDeviceVendor());
}

const OSString* AzulNX2Ethernet::newModelString() const {
  return OSString::withCString(getDeviceModel());
}

UInt32 AzulNX2Ethernet::outputPacket(mbuf_t m, void *param) {
  
  if (!isEnabled)
    return kIOReturnOutputSuccess;
  
  return sendTxPacket(m);
}

IOReturn AzulNX2Ethernet::enable(IONetworkInterface *interface) {
  DBGLOG("enable()");
  
  updatePHYMediaState();
  
  txQueue->setCapacity(1000);
  txQueue->start();
  
 // return super::enable(interface);
  return kIOReturnSuccess;
}

IOReturn AzulNX2Ethernet::getHardwareAddress(IOEthernetAddress *address) {
  memcpy (address, &ethAddress, kIOEthernetAddressSize);
  return kIOReturnSuccess;
}

void AzulNX2Ethernet::interruptOccurred(IOInterruptEventSource *source, int count) {
  if (!isEnabled) {
    return;;
  }
  
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM | NX2_PCICFG_INT_ACK_CMD_MASK_INT);
  
  
 // UInt32 *hcsMem32 = (UInt32*)stsBlockData;
  
  //IOLog("INT\n");
 // IOLog("INT status %X ack %X, %X time %X IDX %X\n", hcsMem32[0], hcsMem32[1], hcsMem32[8], (((uint8_t*)stsBlockData)[0x34]), hcsMem32[13]);
  
 // SYSLOG("INT status %X (%X) index %u tx idx %u rx idx %u", statusBlock->attnBits, statusBlock->attnBitsAck, statusBlock->index, readTxCons(), statusBlock->status_rx_quick_consumer_index0);
  
 // SYSLOG("RX EMAC STS %X %X %X", readReg32(NX2_EMAC_RX_STAT_IFHCINBADOCTETS), readReg32(NX2_EMAC_RX_STAT_IFHCINOCTETS), readReg32(NX2_EMAC_RX_STAT_IFHCINBROADCASTPKTS));
 // SYSLOG("TX EMAC STS %X %X", readReg32(NX2_EMAC_TX_STATUS), readReg32(NX2_EMAC_TX_STAT_IFHCOUTOCTETS));
  //SYSLOG("TXP PC %X", readRegIndr32(NX2_TXP_CPU_PROGRAM_COUNTER));
  //SYSLOG("TXP %X %X", readReg32(NX2_TXP_CPU_STATE), readReg32(NX2_TXP_CPU_EVENT_MASK));
  
  /*if (statusBlock->status_rx_quick_consumer_index0 > 0) {
    UInt8 *dd = (UInt8*) mbuf_data(rxPackets[statusBlock->status_rx_quick_consumer_index0 - 1]);
    dd += 0x12;
    SYSLOG("RX d %X %X %X %X %X %X %X", dd[0], dd[1], dd[2], dd[3], dd[4], dd[5], dd[6]);
  }*/
  
  
  if ((statusBlock->attnBits & STATUS_ATTN_BITS_LINK_STATE) != (statusBlock->attnBitsAck & STATUS_ATTN_BITS_LINK_STATE)) {
    handlePHYInterrupt(statusBlock);
  }
  
  UInt16 txConsNew = readTxCons();
  if (txCons != txConsNew) {
    handleTxInterrupt(txConsNew);
  }
  
  UInt16 rxConsNew = readRxCons();
  if (rxCons != rxConsNew) {
    handleRxInterrupt(rxConsNew);
  }
  
  lastStatusIndex = statusBlock->index;
  enableInterrupts(false);
}

IOReturn AzulNX2Ethernet::getMaxPacketSize(UInt32 *maxSize) const
{
  *maxSize = MAX_PACKET_SIZE;

  return kIOReturnSuccess;
}

