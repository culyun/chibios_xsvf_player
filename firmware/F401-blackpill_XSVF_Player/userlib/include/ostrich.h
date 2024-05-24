/*
 * ostrich.h
 *
 *  Created on: Dec 1, 2022
 *      Author: rob
 */

#ifndef USERLIB_INCLUDE_OSTRICH_H_
#define USERLIB_INCLUDE_OSTRICH_H_

typedef enum {
  IDLE = 0,
  VERSION = 1,
  BANK,
  BANK_BR,
  BANK_BRR,
  BANK_BES,  //5
  BANK_BS,
  BANK_BE,
  BANK_BEE,
  BANK_BRn,
  BANK_BSn, //10
  BANK_BEn,
  BAUD,
  BAUD_Sn,
  SERIAL,
  SERIAL_NS,   //15
  WRITE,
  WRITE_nM,
  WRITE_nML,
  WRITE_nMLB,
  WRITE_nMLCs, //20
  READ,
  READ_nM,
  READ_nML,
  READ_nMLCs,
  BULK,           //25
  BULK_ZRn,
  BULK_ZRnB,
  BULK_ZRnBM,
  BULK_ZRnBMCs,
  BULK_ZWn,     //30
  BULK_ZWnB,
  BULK_ZWnBM,
  BULK_ZWnBMB,
  BULK_ZWnBMBCs,
  CONFIG_C,     //35
  CONFIG_Cn,
  CONFIG_CnCs,
  CLOCK_D,
  CLOCK_DW,
  CLOCK_DRCs,  //40
  CLOCK_DWn,
  CLOCK_DWnCs,
  PINS_C,
  PINS_Cn,
  PINS_CnCs,    //45
  XSVF_X,
  XSVF_Xn,
  XSVF_XnCs,
  UNHANDLED
} char_state_t;

void start_ostrich_thread(void);

#endif /* USERLIB_INCLUDE_OSTRICH_H_ */
