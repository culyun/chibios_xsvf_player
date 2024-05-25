/*
 * xsvf.c
 *
 *  Created on: May 24, 2024
 *      Author: rob
 */

#include "xsvf.h"
#include "chprintf.h"
extern BaseSequentialStream *const dbg;

/* high nibble: next state if TMS=1, low nibble: next step if TMS=0 */
const uint8_t tms_transitions[] = {
	0x01,	/* STATE_TLR		*/
	0x21,	/* STATE_RTI		*/
	0x93,	/* STATE_SELECT_DR_SCAN	*/
	0x54,	/* STATE_CAPTURE_DR	*/
	0x54,	/* STATE_SHIFT_DR	*/
	0x86,	/* STATE_EXIT1_DR	*/
	0x76,	/* STATE_PAUSE_DR	*/
	0x84,	/* STATE_EXIT2_DR	*/
	0x21,	/* STATE_UPDATE_DR	*/
	0x0a,	/* STATE_SELECT_IR_SCAN	*/
	0xcb,	/* STATE_CAPTURE_IR	*/
	0xcb,	/* STATE_SHIFT_IR	*/
	0xfd,	/* STATE_EXIT1_IR	*/
	0xed,	/* STATE_PAUSE_IR	*/
	0xfb,	/* STATE_EXIT2_IR	*/
	0x21,	/* STATE_UPDATE_IR	*/
};

/* bit 0: TMS to get to state 0 ... bit 15: value of TMS to get to state 15 */	
const uint16_t tms_map[] = {
	0x0000,	/* STATE_TLR		*/
	0xfffd,	/* STATE_RTI		*/
	0xfe03,	/* STATE_SELECT_DR_SCAN	*/
	0xffe7,	/* STATE_CAPTURE_DR	*/
	0xffef,	/* STATE_SHIFT_DR	*/
	0xff0f,	/* STATE_EXIT1_DR	*/
	0xffbf,	/* STATE_PAUSE_DR	*/
	0xff0f,	/* STATE_EXIT2_DR	*/
	0xfefd,	/* STATE_UPDATE_DR	*/
	0x01ff,	/* STATE_SELECT_IR_SCAN	*/
	0xf3ff,	/* STATE_CAPTURE_IR	*/
	0xf7ff,	/* STATE_SHIFT_IR	*/
	0x87ff,	/* STATE_EXIT1_IR	*/
	0xdfff,	/* STATE_PAUSE_IR	*/
	0x87ff,	/* STATE_EXIT2_IR	*/
	0x7ffd,	/* STATE_UPDATE_IR	*/
};

uint8_t current_state = 0;
#define MAX_SIZE 0x20

uint8_t repeat;
uint32_t sdr_size;
uint32_t run_test;

uint8_t address_mask[MAX_SIZE];
uint8_t data_mask[MAX_SIZE];
uint8_t tdi_value[MAX_SIZE];
uint8_t tdo_mask[MAX_SIZE];
uint8_t tdo_expected[MAX_SIZE];

void set_port(uint8_t p, uint8_t val){
	static uint32_t BSRR_TMS, BSRR_TDI;

	if (p == TMS && val == 0) BSRR_TMS = ((1 << TMS_Pin) << 16);
	if (p == TMS && val == 1) BSRR_TMS = ((1 << TMS_Pin));
	if (p == TDI && val == 0) BSRR_TDI = ((1 << TDI_Pin) << 16);
	if (p == TDI && val == 1) BSRR_TDI = ((1 << TDI_Pin));

	/* clock TMS and TDI on falling TCK */
	if (p == TCK) {
		if (val == 0) {
			//chprintf(dbg, "Clock Low\r\n");
			XSVF_GPIO_BSRR = BSRR_TMS | BSRR_TDI | ((1 << TCK_Pin) << 16);
		} else {
			//chprintf(dbg, "Clock Hi\r\n");
			XSVF_GPIO_BSRR = (1 << TCK_Pin);
		}
	}
}

void pulse_clock(void){
	set_port(TCK,0);
	set_port(TCK,1);
	set_port(TCK,0);
}

