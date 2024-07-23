#include "bsoc.h"
#include "mcc_generated_files/interrupt_manager.h"

const char *MQTT_ID = "DUMPLOAD";

const uint32_t BVSOC_TABLE[BVSOC_SLOTS][2] = {
	23000, 5,
	23400, 10,
	23600, 20,
	24120, 25,
	24200, 30,
	24440, 40,
	24540, 45,
	24600, 50,
	24646, 53,
	24700, 55,
	24750, 57,
	24800, 60,
	24850, 63,
	24925, 67,
	25000, 70,
	25020, 72,
	25040, 75,
	25050, 80,
	25060, 85,
	25080, 90,
	25096, 92,
	25122, 93,
	25140, 95,
	25160, 97,
	25180, 100,
	26500, 98 // charging voltage guess
};

const char infoline1[] = "   LOW Count   TOD Count    TX Count    RX Count   ADC Count   SPI Count   DIO Count";
const char infoline2[] = "    DAILY AH      LO ESR      HI ESR       PV AH   BAT W Out    BAT W IN     REAL AH    F CYCLES   B CYCLES     UPDATES";

static uint32_t seq_log = 0;

#define MAX_GTI		650
#define GTI_POWER	0	// start with zero power
#define SPINNER_SPEED	200
#define GTI_SPEED	1000
#define Swrite		UART2_Write
#define Sread		UART1_Read
#define Sready		UART1_is_rx_ready

volatile int16_t gti_power = GTI_POWER;
volatile bool flop = true;
volatile uint8_t mqtt_r;
volatile int16_t cmd_value = 0;
time_t utc_cmd_value = 0;

uint8_t gti_sbuf[8] = {0x24, 0x56, 0x00, 0x21, 0x03, 0x99, 0x80, 0x6c};
uint8_t gti_zero[8] = {0x24, 0x56, 0x00, 0x21, 0x00, 0x00, 0x80, 0x08};

uint8_t gti_checksum(uint8_t *, uint16_t);

/*
 * DC PWM diversion for direct PV power before MPPT
 */
bool pv_diversion(bool kill)
{
	static uint16_t pwm_val = 0;
	static bool pwm_active = false;
	bool ret = false;

	if (kill) {
		pwm_val = 0;
		V.mode_pwm = 0;
		pwm_active = false;
		diversion_pwm_set(V.mode_pwm); // 10KHz PWM quick stop
		ret = false;
	} else {
		if ((((cc_state(C.v_cmode) == M_FLOAT) || (cc_state(C.v_cmode) == M_BOOST)) && (C.calc[V_PV] > DPWM_LOW_VOLTS))) {
			if (!pwm_active) {
				pwm_active = true;
				/*
				 * setup PWM start conditions
				 */
				C.start_power = C.p_pv; // save for later possible display
			}
			if (C.p_pv < PWM_MAX_POWER) { // increase power
				if (++pwm_val > DPWM_FULL)
					pwm_val = DPWM_FULL; // max PWM limiter
			}

			if (C.p_pv > PWM_MAX_POWER) { // decrease power
				if (V.mode_pwm)
					V.mode_pwm--;
			}

			V.mode_pwm = pwm_val;
			diversion_pwm_set(V.mode_pwm); // 10KHz PWM ramp up
			ret = true;
		} else {
			if (V.mode_pwm)
				V.mode_pwm--;

			diversion_pwm_set(V.mode_pwm); // 10KHz PWM ramp down
			pwm_val = 0;
			pwm_active = false;
			ret = false;
		}
	}
	return ret;
}

uint8_t gti_checksum(uint8_t * gti_sbuf, uint16_t power)
{
	uint16_t pu, pl, cs;

	if (power > MAX_GTI) {
		power = MAX_GTI;
	}

	pu = power >> 8;
	pl = power & 0xff;
	cs = 264 - pu - pl;
	if (cs >= 256) {
		cs = 8;
	}

	gti_sbuf[4] = (uint8_t) pu;
	gti_sbuf[5] = (uint8_t) pl;
	gti_sbuf[7] = (uint8_t) cs;
	return(uint8_t) cs;
}

