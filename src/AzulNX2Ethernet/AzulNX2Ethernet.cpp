#include "AzulNX2Ethernet.h"

OSDefineMetaClassAndStructors (AzulNX2Ethernet, super);

bool AzulNX2Ethernet::start(IOService *provider) {
  bool started = false;
  
  IOLog("AzulNX2Ethernet::start\n");
  
  isChip5709 = true; // TMP
  

  isEnabled = false;
  
  
  do {
    if (!super::start(provider)) {
      break;
    }
    
    IOWorkLoop *workloop = IOWorkLoop::workLoop();


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
  
  IOPhysicalSegment   segments[384];
    UInt32              segmentCount;
  
  segmentCount =
        txCursor->getPhysicalSegmentsWithCoalesce(m,
                                                  segments,
                                                  384);
  UInt8 *dd = (UInt8*) mbuf_data(m);
  SYSLOG("Data %X %X %X %X %X %X", dd[0], dd[1], dd[2], dd[3], dd[4], dd[5]);
  
  tx_bd *bd = (tx_bd*)transmitBuffer.buffer;
  bd = &bd[txIndex];
  
  UInt32 bdChecksumFlags;
  UInt16 bdFlags = 0;
  
  getChecksumDemand(m, kChecksumFamilyTCPIP, &bdChecksumFlags);

      if (bdChecksumFlags & kChecksumIP) {
        bdFlags |= TX_BD_FLAGS_IP_CKSUM;
      }

      if (bdChecksumFlags & (kChecksumTCP | kChecksumUDP)) {
        bdFlags |= TX_BD_FLAGS_TCP_UDP_CKSUM;
      }
  
  bd--;
  
  for (int i = 0; i < segmentCount; i++) {
    bd++;
    bd->tx_bd_haddr_hi = segments[i].location >> 32;
    bd->tx_bd_haddr_lo = segments[i].location & 0xFFFFFFFF;
    bd->tx_bd_mss_nbytes = segments[i].length;
    bd->tx_bd_flags = bdFlags;
    
    txIndex++;
    txSeq += bd->tx_bd_mss_nbytes;
    
    if (i == 0) {
      bd->tx_bd_flags |= TX_BD_FLAGS_START;
    }
    
  }

 // bd->tx_bd_mss_nbytes = m->;
  bd->tx_bd_flags |= TX_BD_FLAGS_END;
  
  writeReg16(MB_GET_CID_ADDR(TX_CID) +
             NX2_L2MQ_TX_HOST_BIDX, txIndex);
  writeReg32(MB_GET_CID_ADDR(TX_CID) +
             NX2_L2MQ_TX_HOST_BSEQ, txSeq);
  
  SYSLOG("outputPacket() start");
  return kIOReturnOutputSuccess;
}

IOReturn AzulNX2Ethernet::enable(IONetworkInterface *interface) {
  DBGLOG("enable()");
  
  updatePHYMediaState();
  
  IOOutputQueue *transmitQueue = getOutputQueue();
  transmitQueue->setCapacity(1000);
  transmitQueue->start();
  
 // return super::enable(interface);
  return kIOReturnSuccess;
}

IOReturn AzulNX2Ethernet::getHardwareAddress(IOEthernetAddress *address) {
  memcpy (address, &ethAddress, kIOEthernetAddressSize);
  return kIOReturnSuccess;
}

void AzulNX2Ethernet::interruptOccurred(IOInterruptEventSource *source, int count) {
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM | NX2_PCICFG_INT_ACK_CMD_MASK_INT);
  
  status_block_t *sts = (status_block_t*)statusBuffer.buffer;
  
  
 // UInt32 *hcsMem32 = (UInt32*)stsBlockData;
  
  //IOLog("INT\n");
 // IOLog("INT status %X ack %X, %X time %X IDX %X\n", hcsMem32[0], hcsMem32[1], hcsMem32[8], (((uint8_t*)stsBlockData)[0x34]), hcsMem32[13]);
  
  SYSLOG("INT status %X (%X) index %u", sts->attnBits, sts->attnBitsAck, sts->index);
  //SYSLOG("TXP PC %X", readRegIndr32(NX2_TXP_CPU_PROGRAM_COUNTER));
  //SYSLOG("TXP %X %X", readReg32(NX2_TXP_CPU_STATE), readReg32(NX2_TXP_CPU_EVENT_MASK));
  
  if ((sts->attnBits & STATUS_ATTN_BITS_LINK_STATE) != (sts->attnBitsAck & STATUS_ATTN_BITS_LINK_STATE)) {
    handlePHYInterrupt(sts);
  }
  
  lastStatusIndex = sts->index;
  enableInterrupts(false);
}
