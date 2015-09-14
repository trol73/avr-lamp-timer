/*
 * timer_for_uv_lamp.c
 *
 * Created: 30.08.2015 20:12:54
 *  Author: Trol
 */ 

#define F_CPU	8000000

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

#include <util/delay.h>

#define KEY_PLUS	0
#define KEY_MINUS	1
#define KEY_ENTER	2
#define KEY_BACK	3


#define TIMER_MODE_WAIT		0
#define TIMER_MODE_RUN		1

#define KEY_PRESSED_COUNT	16

#define MAX_TIME_INTERVAL_MINUTES	600

#define TIMER2_DELTA_COUNT	(F_CPU/64/100)		// 100 Hz


#define EEPROM_OFFSET_TIME		0

const uint8_t DISPLAY_SYMBOLS[] PROGMEM = {
	0x3f, // 0
	0x06, // 1
	0x5b, // 2
	0x4f, // 3
	0x66, // 4
	0x6d, // 5
	0x7d, // 6
	0x07, // 7
	0x7f, // 8
	0x6f, // 9
	0x77, // A
	0x7c, // b
	0x39, // C
	0x58, // c
	0x5e, // d
	0x79, // E
	0x71, // F
	0x76, // H
	0x74, // h
	0x30, // I
	0x0e, // J
	0x38, // L
	0x3f, // O
	0x5c, // o
	0x73, // P
	0x67, // q
	0x50, // r
	0x6d, // S
	0x78, // t
	0x3e, // U
	0x1c, // u
	0x6e, // Y
	0x63, // *
};

volatile uint8_t video_memory[4];
volatile uint8_t key_pressed[4];
volatile uint8_t timer_mode;
volatile uint16_t time;
volatile uint8_t hsec;


uint16_t timer_interval;




static void onKeyPressed(uint8_t key);


static void enableLed(bool on) {
	if (on) {
		PORTB |= _BV(0);
	} else {
		PORTB &= ~_BV(0);
	}
}


static void enablePower(bool on) {
	if (on) {
		PORTB |= _BV(5);
	} else {
		PORTB &= ~_BV(5);
	}	
}


/*
 Cathode: 0..7
*/
static inline void displayUpdate(uint8_t cathode) {
	uint8_t mask = _BV(cathode);
	uint8_t b = PORTB;
	if (video_memory[0] & mask) {
		b |= _BV(1);
	} else {
		b &= ~_BV(1);
	}
	if (video_memory[1] & mask) {
		b |= _BV(2);
	} else {
		b &= ~_BV(2);
	}
	if (video_memory[2] & mask) {
		b |= _BV(3);
	} else {
		b &= ~_BV(3);
	}
	if (video_memory[3] & mask) {
		b |= _BV(4);
	} else {
		b &= ~_BV(4);
	}
	PORTB = 0;
	PORTD = ~mask;
	PORTB = b;
}


static inline void keyboardCheck() {
	if (!(PINC & _BV(2))) {
		if (key_pressed[KEY_PLUS] < 0xff) {
			key_pressed[KEY_PLUS]++;
			if (key_pressed[KEY_PLUS] == KEY_PRESSED_COUNT) {
				onKeyPressed(KEY_PLUS);
			}
		}
	} else {
		key_pressed[KEY_PLUS] = 0;
	}
	if (!(PINC & _BV(3))) {
		if (key_pressed[KEY_MINUS] < 0xff) {
			key_pressed[KEY_MINUS]++;
			if (key_pressed[KEY_MINUS] == KEY_PRESSED_COUNT) {
				onKeyPressed(KEY_MINUS);
			}
		}
		
	} else {
		key_pressed[KEY_MINUS] = 0;
	}
	if (!(PINC & _BV(4))) {
		if (key_pressed[KEY_ENTER] < 0xff) {
			key_pressed[KEY_ENTER]++;
			if (key_pressed[KEY_ENTER] == KEY_PRESSED_COUNT) {
				onKeyPressed(KEY_ENTER);
			}			
		}
	} else {
		key_pressed[KEY_ENTER] = 0;
	}
	if (!(PINC & _BV(5))) {
		if (key_pressed[KEY_BACK] < 0xff) {
			key_pressed[KEY_BACK]++;
			if (key_pressed[KEY_BACK] == KEY_PRESSED_COUNT) {
				onKeyPressed(KEY_BACK);
			}			
		}
	} else {
		key_pressed[KEY_BACK] = 0;
	}
	
}



