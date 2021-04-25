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
#include "PHY.h"
#include "HwBuffers.h"

#define super IOEthernetController

#define SYSLOG(str, ...) logPrint(__FUNCTION__, str, ## __VA_ARGS__)
#define DBGLOG(str, ...) logPrint(__FUNCTION__, str, ## __VA_ARGS__)

#define IORETURN_ERR(a)  (a != kIOReturnSuccess)

#define DMA_BITS_40           ((1ULL << 40) - 1)
#define DMA_BITS_64           (~0ULL)

#define ADDR_LO(a)            ((a) & 0xFFFFFFFF)
#define ADDR_HI(a)            ((a) >> 32)

typedef struct {
  IOBufferMemoryDescriptor  *bufDesc;
  IODMACommand              *dmaCmd;
  mach_vm_address_t         physAddr;
  void                      *buffer;
  size_t                    size;
} azul_nx2_dma_buf_t;

class AzulNX2Ethernet : public IOEthernetController {
  OSDeclareDefaultStructors(AzulNX2Ethernet);
  
private:
  IOPCIDevice                 *pciNub;
  IOMemoryMap                 *baseMemoryMap;
  volatile void               *baseAddr;
  bool                        isEnabled;
  
  UInt16                      pciVendorId;
  UInt16                      pciDeviceId;
  UInt16                      pciSubVendorId;
  UInt16                      pciSubDeviceId;
  UInt32                      chipId;
  
  IOWorkLoop                  *workLoop;
  IOInterruptEventSource      *interruptSource;

  OSDictionary                *mediumDict;
  UInt32                      currentMediumIndex;
  phy_media_state_t           mediaState;
  
  IOEthernetInterface         *ethInterface;
  IOEthernetAddress           ethAddress;
  
  UInt32                      shMemBase;
  UInt16                      phyAddress;
  
  azul_nx2_dma_buf_t          statusBuffer;
  azul_nx2_dma_buf_t          statsBuffer;
  azul_nx2_dma_buf_t          contextBuffer;
  azul_nx2_dma_buf_t          txBuffer;
  azul_nx2_dma_buf_t          rxBuffer;
  
  status_block_t              *statusBlock;

  
  tx_bd_t                     *txChain;
  UInt16                      txProd;
  UInt16                      txCons;
  UInt32                      txProdBufferSize;
  UInt16                      txFreeDescriptors;
  mbuf_t                      txPackets[TX_USABLE_BD_COUNT];
  IOOutputQueue               *txQueue;
  IOMbufNaturalMemoryCursor   *txCursor;
  
  rx_bd_t                     *rxChain;
  UInt16                      rxProd;
  UInt16                      rxCons;
  UInt32                      rxProdBufferSize;
  UInt16                      rxFreeDescriptors;
  mbuf_t                      rxPackets[RX_USABLE_BD_COUNT];
  UInt32                      rxPacketLengths[RX_USABLE_BD_COUNT];
  IOOutputQueue               *rxQueue;
  IOMbufNaturalMemoryCursor   *rxCursor;

  
  
  UInt16                      fwSyncSeq = 0;
  

  const nx2_mips_fw_file_t    *firmwareMips;
  const nx2_rv2p_fw_file_t    *firmwareRv2p;
  
  UInt16                      lastStatusIndex = 0;
  
  void logPrint(const char *func, const char *format, ...);
  
  UInt16 readReg16(UInt32 offset);
  UInt32 readReg32(UInt32 offset);
  UInt32 readRegIndr32(UInt32 offset);
  UInt32 readShMem32(UInt32 offset);
  UInt32 readContext32(UInt32 cid, UInt32 offset);
  
  void writeReg16(UInt32 offset, UInt16 value);
  void writeReg32(UInt32 offset, UInt32 value);
  void writeRegIndr32(UInt32 offset, UInt32 value);
  void writeShMem32(UInt32 offset, UInt32 value);
  void writeContext32(UInt32 cid, UInt32 offset, UInt32 value);
  
  bool initEventSources(IOService *provider);
  
  const char *getDeviceVendor() const;
  const char *getDeviceModel() const;
  
  void enableInterrupts(bool coalNow);
  void disableInterrupts();
  
  bool allocDmaBuffer(azul_nx2_dma_buf_t *dmaBuf, size_t size, UInt32 alignment);
  void freeDmaBuffer(azul_nx2_dma_buf_t *dmaBuf);
  
  bool firmwareSync(UInt32 msgData);
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
  
  //
  // Controller
  //
  bool prepareController();
  bool resetController(UInt32 resetCode);
  bool initControllerChip();
  bool initTransmitBuffers();
  
  //
  // PHY-related
  //
  IOReturn readPhyReg16(UInt8 offset, UInt16 *value);
  IOReturn writePhyReg16(UInt8 offset, UInt16 value);
  bool probePHY();
  IOReturn resetPHY();
  IOReturn enablePHYLoopback();
  IOReturn enablePHYAutoMDIX();
  IOReturn enablePHYAutoNegotiation();
  
  void addNetworkMedium(UInt32 index, UInt32 type, UInt32 speed);
  void createMediumDictionary();
  void updatePHYMediaState();
  void fetchMacAddress();
  void handlePHYInterrupt(status_block_t *stsBlock);
  
  
  //
  // Transmit/receive
  //
  void initTxRxRegs();
  bool initTxRing();
  void freeTxRing();
  UInt16 readTxCons();
  UInt32 sendTxPacket(mbuf_t packet);
  void handleTxInterrupt(UInt16 txConsIndexNew);
  
  void initRxRegs();
  bool initRxRing();
  bool initRxDescriptor(UInt16 index);
  void freeRxRing();
  UInt16 readRxCons();
  void handleRxInterrupt(UInt16 rxConsIndexNew);
  
  void interruptOccurred(IOInterruptEventSource *source, int count);
  
public:
  //
  // IOService methods.
  //
  virtual bool start(IOService *provider);
  virtual IOWorkLoop *getWorkLoop() const;
  

  //
  // IONetworkController methods.
  //
  virtual bool createWorkLoop();
  virtual IOOutputQueue *createOutputQueue();
  
  virtual UInt32 outputPacket(mbuf_t m, void *param);
  
  
  virtual const OSString *newVendorString() const;
  virtual const OSString *newModelString() const;
 // virtual const OSString *newRevisionString() const;

  
  //
  // IOEthernetController methods.
  //
  virtual IOReturn enable(IONetworkInterface *interface);
  virtual IOReturn getHardwareAddress(IOEthernetAddress *address);
  
  virtual IOReturn getMaxPacketSize(UInt32 *maxSize) const;
  
  
};

#endif
