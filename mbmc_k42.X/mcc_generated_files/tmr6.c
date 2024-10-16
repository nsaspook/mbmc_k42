/**
  TMR6 Generated Driver File

  @Company
    Microchip Technology Inc.

  @File Name
    tmr6.c

  @Summary
    This is the generated driver implementation file for the TMR6 driver using PIC10 / PIC12 / PIC16 / PIC18 MCUs

  @Description
    This source file provides APIs for TMR6.
    Generation Information :
        Product Revision  :  PIC10 / PIC12 / PIC16 / PIC18 MCUs - 1.77
	Device            :  PIC18F57K42
	Driver Version    :  2.11
    The generated drivers are tested against the following:
        Compiler          :  XC8 2.05 and above 
        MPLAB 	          :  MPLAB X 5.20
 */

/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries. 
    
    Subject to your compliance with these terms, you may use Microchip software and any 
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party 
    license terms applicable to your use of third party software (including open source software) that 
    may accompany Microchip software.
    
    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER 
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY 
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS 
    FOR A PARTICULAR PURPOSE.
    
    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP 
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO 
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL 
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT 
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS 
    SOFTWARE.
 */

/**
  Section: Included Files
 */

#include <xc.h>
#include "tmr6.h"
#include "interrupt_manager.h"
#include "uart1.h"
#include "../vconfig.h"
#include "../timers.h"

extern struct V_data V;
extern volatile uint16_t tickCount[TMR_COUNT];

/**
  Section: Global Variables Definitions
 */

void (*TMR6_InterruptHandler)(void);

/**
  Section: TMR6 APIs
 */

void TMR6_Initialize(void)
{
	// Set TMR6 to the options selected in the User Interface

	// T6CS FOSC/4; 
	T6CLKCON = 0x01;

	// T6PSYNC Not Synchronized; T6MODE Software control; T6CKPOL Rising Edge; T6CKSYNC Not Synchronized; 
	T6HLT = 0x00;

	// T6RSEL T6CKIPPS pin; 
	T6RST = 0x00;

	// PR6 124; 
	T6PR = 0x7C;

	// TMR6 0; 
	T6TMR = 0x00;

	// Clearing IF flag before enabling the interrupt.
	PIR9bits.TMR6IF = 0;

	// Enabling TMR6 interrupt.
	PIE9bits.TMR6IE = 1;

	// Set Default Interrupt Handler
	TMR6_SetInterruptHandler(TMR6_DefaultInterruptHandler);

	// T6CKPS 1:64; T6OUTPS 1:1; TMR6ON on; 
	T6CON = 0xE0;
}

void TMR6_ModeSet(TMR6_HLT_MODE mode)
{
	// Configure different types HLT mode
	T6HLTbits.MODE = mode;
}

void TMR6_ExtResetSourceSet(TMR6_HLT_EXT_RESET_SOURCE reset)
{
	//Configure different types of HLT external reset source
	T6RSTbits.RSEL = reset;
}

void TMR6_Start(void)
{
	// Start the Timer by writing to TMRxON bit
	T6CONbits.TMR6ON = 1;
}

void TMR6_StartTimer(void)
{
	TMR6_Start();
}

void TMR6_Stop(void)
{
	// Stop the Timer by writing to TMRxON bit
	T6CONbits.TMR6ON = 0;
}

void TMR6_StopTimer(void)
{
	TMR6_Stop();
}

uint8_t TMR6_Counter8BitGet(void)
{
	uint8_t readVal;

	readVal = TMR6;

	return readVal;
}

uint8_t TMR6_ReadTimer(void)
{
	return TMR6_Counter8BitGet();
}

void TMR6_Counter8BitSet(uint8_t timerVal)
{
	// Write to the Timer6 register
	TMR6 = timerVal;
}

void TMR6_WriteTimer(uint8_t timerVal)
{
	TMR6_Counter8BitSet(timerVal);
}

void TMR6_Period8BitSet(uint8_t periodVal)
{
	PR6 = periodVal;
}

void TMR6_LoadPeriodRegister(uint8_t periodVal)
{
	TMR6_Period8BitSet(periodVal);
}

void __interrupt(irq(TMR6), base(8)) TMR6_ISR()
{

	// clear the TMR6 interrupt flag
	PIR9bits.TMR6IF = 0;

	// ticker function call;
	// ticker is 1 -> Callback function gets called everytime this ISR executes
	TMR6_CallBack();
}

void TMR6_CallBack(void)
{
	// Add your custom callback code here
	// this code executes every TMR6_INTERRUPT_TICKER_FACTOR periods of TMR6
	if (TMR6_InterruptHandler) {
		TMR6_InterruptHandler();
	}
}

void TMR6_SetInterruptHandler(void (* InterruptHandler)(void))
{
	TMR6_InterruptHandler = InterruptHandler;
}

/*
 * update software timers and parse simple serial port #1 single character commands
 */
void TMR6_DefaultInterruptHandler(void)
{
	static uint8_t i, j = 0;
	// add your TMR6 interrupt custom code
	// or set custom function using TMR6_SetInterruptHandler()

	//Decrement each software timer
	for (i = 0; i < TMR_COUNT; i++) {
		if (tickCount[i] != 0) {
			tickCount[i]--;
		}
	}

	if (UART1_is_rx_ready()) {
		i = UART1_Read();

		if (i == 't') { // UTC time command stop
			UART1_Write(i);
			UART1_Write('\r');
			UART1_Write('\n');
			V.time_info = false;
			if (j == 10)
				V.time_info = true; // notify main we have a possible valid time
			V.get_time_text = false;
			V.rbuf[j] = 0; // term string
			j = 0;
		}

		if (V.get_time_text && j < 12) { // load ASCII time string and overflow size count
			UART1_Write(i);
			V.rbuf[j++] = i;
		}
		if (i == '#') { // Display system data on RS232 terminal
			V.sys_info = true;
		}
		if (i == 'V') { // AC charger ON
			V.ac_on = true;
		}
		if (i == 'v') { // AC charger OFF
			V.ac_off = true;
		}
		if (i == 'T') { // UTC time command start
			UART1_Write(i);
			V.time_info = false;
			V.get_time_text = true;
			j = 0;
		}

	}
}

/**
  End of File
 */