/*
 * check for incoming data on the MQTT connection
 * use UART1 or the MQTT receive data port
 * calc_bsoc runs in interrupt low context and uses 16-bit results -> git_power from this
 * cmd_value is the buffer variable
 */
void gti_cmds(void)
{
	static uint8_t value[] = {0, 0, 0, 0}, vi = 0;
	static uint8_t utc_value[DEF_TIME_SIZE] = {0};
	static bool utc = false;
	uint8_t vcmd_size = sizeof(value);
	uint8_t utc_vcmd_size = DEF_TIME_SIZE-1;

	if (Sready()) {
		mqtt_r = Sread();
#ifdef GTI_ECHO
		UART1_Write(mqtt_r); // debug echo
#endif

		switch (mqtt_r) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (!utc) { // process power cmds
				if (vi < vcmd_size) {
					value[vi++] = mqtt_r - 48;
				} else {
				}
			} else { // process UTC time from host cmds
				if (vi < utc_vcmd_size) {
					utc_value[vi++] = mqtt_r;
				} else {
				}
			}
			break;
		case 'T': // begin UTC value
			utc = true;
			break;
		case 't': // end UTC value
			utc = false;
			if (vi >= utc_vcmd_size) {
				vi = 0;
				utc_value[10] = 0;
				utc_cmd_value = (time_t) atol((char *)utc_value);

				if (utc_cmd_value < DEF_TIME) {
					utc_cmd_value = DEF_TIME;
				}
				set_time(utc_cmd_value);
			};
			break;
		case 'V': // begin power/utc value
			vi = 0;
			break;
		case 'X': // end power value
			utc = false;
			if (vi >= vcmd_size) {
				vi = 0;
				cmd_value = value[0]*1000 + value [1]*100 + value[2]*10 + value[3];
				if (cmd_value > GTI_MAX) {
					cmd_value = GTI_MAX;
				}
				if (cmd_value < 0) {
					cmd_value = 0;
				}
				INTERRUPT_GlobalInterruptLowDisable(); // 16-bit atomic update
				gti_power = cmd_value;
				INTERRUPT_GlobalInterruptLowEnable();
			};
			break;
		case 'Z': // zero power
			utc = false;
			cmd_value = 0;
			break;
		case '+': // incr power
			utc = false;
			cmd_value = gti_power + GTI_INCR;
			if (cmd_value > GTI_MAX) {
				cmd_value = GTI_MAX;
			}
			break;
		case '-': // decr power
			utc = false;
			cmd_value = gti_power - GTI_INCR;
			if (cmd_value < 0) {
				cmd_value = 0;
			}
			break;
		case 'I': // idle power
			utc = false;
			cmd_value = GTI_IDLE;
			break;
		case 'F': // normal operation
			utc = false;
			cmd_value = GTI_NORM;
			break;
		case 'M': // max unit rated power testing
			utc = false;
			cmd_value = GTI_MAX;
			break;
		case '#': // execute command symbol
			utc = false;
			INTERRUPT_GlobalInterruptLowDisable(); // 16-bit atomic update
			gti_power = cmd_value;
			INTERRUPT_GlobalInterruptLowEnable();
			break;
		default: // eat extra characters
			utc = false;
			while (Sready()) {
				mqtt_r = Sread();
			}
			break;
		}
	}
}

/*
 * low-pri interrupt ISR the runs every second for simple coulomb counting
 */
