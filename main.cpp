//

// Copyright (c) 2013 Danny Havenith

//

// Distributed under the Boost Software License, Version 1.0. (See accompanying

// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

//

/**

 * Demonstration of the ws2811 code for devices with enough RAM to hold values for 60 LEDs.

 *

 * Depending on which effect you want and on which port/bit you want to transmit do the following

 * - un-comment the appropriate include file

 * - define WS2811_PORT to be the port that you want to transmit on

 * - define the variable 'channel' as the bit you want to use for transmission

 * - make sure that you initialize the DDRn register, so that bit 'channel' becomes an output.

 * - call the appropriate code from main.

 *

 * Note that some demo functions still declare their own array of LEDs, and some require the main

 * function to declare such an array.

 *

 */



#include <avr/io.h>

#include <util/delay.h>

#include <stdlib.h>
#define F_CPU 8000000 // changed from 1000000UL to work with LED strips

// pin 1 on PINA will detect magnet passing by the magnetic switch (one revolution)
#define pulse	(~PINA & 0x01)



// Define the port at which the signal will be sent. The port needs to
// be known at compilation time, the pin (0-7) can be chosen at run time.
#define WS2811_PORT PORTC



// send RGB in R,G,B order instead of the standard WS2811 G,R,B order.
// Most ws2811 LED strips take their colors in GRB order, while some LED strings
// take them in RGB. Default is GRB, define this symbol for RGB.
//#define STRAIGHT_RGB

#include "effects/campfire.hpp"
#include "ws2811/ws2811.h"
#include "Timer.h"

namespace { // why does he put it into namespace?
// selects the pin (the bit of the chosen port).
// this must be in the range 0-7
static const uint8_t 	channel2 = 4;

// the number of LEDs in the string.
static const uint16_t 	led_count = 37;

// declare one RGB value for each led.
ws2811::rgb leds[led_count];
}

template< uint16_t size>
void scroll( ws2811::rgb new_value, ws2811::rgb (&range)[size]) {

	range[0] = new_value;
	for (uint8_t idx = size-1; idx != 0 ; --idx)
	{
		range[idx] = range[idx - 1];
	}
}
template< uint16_t led_count>
void animate( const ws2811::rgb &new_value, ws2811::rgb (&leds)[led_count], uint8_t channel) {
	scroll( new_value, leds);
	send( leds, channel);
}

void TimerISR() {TimerFlag = 1;}

void ADC_init() {
	ADCSRA |= (1 << ADEN) | (1 << ADSC) | (1 << ADATE);
	ADMUX = 0x01;
	static unsigned char i = 0;
	for ( i=0; i<15; i++ ) { asm("nop"); }
}
void Set_A2D_Pin(unsigned char pinNum) {
	// NOTE: this function is taken from the CS122A labs
	// Pins on PORTA are used as input for A2D conversion
	// The default channel is 0 (PA0)
	// The value of pinNum determines the pin on PORTA
	// used for A2D conversion
	// Valid values range between 0 and 7, where the value
	// represents the desired pin for A2D conversion
	ADMUX = (pinNum <= 0x07) ? pinNum : ADMUX;
	// Allow channel to stabilize
	static unsigned char i = 0;
	for ( i=0; i<15; i++ ) { asm("nop"); }
}

enum S_States {idle, rise, waitfall} S_State;
void Tick_SensePulse();
enum DL_States {wait, ScrollSlow, ScrollMed, ScrollFast, ScrollIdle} DL_State;
void Tick_DisplayLights();
enum PS_State {SystemOff, SystemOn, wait30toOff} PS_State;
void Tick_PowerStatus();
void Tick_TrackTime();

// global
unsigned char systemIsOn = 0; // 0 when system is off, 1 when system is on
unsigned long pulseCount = 0; // one pulse per revolution, stores number of pulses
unsigned char pulsesInSampleTime = 0; // end result of number of revolutions in sample time (3 seconds)
unsigned char sysIdle = 0;
signed char delay = 0;

int main() {
	DDRA = 0x00; PORTA = 0xFF; // input for ADC and magnet sensor, (joystick?)
	DDRD = 0xFF; PORTD = 0x00; // output to LED bar (for testing)
	DDRB = 0xFF; PORTB = 0x00; // output to tick LED
	DDRC = 0xFF; PORTC = 0x00; // output to LED strips (pin 5)

	// need a period of at most 10 to detect one rotation (physics)
	TimerSet(10);
	TimerOn();

	// initialize ADC conversion at PORTA pin 1
	ADC_init();

	unsigned short n;
	while (1) {
		Tick_PowerStatus();
		Tick_SensePulse();
		Tick_TrackTime();

		// make this if else to SM?
		// or add it to PS SM?
		if (systemIsOn) {
			Tick_DisplayLights();
		}
		else {
			// clear strips, system is off
			clear(leds); // this line necessary
			fill(leds, rgb(0,0,0));
			send(leds, channel2);
			//possibly use python script to fill pattern array
		}

		// output ADC to LED bar
		//n = ADC;
		//PORTD = (char)n;
		//PORTB |= ((char) (n >> 2)) & 0xC0;

		// wait for period
		while (!TimerFlag) { }
		TimerFlag = 0;
	}
}

