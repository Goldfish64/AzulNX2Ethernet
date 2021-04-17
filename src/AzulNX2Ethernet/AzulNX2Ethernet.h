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
  OSDictionary                *mediumDict;
  
  IOEthernetInterface         *ethInterface;
  IOEthernetAddress           ethAddress;
  
  bool                        isChip5709;
  UInt32                      shMemBase;
  
  IOBufferMemoryDescriptor    *stsBlockDesc;
  IODMACommand                *stsBlockCmd;
  IODMACommand::Segment64     stsBlockSeg;
  void                        *stsBlockData;
  IOBufferMemoryDescriptor    *statsBlockDesc;
  IODMACommand                *statsBlockCmd;
  IODMACommand::Segment64     statsBlockSeg;
  void                        *statsBlockData;
  IOBufferMemoryDescriptor    *ctxBlockDesc;
  IODMACommand                *ctxBlockCmd;
  IODMACommand::Segment64     ctxBlockSeg;
  void                        *ctxBlockData;
  IOBufferMemoryDescriptor    *txBlockDesc;
  IODMACommand                *txBlockCmd;
  IODMACommand::Segment64     txBlockSeg;
  void                        *txBlockData;
  IOBufferMemoryDescriptor    *rxBlockDesc;
  IODMACommand                *rxBlockCmd;
  IODMACommand::Segment64     rxBlockSeg;
  void                        *rxBlockData;
  
  
  UInt16                      fwSyncSeq = 0;
  
  const UInt8                 *firmwareRv2p;
  const UInt8                 *firmwareMips;
  
  UInt16                      lastStatusIndex = 0;
  
  
  UInt32 readReg32(UInt32 offset);
  UInt32 readRegIndr32(UInt32 offset);
  UInt32 readShMem32(UInt32 offset);
  UInt32 readContext32(UInt32 offset);
  
  void writeReg16(UInt32 offset, UInt16 value);
  void writeReg32(UInt32 offset, UInt32 value);
  void writeRegIndr32(UInt32 offset, UInt32 value);
  void writeShMem32(UInt32 offset, UInt32 value);
  void writeContext32(UInt32 cid, UInt32 offset, UInt32 value);
  
  void enableInterrupts(bool coalNow);
  void disableInterrupts();
  
  bool allocMemory();
  bool firmwareSync(UInt32 msgData);
  void fetchMacAddress();
  bool initContext();
  void initCpus();
  
  UInt32 processRv2pFixup(UInt32 rv2pProc, UInt32 index, UInt32 fixup, UInt32 rv2pCode);
  void loadRv2pFirmware(UInt32 rv2pProcessor, const nx2_rv2p_fw_file_entry_t *rv2pEntry);
  void loadCpuFirmware(const cpu_reg_t *cpuReg, const nx2_mips_fw_file_entry_t *mipsEntry);
  void startCpu(const cpu_reg_t *cpuReg);
  void stopCpu(const cpu_reg_t *cpuReg);
  void initCpuRxp();
  void initCpuTxp();
  void initCpuTpat();
  void initCpuCom();
  void initCpuCp();
  
  bool prepareController();
  bool resetController(UInt32 resetCode);
  bool initControllerChip();
  
  void interruptOccurred(IOInterruptEventSource *source, int count);
  
public:
  virtual bool start(IOService *provider);
  
  virtual IOReturn getHardwareAddress(IOEthernetAddress *address);
  
  

  
  
};

#endif
