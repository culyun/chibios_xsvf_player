/*
 * xsvf.h
 *
 *  Created on: May 24, 2024
 *      Author: rob
 */

#ifndef USERLIB_INCLUDE_XSVF_H_
#define USERLIB_INCLUDE_XSVF_H_
#include "ch.h"
#include "hal.h"

#define TMS 0x10
#define TDO 0x20
#define TDI 0x40
#define TCK 0x80

#define TDI_Pin    13U
#define TCK_Pin    14U
#define TMS_Pin    15U
#define TDI_PIN    PAL_LINE(GPIOC, TDI_Pin) // Output
#define TDO_PIN    PAL_LINE(GPIOB, 0U) // Input
#define TCK_PIN    PAL_LINE(GPIOC, TCK_Pin) // Output
#define TMS_PIN    PAL_LINE(GPIOC, TMS_Pin) // Output
#define XSVF_GPIO_BSRR (GPIOC->BSRR.W)
#define TDI_IDLE   palClearLine  (TDI_PIN)
#define TDI_ACTIVE palSetLine  (TDI_PIN)
#define TMS_IDLE   palClearLine  (TMS_PIN)
#define TMS_ACTIVE palSetLine  (TMS_PIN)
#define TCK_IDLE   palClearLine(TCK_PIN)
#define TCK_ACTIVE palSetLine  (TCK_PIN)

#define STATE_TLR		0x00
#define STATE_RTI		0x01
#define STATE_SELECT_DR_SCAN	0x02
#define STATE_CAPTURE_DR	0x03
#define STATE_SHIFT_DR		0x04
#define STATE_EXIT1_DR		0x05
#define STATE_PAUSE_DR		0x06
#define STATE_EXIT2_DR		0x07
#define STATE_UPDATE_DR		0x08
#define STATE_SELECT_IR_SCAN	0x09
#define STATE_CAPTURE_IR	0x0a
#define STATE_SHIFT_IR		0x0b
#define STATE_EXIT1_IR		0x0c
#define STATE_PAUSE_IR		0x0d
#define STATE_EXIT2_IR		0x0e
#define STATE_UPDATE_IR		0x0f

#define SDR_BEGIN	0x01
#define SDR_END		0x02
#define SDR_CHECK	0x10

#define SDR_NOCHECK	0
#define SDR_CONTINUE	0
#define SDR_FULL	3

/* xsvf instructions */
#define XCOMPLETE	0  // 0
#define XTDOMASK	1  // 1
#define XSIR		2  // 2
#define XSDR		3  // 3
#define XRUNTEST	4  // 4
#define XREPEAT		7  // 7
#define XSDRSIZE	8  // 8
#define XSDRTDO		9  // 9
#define XSETSDRMASKS 10 // A
#define XSDRINC		11 // B
#define XSDRB		12 // C
#define XSDRC		13 // D
#define XSDRE		14 // E
#define XSDRTDOB	15 // F
#define XSDRTDOC	16 // 10
#define XSDRTDOE	17 // 11
#define XSTATE		18 // 12

#define PING		126 /* '~' */ 

/* return number of bytes necessary for "num" bits */
#define BYTES(num) ((int)((num+7)>>3))

uint16_t write_xsvf(uint16_t len, uint8_t * buf);
void xsvf_init(void);

#endif /* USERLIB_INCLUDE_XSVF_H_ */
