#!/bin/sh

firmwareHeader="FirmwareGenerated.h"
firmwares=../../firmware/*.fw

rm $firmwareHeader

#
# Process firmware files.
#
for firmware in $firmwares; do
  fileName=`basename $firmware`
  fwName=${fileName//./_}
  
  echo "Processing firmware $fileName..."
  
  echo "static const UInt8 $fwName[] = " | tr '-' '_' >>$firmwareHeader
  echo "{" >>$firmwareHeader
  xxd -i <$firmware >>$firmwareHeader
  echo "};" >>$firmwareHeader
  echo "" >>$firmwareHeader
done