void calc_bsoc(void)
{
	uint8_t * log_ptr;
	uint32_t t_time;
	static uint8_t log_update_wait = 0;
	float adj = 1.0;

#ifdef DEBUG_BSOC1
	DEBUG1_SetHigh();
#endif
	V.lowint_count++;
	/*
	 * check for excess power and send to DC dump load
	 */
	pv_diversion(false);
	/*
	 * Charge Controller condition events
	 */
	if (cc_state(C.v_cmode) == M_FLOAT) {
		if (!V.in_float && (++V.float_ticks > FLOAT_TIME))
			if (!V.in_float) {
				V.in_float = true;
				if (C.soc < 95) // SOC and AH corrections
					C.dynamic_ah = C.bank_ah * 0.95f;
			}
	} else {
		V.float_ticks = 0;
	}

	if (cc_state(C.v_cmode) == M_BOOST) {
		if (!V.in_boost && (++V.boost_ticks > FLOAT_TIME))
			if (!V.in_boost) { // SOC and AH corrections
				V.in_boost = true;
				if (C.soc < 80)
					C.dynamic_ah = C.bank_ah * 0.80f;
			}
	} else {
		V.boost_ticks = 0;
	}

	/*
	 * Battery data calculations
	 */
	C.dynamic_ah += (C.c_bat / SSLICE); // Ah
	C.dynamic_ah_daily += (C.c_bat / SSLICE); // Ah
	if (C.dynamic_ah > (C.bank_ah))
		C.dynamic_ah = C.bank_ah;
	if (C.dynamic_ah < 0.1)
		C.dynamic_ah = 0.1;

	if (C.c_bat > 0.01)
		adj = C.hist[0].cef;
	if (C.c_bat < 0.01)
		adj = C.hist[0].peukert;
	C.dynamic_ah += ((C.c_bat * adj) / SSLICE); // Ah

	C.dynamic_ah_adj = C.dynamic_ah; // need to add peukert factor here
	C.dynamic_ah_adj_daily = C.dynamic_ah_daily; // need to add peukert factor here
	if (C.dynamic_ah_adj > (C.bank_ah))
		C.dynamic_ah_adj = C.bank_ah;
	if (C.dynamic_ah_adj < 0.1)
		C.dynamic_ah_adj = 0.1;


	C.pv_ah += (C.c_pv / SSLICE);
	C.pvkw += (C.p_pv / SSLICE);
	C.invkw += (C.p_inverter / SSLICE);
	C.loadkw += (C.p_load / SSLICE);
	if (C.p_bat > 0.0)
		C.bkwi += (C.p_bat / SSLICE);
	if (C.p_bat < 0.0)
		C.bkwo += fabs(C.p_bat / SSLICE);

	C.soc = ((uint16_t) ((C.dynamic_ah_adj / C.bank_ah)*100.0) + 1);
	if (C.soc > 100)
		C.soc = 100;

	if (C.c_bat < 0.0) {
		C.runtime = (uint16_t) (-(C.dynamic_ah_adj / C.c_bat));
	} else {
		C.runtime = 99;
	}
	if (C.runtime > 99)
		C.runtime = 99;

	/*
	 * check event flags
	 */
	if (V.time_info) { // set local time from remote server
		V.time_info = false;
		t_time = (uint32_t) atol(V.rbuf);
		if (t_time > DEF_TIME) {
			set_time(t_time);
		}
	}

	/*
	 * serial port log data updates using DMA
	 */
	if (V.sys_info) {
		V.sys_info = false;
	} else {
		if (!log_update_wait++ && V.system_stable && !V.get_time_text) {
			log_ptr = port_data_dma_ptr();
			if (H.sequence == HID_AUX) {
			}

			/*
			 * REMOVE THE CHIP PROGRAMMER, IT CHANGES THE ADC READINGS
			 */
			/*
			 * format data to JSON
			 */
			C.p_pv = C.v_pv * C.c_bat; // power from FM80 AC
			C.p_bat = C.v_bat * C.c_mppt; // Power to/from BATTERY
			C.p_mppt = C.v_bat * C.c_pv; // Power from Charge Controller
			snprintf((char *) log_ptr, max_port_data - 1, "{\r\n \"DLname\": \"%s MBMC K42\",\r\n \"DLsequence\": %lu,\r\n \"DLccmode\": %u,\r\n \"DLgti\": %u,\r\n \"DLv_pv\": %7.2f,\r\n \"DLv_bat\": %7.2f,\r\n \"DLc_pv\": %7.2f,\r\n \"DLc_mppt\": %7.2f,\r\n \"DLc_bat\": %7.2f,\r\n \"DLp_pv\": %7.2f,\r\n \"DLp_mppt\": %7.2f,\r\n \"DLp_bat\": %7.2f,\r\n \"DLah_bat\": %7.2f,\r\n \"DLbuild_date\": \"%s\",\r\n \"DLbuild_time\": \"%s\"\r\n}\r\n",
				VER, seq_log++, V.cc_state, gti_power, C.v_pv, C.v_bat, C.c_pv, C.c_mppt, C.c_bat, C.p_pv, C.p_mppt, C.p_bat, C.dynamic_ah, build_date, build_time);

			StartTimer(TMR_DISPLAY, SOCDELAY); // sync the spi dma display updates to avoid memory contention
#ifdef DEBUG_JSON
			DEBUG2_Toggle();
#endif
			send_port_data_dma(strlen((char*) log_ptr));
			/*
			 * send power limiter commands to the GTI Inverter
			 * uses serial port 2 4800 baud
			 */
			gti_checksum(gti_sbuf, (uint16_t) gti_power);
			for (uint8_t i = 0; i < 8; i++) {
				if (flop) {
					Swrite(gti_sbuf[i]);
				} else {
					Swrite(gti_zero[i]);
				}
			}
		}

		if (log_update_wait >= LOG_WAIT) {
			log_update_wait = 0;
		}
	}

	C.update = false;
#ifdef DEBUG_BSOC1
	DEBUG1_SetLow();
#endif
}

