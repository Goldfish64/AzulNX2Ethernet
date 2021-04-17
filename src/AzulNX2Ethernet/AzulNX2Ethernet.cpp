#include "AzulNX2Ethernet.h"

OSDefineMetaClassAndStructors (AzulNX2Ethernet, super);

bool AzulNX2Ethernet::start(IOService *provider) {
  bool started = false;
  
  IOLog("AzulNX2Ethernet::start\n");
  
  isChip5709 = true; // TMP
  


  
  
  do {
    if (!super::start(provider)) {
      break;
    }
    
    workloop = IOWorkLoop::workLoop();


    intSource = IOInterruptEventSource::interruptEventSource(
          this, OSMemberFunctionCast(IOInterruptEventAction, this,
                                     &AzulNX2Ethernet::interruptOccurred),
          provider, 1);

      if (!intSource ||
          (workloop->addEventSource(intSource) != kIOReturnSuccess)) {

        IOLog("Failed to register interrupt source");
        return false;

      }
    
    intSource->enable();
    
    mediumDict = OSDictionary::withCapacity(3);
    IONetworkMedium *medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 0);
    IOLog("medium != NULL: %u\n", medium != NULL);
    bool result = IONetworkMedium::addMedium(mediumDict, medium);
    IOLog("medium  add good: %u\n", result);
    medium->release();
    
    medium = IONetworkMedium::medium(kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000);
    IOLog("medium != NULL: %u\n", medium != NULL);
    result = IONetworkMedium::addMedium(mediumDict, medium);
    IOLog("medium  add good: %u\n", result);
    medium->release();
    
    publishMediumDictionary(mediumDict);

    
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
    
    
  } while (false);
  
  if (pciNub != NULL) {
    pciNub->close(this);
  }

  bool result = started;
  
  result = attachInterface((IONetworkInterface **)&ethInterface);
  setLinkStatus(kIONetworkLinkValid);
  
  return result;
}

IOReturn AzulNX2Ethernet::getHardwareAddress(IOEthernetAddress *address) {
  memcpy (address, &ethAddress, kIOEthernetAddressSize);
  return kIOReturnSuccess;
}

void AzulNX2Ethernet::interruptOccurred(IOInterruptEventSource *source, int count) {
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM | NX2_PCICFG_INT_ACK_CMD_MASK_INT);
  
  status_block_t *sts = (status_block_t*)stsBlockData;
  
  
 // UInt32 *hcsMem32 = (UInt32*)stsBlockData;
  
  //IOLog("INT\n");
 // IOLog("INT status %X ack %X, %X time %X IDX %X\n", hcsMem32[0], hcsMem32[1], hcsMem32[8], (((uint8_t*)stsBlockData)[0x34]), hcsMem32[13]);
  
  IOLog("INT status %X (%X) index %u\n", sts->attnBits, sts->attnBitsAck, sts->index);
  
  if ((sts->attnBits & STATUS_ATTN_BITS_LINK_STATE) != (sts->attnBitsAck & STATUS_ATTN_BITS_LINK_STATE)) {
    bool newLink = sts->attnBits & STATUS_ATTN_BITS_LINK_STATE;
    bool oldLink = sts->attnBitsAck & STATUS_ATTN_BITS_LINK_STATE;
    
    if (newLink) {
      writeReg32(NX2_PCICFG_STATUS_BIT_SET_CMD, STATUS_ATTN_BITS_LINK_STATE);
      setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive);
      IOLog("Link UP\n");
    } else {
      writeReg32(NX2_PCICFG_STATUS_BIT_CLEAR_CMD, STATUS_ATTN_BITS_LINK_STATE);
      setLinkStatus(kIONetworkLinkValid);
      IOLog("Link DOWN\n");
    }
    
    writeReg32(NX2_EMAC_STATUS, NX2_EMAC_STATUS_LINK_CHANGE);
    
    
    writeReg32(NX2_HC_COMMAND, readReg32(NX2_HC_COMMAND) | NX2_HC_COMMAND_COAL_NOW_WO_INT);
    readReg32(NX2_HC_COMMAND);
  }
  
  lastStatusIndex = sts->index;
  enableInterrupts(false);
}