unsigned char checkConditions() {
	unsigned short n;
// 	unsigned short x;
// 	unsigned short y;
	Set_A2D_Pin(1);
	n = ADC;
//  	Set_A2D_Pin(2);
//  	// TOP potentiometer, turning right increments value
//  	x = ADC; //subtract 512 for joystick
//  	Set_A2D_Pin(3);
// 	// Lower potentiometer, turning LEFT increments value
// 	y = ADC;
	PORTD = n < 40;

	//d = sqrt( pow(x,2) + pow(y,2) );
	if (0) {
		return 0;
	}
	else if (pulsesInSampleTime < 3) {
		return 0;
	}
	else {
		return 1;
	}

}
void Tick_PowerStatus() {
	// SystemOn Coditions:
	//   system should be on in the case that it is not too bright out (photo-resistor)
	//   AND the wheels are rolling
	//   system stays on for 30 seconds when wheels stop spinning
	//   NOTE: not sure if there is a better way to do this
	static unsigned short count30 = 0;
	switch (PS_State) { // transitions
		case SystemOff:
			// 0b111100 is ADC value when finger is over photo resistor (testing)
			// later to be calibrated to ADC value for outside night-time darkness
			if (checkConditions()) {
				systemIsOn = 1;
				PS_State = SystemOn;
			}
			else {
				PS_State = SystemOff;
			}
			break;
		case SystemOn:
			if (checkConditions()) {
				PS_State = SystemOn;
			}
			else {
				PS_State = wait30toOff;
			}
			break;
		case wait30toOff:
			if (count30 < 1000 && pulsesInSampleTime < 5) {
				PS_State = wait30toOff;
			}
			if (count30 < 1000 && pulsesInSampleTime > 5) {
				PS_State = SystemOn;
			}
			else if (count30 > 1000) {
				systemIsOn = 0;
				count30 = 0;
				PS_State = SystemOff;
			}
			break;
		default:
			PS_State = SystemOff;
			break;
	} // end transitions
	switch (PS_State) { // actions
		case wait30toOff:
			++count30;
			sysIdle = 1;
			break;
		default:
			sysIdle = 0;
			break;
	} // end actions
}

DL_States getSpeed(int n) {
	// n is number of revolutions in 3 seconds
	// 60 revolutions in 3 seconds ~ 10 mph
	if (n > 60) {
		return ScrollFast;
	}
	else if (n > 30) {
		return ScrollMed;
	}
	else {
		return ScrollSlow;
	}
}
void Tick_DisplayLights() {
	static unsigned short cnt = 0;
	static unsigned char i = 0;
	switch (DL_State) { // transitions
		case wait:
			if (cnt < delay) {
				//25 good for slow, 15 maybe better
				// 1 is really quick, maybe good for fast
				DL_State = wait;
			}
			else if(!sysIdle) {
				DL_State = getSpeed(pulsesInSampleTime);
				i = (i < 22) ? i + 1 : 0;
			}
			else {
				DL_State = ScrollIdle;
				i = (i < 3) ? i + 1 : 0;
			}
			break;
		case ScrollSlow:
			DL_State = wait;
			break;
		case ScrollMed:
			DL_State = wait;
			break;
		case ScrollFast:
			DL_State = wait;
			break;
		case ScrollIdle:
			DL_State = wait;
			break;
		default:
			DL_State = wait;
			break;
	} // end transitions
	switch (DL_State) { // actions
		case wait:
			++cnt;
			break;
		case ScrollSlow:
			scroll(patternSlow[i], leds);
			send(leds, channel2);
			cnt = 0;
			break;
		case ScrollMed:
			scroll(patternMed[i], leds);
			send(leds, channel2);
			cnt = 0;
			break;
		case ScrollFast:
			scroll(patternFast[i], leds);
			send(leds, channel2);
			cnt = 0;
			break;
		case ScrollIdle:
			scroll(patternIdle[i], leds);
			send(leds, channel2);
			cnt = 0;
			break;
		default:
			break;
	} // end actions
}

// grab a rising edge, wait for falling edge until grabbing next rising edge
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


	if (TimeElapsed > sampleTime) {
		pulsesInSampleTime = pulseCount;
		// mounted on wheel, this should max out at around 60
		TimeElapsed = 0;
		pulseCount = 0;

		PORTB = 0x01;
	}
	else {
		PORTB = 0x00;
		++TimeElapsed;
	}

	PORTB |= systemIsOn << 1;
	delay = 25 - pulsesInSampleTime;
	delay = (delay < 0) ? 0 : delay;
}


/************************************************************************/
/*
// 0xFF == 0b1111 1111
Set_A2D_Pin(0);
x = ADC;
_delay_ms(10);

Set_A2D_Pin(1);
y = ADC;
_delay_ms(10);

//d = pow((pow(x,2) + pow(y,2)), 0.5);
//(x > 450 && x < 650) &&
if (y > 450 && y < 600) {
PORTD = Sam;
}
else {
PORTD = ~Sam;
}                                                                     */
/************************************************************************/