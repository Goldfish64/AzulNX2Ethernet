#ifndef __AZUL_NX2_ETHERNET_H__
#define __AZUL_NX2_ETHERNET_H__

#include <IOKit/IODMACommand.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/network/IONetworkInterface.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkStats.h>
#include <IOKit/network/IOEthernetStats.h>
#include <IOKit/network/IOMbufMemoryCursor.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOBasicOutputQueue.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "Registers.h"

#define super IOEthernetController

class AzulNX2Ethernet : public IOEthernetController {
  OSDeclareDefaultStructors (AzulNX2Ethernet);
  
private:
  IOPCIDevice                 *pciNub;
  IOMemoryMap                 *baseMemoryMap;
  volatile void               *baseAddr;
  
  IOWorkLoop                  *workloop;
  IOInterruptEventSource      *intSource;
  
  IOEthernetInterface         *ethInterface;
  IOEthernetAddress           *ethAddress;
  
  UInt32                      shMemBase;
  
  
  UInt32 readReg32 (UInt32 offset);
  UInt32 readRegIndr32 (UInt32 offset);
  UInt32 readShMem32 (UInt32 offset);
  UInt32 readContext32 (UInt32 offset);
  
  void writeReg32 (UInt32 offset, UInt32 value);
  void writeRegIndr32 (UInt32 offset, UInt32 value);
  void writeShMem32 (UInt32 offset, UInt32 value);
  void writeContext32 (UInt32 offset, UInt32 value);
  
public:
  virtual bool start (IOService *provider);
  
  

  
  
};

#endif