void wait_nops(void){
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

/* Wait at least the specified number of microsec. */
void delay(uint32_t microsec){
//	_delay_ms(microsec>>12);
	wait_nops();
	set_port(TCK,0);
	while (--microsec > 0) {
		set_port(TCK,1);
		set_port(TCK,0);
	}
}

uint8_t read_tdo(void){
	return (palReadLine(TDO_PIN) == PAL_HIGH) ? 1 : 0 ;
}

void set_state(uint8_t state){
	current_state = state;
}

void state_ack(uint8_t tms){
	if (tms==0) {
		current_state = tms_transitions[current_state]&0xf;
	} else {
		current_state = (tms_transitions[current_state]>>4)&0xf;
	}
}

void state_step(uint8_t tms){
	set_port(TMS,tms);
	pulse_clock();
	state_ack(tms);
}

void state_goto(uint8_t state){
	if (state==STATE_TLR) {
		uint8_t i;
		for (i=0;i<5;i++) {
			state_step(1);
		}

	} else {
		while (current_state != state) {
			uint8_t tms = (tms_map[current_state]>>state) & 1;
			state_step(tms);
		}
	}
}

/* output dataVal onto the TDI ports; store the TDO value returned */
static void shift(int flags, uint8_t *data, uint8_t *tdo, uint32_t length){
	int i,j;
	int n_bytes = BYTES(length);

	for (i=0; i<n_bytes; i++) {
		uint8_t byte = data[i];
		uint8_t in = 0;
		for (j=0;j<8;j++) {
			/* on the last bit, set TMS to 1 so that we go to the EXIT state */
			if ((length==1) && (flags&SDR_END)) {
				set_port(TMS,1);
				state_ack(1);
			}
			if (length>0) {
				if (tdo) {
					in |= read_tdo()<<j;
				}
				set_port(TDI, byte&1);
				byte >>= 1;

				pulse_clock();
				length--;
			}
		}
		if (tdo)
			tdo[i] = in;
	}
}

static int sdr(int flags){
	int failTimes=0;
	uint8_t tdo_actual[MAX_SIZE];

	if (flags&SDR_BEGIN) {
		state_goto(STATE_SHIFT_DR);
	}

	/* data processing loop */
	while (1)
	{

		shift(flags, tdi_value, tdo_actual, sdr_size);

		if (flags&SDR_CHECK) {
			int i;
			int equal = 1;

			for (i=0; i<BYTES(sdr_size); i++) {
				uint8_t expected,actual;
				expected = tdo_expected[i] & tdo_mask[i];
				actual = tdo_actual[i] & tdo_mask[i];
				if (expected!=actual) {
					equal = 0;
					break;
				}
			}

			/* compare the TDO value against the expected TDO value */
			if (equal) {
				/* TDO matched what was expected */
				break;
			} else {
				/* TDO did not match the value expected */
				failTimes++;
				/* update failure count */
				if (failTimes>repeat) {
					return 1;
				}
				/* ISP failed */
				state_step(0); /* Pause-DR state */
				state_step(1); /* Exit2-DR state */
				state_step(0); /* Shift-DR state */
				state_step(1); /* Exit1-DR state */

				state_goto(STATE_RTI);
				delay(run_test);
				state_goto(STATE_SHIFT_DR);
			}
		} else {
			/* No TDO check - exit */
			break;
		}

	}
	if (flags&SDR_END) {
		state_goto(STATE_RTI);
	}
	
	delay(run_test);
	return 0;
}
void read_byte(uint8_t *data, uint8_t *buf){
	*data = *buf;
}

uint8_t read_bytes(uint8_t *data, uint8_t *buf, int len){
	uint8_t i=0;
	for (i=0; i < len; i++){
		data[i] = *(buf++);
	}
	return len;
}

uint8_t read_long(uint32_t *data, uint8_t *buf){
	uint32_t temp = *(buf++) * 16777216;
	temp += *(buf++) * 65536;
	temp += *(buf++) * 256;
	temp += *(buf++);
	*data = temp;
	return 4;
}

void fail(void){
		chprintf(dbg, "---------FAIL!\r\n");
}

uint16_t write_xsvf(uint16_t len, uint8_t * buf){
	uint16_t i=0; // Counter variable
	uint8_t length; /* hold the length of the arguments to read in */
	uint8_t inst; /* instruction */
	uint8_t temp, tlen;
	uint32_t temp32;
	static uint16_t sdr_bytes;
	// len is the total length of the xsvf file

	chprintf(dbg, "XSVF: Length: %d\r\n", len);
	while (i < len){
		chprintf(dbg, "%02X ", buf[i]);
		switch (buf[i++]) {

		case XCOMPLETE:
			chprintf(dbg, "Complete. %d\r\n", i);
			break;

		case XTDOMASK:
			i += read_bytes(tdo_mask, &(buf[i]), sdr_bytes);
			//chprintf(dbg, "Set TDOMASK to %02X %02X %02X %02X\r\n", tdo_mask[0], tdo_mask[1], tdo_mask[2], tdo_mask[3]);
			break;

		case XREPEAT:
			read_byte(&repeat, &(buf[i++]));
			//chprintf(dbg, "Set REPEAT to %02X\r\n", repeat);
			break;

		case XRUNTEST:
			i += read_long(&run_test, &(buf[i]));
			//chprintf(dbg, "Set RUNTEST to %08X\r\n", run_test);
			break;

		case XSIR:
			read_byte(&temp, &(buf[i++]));
			tlen = (temp>>3);
			chprintf(dbg, "XSIR Read %d Bytes\r\n", tlen);
			i += read_bytes(tdi_value, &(buf[i]), tlen);
			chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			state_goto(STATE_SHIFT_IR);
			shift(SDR_END, tdi_value, 0, temp);
			state_goto(STATE_RTI);
			break;

		case XSDR:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRSIZE:
			i += read_long(&temp32, &(buf[i]));
			sdr_bytes = temp32;
			sdr_bytes = (sdr_bytes+7)>>3;
			chprintf(dbg, "Set XDRSIZE to %04X or %04X\r\n", temp32, sdr_bytes);
			break;

		case XSDRTDO:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			i += read_bytes(tdo_expected, &(buf[i]), sdr_bytes);
			chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			chprintf(dbg, "Set TDOEXP to %02X %02X %02X %02X\r\n", tdo_expected[0], tdo_expected[1], tdo_expected[2], tdo_expected[3]);
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRB:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			sdr(SDR_BEGIN|SDR_NOCHECK);
			break;

		case XSDRC:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			sdr(SDR_CONTINUE|SDR_NOCHECK);
			break;

		case XSDRE:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			sdr(SDR_END|SDR_NOCHECK);
			break;

		case XSDRTDOB:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			i += read_bytes(tdo_expected, &(buf[i]), sdr_bytes);
			if (sdr(SDR_BEGIN|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRTDOC:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			i += read_bytes(tdo_expected, &(buf[i]), sdr_bytes);
			if (sdr(SDR_CONTINUE|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRTDOE:
			i += read_bytes(tdi_value, &(buf[i]), sdr_bytes);
			i += read_bytes(tdo_expected, &(buf[i]), sdr_bytes);
			if (sdr(SDR_END|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSETSDRMASKS:
			i += read_bytes(address_mask, &(buf[i]), sdr_bytes);
			i += read_bytes(data_mask, &(buf[i]), sdr_bytes);
			break;

		case XSDRINC:
			fail();
			return 0;
			break;

		case XSTATE:
			read_byte(&inst, &(buf[i++]));
			//chprintf(dbg, "Goto STATE: %02X\r\n", inst);
			state_goto(inst);
			break;

		default:
			fail();
			return 0;
		}

	}
	return 1;
	//chprintf(dbg, "\r\n");
}

void xsvf_init(void){
  palSetLineMode(TDO, PAL_MODE_INPUT);
  TDI_IDLE;
  TMS_IDLE;
  TCK_IDLE;
  palSetLineMode(TDI_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TMS_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TCK_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);

}