void init_bsoc(void)
{
	/*
	 * use raw battery voltage
	 */
	C.soc = Volts_to_SOC((uint32_t) (conv_raw_result(V_BAT, CONV) * 1000.0f));
	C.dynamic_ah = C.bank_ah * (Volts_to_SOC((uint32_t) (conv_raw_result(V_BAT, CONV) * 1000.0f)) / 100.0f);
	if (V.cc_active) { // charge controller online FIXUP
		C.dynamic_ah += 10.0f;
		C.soc += 10;
	}
	C.dynamic_ah_adj = C.dynamic_ah;
	TMR3_SetInterruptHandler(calc_bsoc); // one second timer
}

void start_bsoc(void)
{
	// Enabling TMR3 interrupt.
	PIE6bits.TMR3IE = 1;
#ifdef DEBUG_BSOC2
	DEBUG2_Toggle();
#endif
}

void stop_bsoc(void)
{
	// Disabling TMR3 interrupt.
	PIE6bits.TMR3IE = 0;
}

void reset_bsoc(const R_CODES rmode)
{
	switch (rmode) {
	case R_CYCLE:
	default:
		C.pv_ah = 0.0;
		break;
	}
}

/* Current is in 0.1A units */
uint32_t peukert(uint16_t brate, float bcurrent, float peukert, int16_t bsoc)
{
	float t1, t2, t3, t4, t5, t6, t7, t_time;

	t1 = (float) bcurrent; // Load on battery in 0.1A units
	t1 /= 10.0; // convert back to A
	if (t1 < 1.0) t1 = 1.0; // inverter current floor is 1.0 amp
	t2 = (float) brate; // Battery capacity in 1.0A units
	t3 = (float) ((float) bsoc / 100.0); // State of Charge
	if (t3 > 0.0) t2 = t2 * t3; // only decrease battery Ah
	if (t2 < 1.0) t2 = 1.0; // Ah floor is 1.0
	t4 = t2 * (float) (pow((t2 / (float) 20.0), ((float) peukert - (float) 1.0)));
	t_time = pow((t2 / (float) 20.0), peukert); // factor at 20h rate
	t5 = pow(t1, peukert); // factor at load current rate
	t6 = t4 / t5; // runtime in hours at the current draw
	if (t6 > MAXRUNTIME) t6 = MAXRUNTIME; // max time is 4 days in hours
	if (t6 < MINRUNTIME) t6 = MINRUNTIME;
	t7 = t4 / t_time; // time at fixed 20h rate

	C.hist[0].peukert_adj = t7 / t6; // Peukert factor to resize Ah usage, t7=runtime at the 20h current rate/t6=runtime at the inverter load current value
	if (C.hist[0].peukert_adj < Perk_ADJ_FL)
		C.hist[0].peukert_adj = Perk_ADJ_FL; // limit how LOW
	if (C.hist[0].peukert_adj > Perk_ADJ_FH)
		C.hist[0].peukert_adj = Perk_ADJ_FH; // limit how HIGH

	t6 = t6 * 3600.0f; // convert to seconds for runtime at current discharge rate
	return(uint32_t) t6;
}

