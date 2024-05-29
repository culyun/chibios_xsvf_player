/*
 * xsvf.c
 *
 *  Created on: May 24, 2024
 *      Author: rob
 */

#include "xsvf.h"
#include "chprintf.h"
extern BaseSequentialStream *const ost;
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

void wait_nops(uint32_t t){
	uint32_t i;
	for (i=0; i<t; i++){
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
    	__NOP();
	}
}

void set_port(uint8_t p, uint8_t val){
	static uint32_t BSRR_TMS, BSRR_TDI;

	if (p == TMS && val == 0) BSRR_TMS = (1 << (TMS_Pin+16));
	if (p == TMS && val == 1) BSRR_TMS = (1 << TMS_Pin);
	if (p == TDI && val == 0) BSRR_TDI = (1 << (TDI_Pin+16));
	if (p == TDI && val == 1) BSRR_TDI = (1 << TDI_Pin);

	/* clock TMS and TDI on falling TCK */
	if (p == TCK) {
		if (val == 0) {
			//chprintf(dbg, "Clock Low\r\n");
			XSVF_GPIO_BSRR = BSRR_TMS | BSRR_TDI | (1 << (TCK_Pin+16));
		} else {
			//chprintf(dbg, "Clock Hi\r\n");
			XSVF_GPIO_BSRR = (1 << TCK_Pin);
		}
	}
}

void pulse_clock(void){
	set_port(TCK,0);
	set_port(TCK,1);
	wait_nops(4);
	set_port(TCK,0);
	wait_nops(4);
}