void displayOutTime(uint16_t timeInSec) {
	bool showFirstZerro;
	uint16_t displayTime;
	if (timeInSec > 60*60) {
		displayTime = timeInSec / 60;
		showFirstZerro = true;
	} else {
		displayTime = timeInSec;
		showFirstZerro = false;
	}
	uint8_t min = displayTime / 60;
	uint8_t sec = displayTime % 60;
	if (min < 10 && !showFirstZerro) {
		video_memory[0] = 0;
	} else {
		video_memory[0] = pgm_read_byte(DISPLAY_SYMBOLS + min / 10);
	}
	video_memory[1] = pgm_read_byte(DISPLAY_SYMBOLS + min % 10);
	video_memory[2] = pgm_read_byte(DISPLAY_SYMBOLS + sec / 10);
	video_memory[3] = pgm_read_byte(DISPLAY_SYMBOLS + sec % 10);
}


void displayOutSeparator(bool enable) {
	if (enable) {
		video_memory[1] |= _BV(7);
	} else {
		video_memory[1] &= ~_BV(7);
	}
}


void displayClear() {
	video_memory[0] = 0;
	video_memory[1] = 0;
	video_memory[2] = 0;
	video_memory[3] = 0;
}

static void onKeyPressed(uint8_t key) {
	switch(key) {
		case KEY_PLUS:
			if (timer_mode == TIMER_MODE_WAIT && timer_interval < MAX_TIME_INTERVAL_MINUTES) {
				timer_interval++;
			}
			break;
		case KEY_MINUS:
			if (timer_mode == TIMER_MODE_WAIT && timer_interval  > 0) {
				timer_interval--;
			}
			break;
		case KEY_ENTER:
			if (timer_mode == TIMER_MODE_WAIT) {
				eeprom_update_word(EEPROM_OFFSET_TIME, timer_interval);
				time = timer_interval * 60;
				enableLed(true);
				enablePower(true);
				timer_mode = TIMER_MODE_RUN;
			}
			break;
		case KEY_BACK:
			if (timer_mode == TIMER_MODE_RUN) {
				timer_mode = TIMER_MODE_WAIT;
				enableLed(false);
				enablePower(false);				
			}
			break;
	}
}





ISR(TIMER1_COMPA_vect) {
	OCR1A += TIMER2_DELTA_COUNT;
	
	if (++hsec >= 100) {
		if (time == 0) {
			timer_mode = TIMER_MODE_WAIT;
			enableLed(false);
			enablePower(false);
		}
		time--;
		hsec = 0;
	}

}

int main(void) {
	DDRD = 0xff;
	DDRB = 0xff;
	DDRC = 0;
	
	PORTC = _BV(2) | _BV(3) | _BV(4) | _BV(5);	// keyboard pull-up
	

	TCCR1A = 0;
	TCCR1B = _BV(CS11) | _BV(CS10);
	
	TIMSK = _BV(OCIE1A);
	
	sei();
	enableLed(false);
	enablePower(false);
	displayOutTime(0);
	
	timer_interval = eeprom_read_word(EEPROM_OFFSET_TIME);
	if (timer_interval > MAX_TIME_INTERVAL_MINUTES || timer_interval == 0) {
		timer_interval = 10;
	}
	
    while(1) {
		keyboardCheck();
		if (timer_mode == TIMER_MODE_WAIT) {
			if (key_pressed[KEY_PLUS] == 0xff && timer_interval < MAX_TIME_INTERVAL_MINUTES) {
				timer_interval++;
				key_pressed[KEY_PLUS] = 220;
			} else if (key_pressed[KEY_MINUS] == 0xff && timer_interval > 0) {
				timer_interval--;
				key_pressed[KEY_MINUS] = 220;
			}
		}
		for (uint8_t i = 0; i < 8; i++) {
			displayUpdate(i);
			if (timer_mode == TIMER_MODE_RUN) {
				displayOutTime(time);
				displayOutSeparator(hsec < 50);
				if (time < 30) {
					enableLed(hsec < 25 || (hsec > 50 && hsec < 75));	
				} else if (time < 60) {
					enableLed(hsec < 75);
				}
			} else {
				displayOutTime(timer_interval);
				displayOutSeparator(true);
				//displayOutSeparator(hsec < 25 || (hsec > 50 && hsec < 75));
				//if (hsec < 25 || (hsec > 50 && hsec < 75)) {
					//displayOutTime(timer_interval);
					//displayOutSeparator(true);
				//} else {
					//displayClear();
				//}
			}
			_delay_us(500);
		}
    }
}