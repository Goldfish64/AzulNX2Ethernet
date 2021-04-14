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

#include "FirmwareStructs.h"
#include "Registers.h"

#define super IOEthernetController

class AzulNX2Ethernet : public IOEthernetController {
  OSDeclareDefaultStructors(AzulNX2Ethernet);
  
private:
  IOPCIDevice                 *pciNub;
  IOMemoryMap                 *baseMemoryMap;
  volatile void               *baseAddr;
  
  IOWorkLoop                  *workloop;
  IOInterruptEventSource      *intSource;
  
  IOEthernetInterface         *ethInterface;
  IOEthernetAddress           *ethAddress;
  
  bool                        isChip5709;
  UInt32                      shMemBase;
  
  IOBufferMemoryDescriptor    *stsBlockDesc;
  IODMACommand                *stsBlockCmd;
  void                        *stsBlockData;
  IOBufferMemoryDescriptor    *statsBlockDesc;
  IODMACommand                *statsBlockCmd;
  void                        *statsBlockData;
  IOBufferMemoryDescriptor    *ctxBlockDesc;
  IODMACommand                *ctxBlockCmd;
  IODMACommand::Segment64     ctxBlockSeg;
  void                        *ctxBlockData;
  
  
  UInt16                      fwSyncSeq = 0;
  
  const UInt8                 *firmwareRv2p;
  const UInt8                 *firmwareMips;
  
  
  UInt32 readReg32(UInt32 offset);
  UInt32 readRegIndr32(UInt32 offset);
  UInt32 readShMem32(UInt32 offset);
  UInt32 readContext32(UInt32 offset);
  
  void writeReg32(UInt32 offset, UInt32 value);
  void writeRegIndr32(UInt32 offset, UInt32 value);
  void writeShMem32(UInt32 offset, UInt32 value);
  void writeContext32(UInt32 offset, UInt32 value);
  
  bool allocMemory();
  bool firmwareSync(UInt32 msgData);
  bool initContext();
  bool initCpus();
  
  UInt32 processRv2pFixup(UInt32 rv2pProc, UInt32 index, UInt32 fixup, UInt32 rv2pCode);
  void loadRv2pFirmware(const nx2_rv2p_fw_file_entry_t *rv2p, UInt32 rv2pProcessor);
  bool loadCpuFirmware();
  
  
  bool prepareController();
  bool resetController(UInt32 resetCode);
  bool initControllerChip();
  
public:
  virtual bool start(IOService *provider);
  
  virtual IOReturn getHardwareAddress(IOEthernetAddress *address);
  
  

  
  
};

#endif
