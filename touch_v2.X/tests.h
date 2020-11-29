/* Microchip Technology Inc. and its subsidiaries.  You may use this software 
 * and any derivatives exclusively with Microchip products. 
 * 
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER 
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED 
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A 
 * PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION 
 * WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION. 
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS 
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE 
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS 
 * IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF 
 * ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE 
 * TERMS. 
 */

/* 
 * File:   
 * Author: 
 * Comments:
 * Revision history: 
 */

// This is a guard condition so that contents of this file are not included
// more than once.  
#ifndef TESTS_H
#define	TESTS_H

#include <xc.h> // include processor files - each processor file is guarded.  
#include "mcc_generated_files/pin_manager.h"

#define DEBUG_MBMC	// global application debug switch
#define DLED1	DEBUG1_LAT
#define DLED2	DEBUG2_LAT

#ifdef DEBUG_MBMC
#define DEBUG_DAQ1 // daq debug switches : SCANNER
#define DEBUG_DAQ2 // ADC conversion
//#define DEBUG_SWH1 // switches
//#define DEBUG_SWH2
//#define DEBUG_BSOC1 // battery run calculations
//#define DEBUG_BSOC2
//#define DEBUG_HIST1
//#define DEBUG_TMR3 // timers
//#define DEBUG_TRM5
//#define DEBUG_DISP1 // LCD Display
//#define DEBUG_DISP2
//#define DEBUG_WDT1 // WDT
//#define DEBUG_WDT2
#endif

#define CALIB	false
#define FIXUPS	true
//#define CLR_CYCLE_DATA
//#define CLR_BATTERY_DATA
//#define CLR_PV_DATA
#endif	/* XC_HEADER_TEMPLATE_H */