/*
 * static SOC table
 */
uint16_t Volts_to_SOC(const uint32_t cvoltage)
{
	uint8_t slot;
	uint16_t soc = 0;

	/*
	 * walk up the table
	 */
	for (slot = 0; slot < BVSOC_SLOTS; slot++) {
		if (cvoltage > BVSOC_TABLE[slot][0]) {
			soc = (uint16_t) BVSOC_TABLE[slot][1];
		}
	}
	return soc;
}

/*
 * check battery ESR, returns positive ESR value when done,
 * a negative number code when running the sequence and
 * -1.0 when each FSM sequence is done
 * (fsm 'true' will init the state machine and return the init code)
 */
float esr_check(const uint8_t fsm)
{
	static uint8_t esr_state = 0;
	float ret = -1.0;

	if (fsm) {
		esr_state = 0;
		ret = -10.0;
	} else {

		switch (esr_state) {
		case 0:
			StartTimer(TMR_ESR, 10000); // start the sequence timer
			esr_state++; // move to the next state of the FSM
			break;
		case 1:
			/*
			 * set the load resistors to all off
			 */
			set_load_relay_one(false);
			set_load_relay_two(false);
			if (TimerDone(TMR_ESR)) { // check for expired timer
				StartTimer(TMR_ESR, 10000); // done, restart the timer, complete sequence, return -1.0
			} else {
				ret = -2.0; // nope, return with a progress code
			}
			/*
			 * save unloaded battery voltage
			 */
			update_adc_result();
			C.bv_noload = conv_raw_result(V_BAT, CONV);
			esr_state++; // move to the next state of the FSM
			break;
		case 2:
			set_load_relay_one(true);
			if (TimerDone(TMR_ESR)) {
				StartTimer(TMR_ESR, 10000);
			} else {
				ret = -3.0;
			}

			update_adc_result();
			C.bv_one_load = conv_raw_result(V_BAT, CONV);
			C.load_i1 = conv_raw_result(C_BATT, CONV); // get current
			esr_state++;
			break;
		case 3:
			set_load_relay_two(true);
			if (!TimerDone(TMR_ESR))
				ret = -4.0;

			update_adc_result();
			C.bv_full_load = conv_raw_result(V_BAT, CONV);
			C.load_i2 = conv_raw_result(C_BATT, CONV); // get current

			C.esr = fabs((C.bv_one_load - C.bv_full_load) / (C.load_i1 - C.load_i2)); // find internal resistance causing voltage drop (sorta)
			set_load_relay_one(false);
			set_load_relay_two(false);
			esr_state = 0;
			ret = C.esr;
			break;
		default:
			break;
		}
	}
	return ret;
}

/*
 * read analog signal from RS485 interface for charge controller status
 */
uint8_t cc_state(float cc_signal)
{
	uint8_t state = M_OFFLINE; // offline

	if ((cc_signal >= 4.65) || (cc_signal < 0.75))
		return state; // dead signal range

	if ((cc_signal >= 4.25) && (cc_signal < 4.65)) // 4.4
		return state; //offline

	if ((cc_signal >= 3.75) && (cc_signal < 4.25)) // 4.0
		state = M_LIMIT;
	if ((cc_signal >= 3.25) && (cc_signal < 3.75)) // 3.5
		state = M_FLOAT;
	if ((cc_signal >= 2.75) && (cc_signal < 3.25)) // 3.0
		state = M_BOOST;
	if ((cc_signal >= 2.25) && (cc_signal < 2.75)) // 2.5
		state = M_EQUAL;
	if ((cc_signal >= 1.75) && (cc_signal < 2.25)) // 2.0
		state = M_MPPT;
	if ((cc_signal >= 1.25) && (cc_signal < 1.75)) // 1.5
		state = M_ACT;
	if ((cc_signal >= .75) && cc_signal < 1.25) // 1.0
		state = M_DEACT;
	return state;
}
