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

	//if (p == TMS && val == 0) BSRR_TMS = (TMS_Pin << 16);
	//if (p == TMS && val == 1) BSRR_TMS = (TMS_Pin);
	//if (p == TDI && val == 0) BSRR_TDI = (TDI_Pin << 16);
	//if (p == TDI && val == 1) BSRR_TDI = (TDI_Pin);
//
	///* clock TMS and TDI on falling TCK */
	//if (p == TCK) {
	//	if (val == 0) {
	//		XSVF_GPIO_BSRR = BSRR_TMS | BSRR_TDI | (TCK_Pin << 16);
	//	} else {
	//		XSVF_GPIO_BSRR = TCK_Pin;
	//	}
	//}
}

void pulse_clock(void){
	set_port(TCK,0);
	set_port(TCK,1);
	set_port(TCK,0);
}

/* Wait at least the specified number of microsec. */
void delay(uint32_t microsec){
//	_delay_ms(microsec>>12);
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
int read_byte(uint8_t *data){
	return 0;
}
int read_bytes(uint8_t *data, int len){
	return 0;
}
int read_word(uint16_t *data){
	return 0;
}
int read_long(uint32_t *data){
	return 0;
}
void fail(void){
}

void success(void){
}

/* parse the xsvf file and pump the bits */
int xsvf_main(void){
	uint8_t inst; /* instruction */
	uint8_t length; /* hold the length of the arguments to read in */
	while (1)
	{
		//inst = read_next_instr();

		switch (inst) {
		case XCOMPLETE:
			break;

		case XTDOMASK:
			READ_TDO_MASK();
			break;

		case XREPEAT:
			read_byte(&repeat);
			break;

		case XRUNTEST:
			read_long(&run_test);
			break;

		case XSIR:
			read_byte(&length);
			read_bytes(tdi_value,BYTES(length));
			state_goto(STATE_SHIFT_IR);
			shift(SDR_END, tdi_value, 0, length);
			state_goto(STATE_RTI);
			break;

		case XSDR:
			READ_TDI_VALUE();
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRSIZE:
			read_long(&sdr_size);
			break;

		case XSDRTDO:
			READ_TDI_VALUE();
			READ_TDO_EXPECTED();
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRB:
			READ_TDI_VALUE();
			sdr(SDR_BEGIN|SDR_NOCHECK);
			break;

		case XSDRC:
			READ_TDI_VALUE();
			sdr(SDR_CONTINUE|SDR_NOCHECK);
			break;

		case XSDRE:
			READ_TDI_VALUE();

			sdr(SDR_END|SDR_NOCHECK);
			break;

		case XSDRTDOB:
			READ_TDI_VALUE();
			READ_TDO_EXPECTED();
			if (sdr(SDR_BEGIN|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRTDOC:
			READ_TDI_VALUE();
			READ_TDO_EXPECTED();
			if (sdr(SDR_CONTINUE|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSDRTDOE:
			READ_TDI_VALUE();
			READ_TDO_EXPECTED();
			if (sdr(SDR_END|SDR_CHECK)) {
				fail();
				return 0;
			}
			break;

		case XSETSDRMASKS:
			read_bytes(address_mask,BYTES(sdr_size));
			read_bytes(data_mask,BYTES(sdr_size));
			break;

		case XSDRINC:
			fail();
			return 0;
			break;

		case XSTATE:
			read_byte(&inst);
			state_goto(inst);
			break;

		default:
			fail();
			return 0;
		}
	}
}

void write_xsvf(uint8_t * buf){
	//   1     2     3     4   
	// state count byte0 byte1 ... 
chprintf(dbg, "XSVF (C): cnt: %03d, data: %02X, %02X, %02X, %02X\r\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
}

void xsvf_init(void){
  palSetLineMode(TDO, PAL_MODE_INPUT);
  TDI_IDLE;
  TMS_IDLE;
  TCK_IDLE;
  palSetLineMode(TDI, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TMS, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TCK, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);

}