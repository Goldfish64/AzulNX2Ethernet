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
  
  SYSLOG("rev %X", NX2_CHIP_ID);
  
  probePHY();
  createMediumDictionary();

  
  enableInterrupts(true);
 // setLinkStatus(kIONetworkLinkValid);
  
  return result;
}

const OSString* AzulNX2Ethernet::newVendorString() const {
  return OSString::withCString(getDeviceVendor());
}

const OSString* AzulNX2Ethernet::newModelString() const {
  return OSString::withCString(getDeviceModel());
}

IOReturn AzulNX2Ethernet::enable(IONetworkInterface *interface) {
  DBGLOG("enable()");
  
  updatePHYMediaState();
  
  return super::enable(interface);
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
  
  SYSLOG("INT status %X (%X) index %u", sts->attnBits, sts->attnBitsAck, sts->index);
  
  if ((sts->attnBits & STATUS_ATTN_BITS_LINK_STATE) != (sts->attnBitsAck & STATUS_ATTN_BITS_LINK_STATE)) {
    handlePHYInterrupt(sts);
  }
  
  lastStatusIndex = sts->index;
  enableInterrupts(false);
}
