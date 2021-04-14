
#ifndef __FIRMWARE_STRUCTS_H__
#define __FIRMWARE_STRUCTS_H__

typedef struct {
  UInt32  address;
  UInt32  length;
  UInt32  offset;
} nx2_fw_file_section_t;

typedef struct {
  UInt32                  startAddress;
  nx2_fw_file_section_t   text;
  nx2_fw_file_section_t   data;
  nx2_fw_file_section_t   roData;
} nx2_mips_fw_file_entry_t;

typedef struct {
  nx2_fw_file_section_t   rv2p;
  UInt32                  fixups[8];
} nx2_rv2p_fw_file_entry_t;

typedef struct {
  nx2_mips_fw_file_entry_t  com;
  nx2_mips_fw_file_entry_t  cp;
  nx2_mips_fw_file_entry_t  rxp;
  nx2_mips_fw_file_entry_t  tpat;
  nx2_mips_fw_file_entry_t  txp;
} nx2_mips_fw_file_t;

typedef struct {
  nx2_rv2p_fw_file_entry_t  proc1;
  nx2_rv2p_fw_file_entry_t  proc2;
} nx2_rv2p_fw_file_t;

#endif
