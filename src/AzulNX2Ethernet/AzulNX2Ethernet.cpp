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
  
  return result;
}

IOReturn AzulNX2Ethernet::getHardwareAddress(IOEthernetAddress *address) {
  return kIOReturnUnsupported;
}
