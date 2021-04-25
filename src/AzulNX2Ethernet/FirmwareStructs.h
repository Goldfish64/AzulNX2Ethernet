
#ifndef __FIRMWARE_STRUCTS_H__
#define __FIRMWARE_STRUCTS_H__

#define RV2P_PROC1    0
#define RV2P_PROC2    1

#define RV2P_P1_FIXUP_PAGE_SIZE_IDX     0
#define RV2P_BD_PAGE_SIZE_MSK           0xFFFF
#define RV2P_BD_PAGE_SIZE               ((RX_PAGE_SIZE / 16) - 1)

#define RV2P_FIXUP_COUNT                8

typedef struct {
  UInt32  mode;
  UInt32  modeValueHalt;
  UInt32  modeValueSstep;

  UInt32  state;
  UInt32  stateValueClear;

  UInt32  gpr0;
  UInt32  evmask;
  UInt32  pc;
  UInt32  inst;
  UInt32  bp;

  UInt32  spadBase;

  UInt32  mipsViewBase;
} cpu_reg_t;

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
  UInt32                  fixups[RV2P_FIXUP_COUNT];
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
