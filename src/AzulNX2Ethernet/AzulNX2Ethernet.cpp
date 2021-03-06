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

OSDefineMetaClassAndStructors (AzulNX2Ethernet, super);

bool AzulNX2Ethernet::start(IOService *provider) {
  bool started     = false;
  bool initialized = false;
  
  isEnabled = false;
  
  DBGLOG("Starting driver load");
  
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
      SYSLOG("Controller prep failed!");
      break;
    }
    if (!resetController(NX2_DRV_MSG_CODE_RESET)) {
      SYSLOG("Controller reset failed!");
      break;
    }
    if (!initControllerChip()) {
      SYSLOG("Controller initialization failed!");
      break;
    }
    
    probePHY();
    createMediumDictionary();
    
    initialized = true;
  } while (false);
  
  if (pciNub != NULL) {
    pciNub->close(this);
  }
  
  do {
    if (!initialized) {
      break;
    }
    initialized = false;
    
    //
    // Attach network interface.
    //
    if (!attachInterface((IONetworkInterface **)&ethInterface, false)) {
      break;
    }
    ethInterface->registerService();
    initialized = true;
  } while (false);

  if (started && !initialized) {
    super::stop(provider);
  } else {
    DBGLOG("Controller is started");
  }
  
  return initialized;
}

void AzulNX2Ethernet::stop(IOService *provider) {
  //
  // Stop interrupt sources.
  //
  if (interruptSource != NULL) {
    interruptSource->disable();
    workLoop->removeEventSource(interruptSource);
  }
  
  super::stop(provider);
}

void AzulNX2Ethernet::free() {
  freeDmaBuffer(&statusBuffer);
  freeDmaBuffer(&statsBuffer);
  freeDmaBuffer(&txBuffer);
  freeDmaBuffer(&rxBuffer);
  
  if (NX2_CHIP_NUM == NX2_CHIP_NUM_5709) {
    freeDmaBuffer(&contextBuffer);
  }
  
  super::free();
}

IOWorkLoop* AzulNX2Ethernet::getWorkLoop() const {
  return workLoop;
}

bool AzulNX2Ethernet::createWorkLoop() {
  workLoop = IOWorkLoop::workLoop();
  return workLoop != NULL;
}

IOOutputQueue* AzulNX2Ethernet::createOutputQueue() {
  return IOBasicOutputQueue::withTarget(this, TX_QUEUE_LENGTH);
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
  DBGLOG("Enabling controller...");
  
  bool initialized = false;
  
  do {
    if (isEnabled) {
      DBGLOG("Controller is already enabled!");
      break;
    }
    
    if (!pciNub->open(this)) {
      break;
    }
    
    stopController();
    
    if (!resetController(NX2_DRV_MSG_CODE_RESET)) {
      SYSLOG("Controller reset failed!");
      break;
    }
    if (!initControllerChip()) {
      SYSLOG("Controller initialization failed!");
      break;
    }
    
    startController();
    
    updatePHYMediaState();
    
    txQueue->setCapacity(TX_QUEUE_LENGTH);
    txQueue->start();
    
    isEnabled = true;
    initialized = true;
    DBGLOG("Controller is now enabled");
    
  } while (false);
  
  return initialized ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn AzulNX2Ethernet::getHardwareAddress(IOEthernetAddress *address) {
  memcpy (address, &ethAddress, kIOEthernetAddressSize);
  return kIOReturnSuccess;
}

void AzulNX2Ethernet::interruptOccurred(IOInterruptEventSource *source, int count) {
  if (!isEnabled) {
    return;
  }
  
  writeReg32(NX2_PCICFG_INT_ACK_CMD, NX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM | NX2_PCICFG_INT_ACK_CMD_MASK_INT);
  
  
 // UInt32 *hcsMem32 = (UInt32*)stsBlockData;
  
  //IOLog("INT\n");
 // IOLog("INT status %X ack %X, %X time %X IDX %X\n", hcsMem32[0], hcsMem32[1], hcsMem32[8], (((uint8_t*)stsBlockData)[0x34]), hcsMem32[13]);
  UInt32 i = statusBlock->index;
  if (i % 200 == 0) {
    SYSLOG("INT status %X (%X) index %u tx idx %u rx idx %u", statusBlock->attnBits, statusBlock->attnBitsAck, statusBlock->index, readTxCons(), readRxCons());
  }
  
  
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

IOReturn AzulNX2Ethernet::setMulticastMode(bool active) {
  return kIOReturnUnsupported;
}

IOReturn AzulNX2Ethernet::setMulticastList(IOEthernetAddress *addrs, UInt32 count) {
  return kIOReturnUnsupported;
}

IOReturn AzulNX2Ethernet::setPromiscuousMode(bool active) {
  setRxMode(active);
  return kIOReturnSuccess;
}