/* Wait at least the specified number of microsec. */
void delay(int32_t microsec){
//	_delay_ms(microsec>>12);
	//wait_nops();
	set_port(TCK,0);
	//chprintf(dbg, "Port set1\r\n");
	while (--microsec > 0) {
		wait_nops(4);
		set_port(TCK,1);
		//chprintf(dbg, "Port set2\r\n");
		wait_nops(4);
		set_port(TCK,0);
		//chprintf(dbg, "Port set3\r\n");
	}
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
	//chprintf(dbg, "State Goto %02X\r\n", state);
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

uint8_t read_tdo(void){
	return (palReadLine(TDO_PIN) == PAL_HIGH) ? 1 : 0 ;
}

/* output dataVal onto the TDI ports; store the TDO value returned */
static void shift(int flags, uint8_t *data, uint8_t *tdo, uint32_t length){
	int i,j;
	int n_bytes = BYTES(length);

	for (i=0; i<n_bytes; i++){
		//chprintf(dbg, "Shift Byte: %02X\r\n", i);
		uint8_t byte = data[i];
		uint8_t in = 0;
		for (j=0;j<8;j++){
			//chprintf(dbg, "Shift Bit: %02X\r\n", j);
			/* on the last bit, set TMS to 1 so that we go to the EXIT state */
			if ((length==1) && (flags&SDR_END)) {
				set_port(TMS,1);
				state_ack(1);
			}
			if (length>0) {
				if (tdo) {
					in |= read_tdo()<<j;
					//chprintf(dbg, "TDO: %d\r\n", read_tdo());
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
	while (1){

		shift(flags, tdi_value, tdo_actual, sdr_size);

		if (flags&SDR_CHECK){
			int i;
			int equal = 1;

			for (i=0; i<BYTES(sdr_size); i++){
				uint8_t expected,actual;
				expected = tdo_expected[i] & tdo_mask[i];
				actual = tdo_actual[i] & tdo_mask[i];
				//chprintf(dbg, "TDO actual: %02X idx: %d\r\n", tdo_actual[i], i);
				if (expected!=actual) {
					equal = 0;
					break;
				}
			}

			/* compare the TDO value against the expected TDO value */
			if (equal){
				/* TDO matched what was expected */
				//chprintf(dbg, "TDO matched.\r\n");
				break;
			} 
			else{
				/* TDO did not match the value expected */
				//chprintf(dbg, "TDO didn't match.\r\n");
				failTimes++;
				/* update failure count */
				if (failTimes>repeat){
					//chprintf(dbg, "Max. Repeats reached!.\r\n");
					return 1;
				}
				/* ISP failed */
				state_step(0); /* Pause-DR state */
				state_step(1); /* Exit2-DR state */
				state_step(0); /* Shift-DR state */
				state_step(1); /* Exit1-DR state */
				//chprintf(dbg, "Trying again....\r\n");

				state_goto(STATE_RTI);
				//chprintf(dbg, "State ch.1\r\n");
				delay(run_test);
				//chprintf(dbg, "delay1\r\n");
				state_goto(STATE_SHIFT_DR);
				//chprintf(dbg, "State ch.2\r\n");
			}
		} 
		else{
			/* No TDO check - exit */
			break;
		}
	}
	if (flags&SDR_END){
		state_goto(STATE_RTI);
	}

	delay(run_test);
	return 0;
}

void read_byte(uint8_t *data, uint8_t *buf){
	*data = *buf;
}

uint8_t read_bytes(uint8_t *data, uint8_t *buf, int len){
	int8_t i=0;
	for (i=len-1; i>=0; --i){
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

void send_response(uint16_t chunk, uint16_t pos){
	if (pos > chunk) streamPut(ost, 1);          // 10%
	else if (pos > chunk * 2) streamPut(ost, 2); // 20% 
	else if (pos > chunk * 3) streamPut(ost, 3); // 30% 
	else if (pos > chunk * 4) streamPut(ost, 4); // 40% 
	else if (pos > chunk * 5) streamPut(ost, 5); // 50% 
	else if (pos > chunk * 6) streamPut(ost, 6); // 60% 
	else if (pos > chunk * 7) streamPut(ost, 7); // 70% 
	else if (pos > chunk * 8) streamPut(ost, 8); // 80% 
	else if (pos > chunk * 9) streamPut(ost, 9); // 90% 
}

uint16_t write_xsvf(uint16_t len, uint8_t * buf){
	uint16_t i=0; // Counter variable
	uint8_t length; /* hold the length of the arguments to read in */
	uint8_t inst; /* instruction */
	uint32_t temp32;
	//static uint16_t sdr_bytes;
	// len is the total length of the xsvf file
	uint16_t chunk = len / 10;
	//chprintf(dbg, "XSVF: Length: %d\r\n", len);
	while (i < len){
		//chprintf(dbg, "%02X ", buf[i]);
		switch (buf[i++]) {

		case XCOMPLETE: // 00
			chprintf(dbg, "Complete. %d\r\n", i);
			chprintf(ost, "F"); // Done Programming
			break;

		case XTDOMASK: // 01
			i += read_bytes(tdo_mask, &(buf[i]), BYTES(sdr_size));
			// streamPut(ost, 1);
			//chprintf(dbg, "Set TDOMASK to %02X %02X %02X %02X\r\n", tdo_mask[0], tdo_mask[1], tdo_mask[2], tdo_mask[3]);
			break;

		case XREPEAT: // 07
			read_byte(&repeat, &(buf[i++]));
			// streamPut(ost, 7);
			//chprintf(dbg, "Set REPEAT to %02X\r\n", repeat);
			break;

		case XRUNTEST: // 04
			i += read_long(&run_test, &(buf[i]));
			// streamPut(ost, 4);
			//chprintf(dbg, "Set RUNTEST to %08X\r\n", run_test);
			break;

		case XSIR: // 02
			read_byte(&length, &(buf[i++]));
			//chprintf(dbg, "XSIR Read %d Bytes\r\n", BYTES(length));
			i += read_bytes(tdi_value, &(buf[i]), BYTES(length));
			//chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			state_goto(STATE_SHIFT_IR);
			shift(SDR_END, tdi_value, 0, length);
			state_goto(STATE_RTI);
			// streamPut(ost, 2);
			break;

		case XSDR: // 03
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			//chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			// streamPut(ost, 3);
			break;

		case XSDRSIZE: // 08
			i += read_long(&sdr_size, &(buf[i]));
			//sdr_size = temp32;
			//sdr_size = (sdr_size+7)>>3; // The +7 should be useless since 7>>3 == 0!
			//chprintf(dbg, "Set XDRSIZE to %04X or %04X\r\n", sdr_size, BYTES(sdr_size));
			// streamPut(ost, 8);
			break;

		case XSDRTDO: // 09
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			i += read_bytes(tdo_expected, &(buf[i]), BYTES(sdr_size));
			//chprintf(dbg, "Set TDIVAL to %02X %02X %02X %02X\r\n", tdi_value[0], tdi_value[1], tdi_value[2], tdi_value[3]);
			//chprintf(dbg, "Set TDOEXP to %02X %02X %02X %02X\r\n", tdo_expected[0], tdo_expected[1], tdo_expected[2], tdo_expected[3]);
			if (sdr(SDR_FULL|SDR_CHECK)) {
				fail();
				return 0;
			}
			//// streamPut(ost, 9);
			break;

		case XSDRB:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			sdr(SDR_BEGIN|SDR_NOCHECK);
			// streamPut(ost, 12);
			break;

		case XSDRC:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			sdr(SDR_CONTINUE|SDR_NOCHECK);
			// streamPut(ost, 13);
			break;

		case XSDRE:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			sdr(SDR_END|SDR_NOCHECK);
			// streamPut(ost, 14);
			break;

		case XSDRTDOB:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			i += read_bytes(tdo_expected, &(buf[i]), BYTES(sdr_size));
			if (sdr(SDR_BEGIN|SDR_CHECK)) {
				fail();
				return 0;
			}
			// streamPut(ost, 15);
			break;

		case XSDRTDOC:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			i += read_bytes(tdo_expected, &(buf[i]), BYTES(sdr_size));
			if (sdr(SDR_CONTINUE|SDR_CHECK)) {
				fail();
				return 0;
			}
			// streamPut(ost, 16);
			break;

		case XSDRTDOE:
			i += read_bytes(tdi_value, &(buf[i]), BYTES(sdr_size));
			i += read_bytes(tdo_expected, &(buf[i]), BYTES(sdr_size));
			if (sdr(SDR_END|SDR_CHECK)) {
				fail();
				return 0;
			}
			// streamPut(ost, 17);
			break;

		case XSETSDRMASKS:
			i += read_bytes(address_mask, &(buf[i]), BYTES(sdr_size));
			i += read_bytes(data_mask, &(buf[i]), BYTES(sdr_size));
			// streamPut(ost, 10);
			break;

		case XSDRINC:
			fail();
			return 0;
			break;

		case XSTATE:
			read_byte(&inst, &(buf[i++]));
			//chprintf(dbg, "Goto STATE: %02X\r\n", inst);
			state_goto(inst);
			// streamPut(ost, 18);
			break;

		default:
			fail();
			return 0;
		}
		send_response(chunk, i);
	}
	return 1;
	//chprintf(dbg, "\r\n");
}

void xsvf_init(void){
  palSetLineMode(TDO_PIN, PAL_MODE_INPUT_PULLDOWN);
  TDI_IDLE;
  TMS_IDLE;
  TCK_IDLE;
  palSetLineMode(TDI_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TMS_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(TCK_PIN, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);

}