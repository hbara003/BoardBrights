/*
 * Tachometer_CS122AProject.c
 *
 * Created: 11/15/2018 3:36:38 PM
 * Author : Hector
 */ 

#include <avr/io.h>
#include "Timer.h"
//#include "ws2811.h" // this will auto-select the 8Mhz or 9.6Mhz version

#define pulse	(~PINA & 0x01)

void TimerISR() { TimerFlag = 1; }
	
void ADC_init() {
	ADCSRA |= (1 << ADEN) | (1 << ADSC) | (1 << ADATE);
	// ADEN: setting this bit enables analog-to-digital conversion.
	// ADSC: setting this bit starts the first conversion.
	// ADATE: setting this bit enables auto-triggering. Since we are
	//        in Free Running Mode, a new conversion will trigger whenever
	//        the previous conversion completes.
	
	// Pins on PORTA are used as input for A2D conversion
	// The default channel is 0 (PA0)
	// The value of ADMUX determines the pin on PORTA
	// used for A2D conversion
	// Valid values range between 0 and 7, where the value
	// represents the desired pin for A2D conversion
	ADMUX = 0x01;
	// Allow channel to stabilize
	static unsigned char i = 0;
	for ( i=0; i<15; i++ ) { asm("nop"); }
}

enum S_States {idle, rise, waitfall} S_State;
void Tick_SensePulse();
enum DL_States {LightsOff, DisplayPattern} DL_State;
void Tick_DisplayLights();
void Tick_TrackTime();


// global
unsigned char systemOn = 0;
unsigned long pulseCount = 0; // one pulse per revolution
unsigned char patternSelect = 0;
unsigned char lightsOut = 0;
unsigned char patterns[4] = {0x00, 0x0F, 0xF0, 0xFF};
unsigned short n;
int main(void){
	DDRA = 0x00; PORTA = 0xFF; // PORTA as input for pulse detection
	DDRD = 0xFF; PORTD = 0x00; // PORTD as output
	DDRB = 0xFF; PORTB = 0x00; // PORTB as output
	
	// Period set to <250 to catch a pulse
	TimerSet(10);
	TimerOn();
	// Detect light, system off (sleep?) while bright outside
	ADC_init();
	n = ADC;
    while (1) {
		n = ADC;
		if (n > 0b111100) {
			Tick_SensePulse();
			Tick_DisplayLights();
			Tick_TrackTime();
		}

		PORTD = lightsOut;
		
		while(!TimerFlag) { }
		TimerFlag = 0;
    }
}

void Tick_DisplayLights() {
	switch (DL_State) { // transitions
		case LightsOff:
			if (!patternSelect) {
				DL_State = LightsOff;
			}
			else {
				DL_State = DisplayPattern;
			}
			break;
		case DisplayPattern:
			DL_State = (patternSelect) ? DisplayPattern : LightsOff;
			break;
		default:
			DL_State = LightsOff;
			break;
	} // end transitions
	switch (DL_State) { // actions
		case LightsOff:
			lightsOut = 0x00;
			break;
		case DisplayPattern:
			lightsOut = patterns[patternSelect];
			break;
		default:
			break;
	} // end actions
}

void Tick_SensePulse() {
	switch (S_State) { // transitions
		case idle:
			if (pulse) {
				S_State = rise;
			}
			else {
				S_State = idle;
			}
			break;
		case rise:
			S_State = waitfall;
			break;
		case waitfall:
			if (pulse) {
				S_State = waitfall;
			}
			else {
				S_State = idle;
			}
			break;
		default:
			S_State = idle;
			break;
	} // end transitions
	switch (S_State) { // actions
		case rise:
			++pulseCount;
			break;
		default:
			break;
	} // end actions
}

void Tick_TrackTime() {
	static unsigned short sampleTime = 300; // sample number of pulses in 3 seconds
	static unsigned short TimeElapsed = 0; // keep counting pulses until TimeElapsed == sampleTime
	static unsigned char pulsesInSampleTime = 0; // end result of #revolutions in sample time (3 seconds)
	
	if (TimeElapsed > sampleTime) {
		TimeElapsed = 0;
		pulsesInSampleTime = pulseCount;
		pulseCount = 0;
		PORTB = 0x01;
	}
	else {
		PORTB = 0x00;
		++TimeElapsed;
	}
						
	if (pulsesInSampleTime > 20) {
		patternSelect = 3;
	}
	else if (pulsesInSampleTime > 7) {
		patternSelect = 2;
	}
	else if (pulsesInSampleTime > 2) {
		patternSelect = 1;
	}
	else {
		patternSelect = 0; // no lights
	}
}