#include "AzulNX2Ethernet.h"


bool AzulNX2Ethernet::start (IOService *provider) {
  bool result = super::start (provider);
  
  IOLog("AzulNX2Ethernet::start\n");
  
  return result;
}
