//*****************************************************************************
// Title        : Pulse to tone (DTMF) converter
// Author       : Boris Cherkasskiy
//                http://boris0.blogspot.ca/2013/09/rotary-dial-for-digital-age.html
// Created      : 2011-10-24
//
// Modified     : Arnie Weber 2015-06-22
//                https://bitbucket.org/310weber/rotary_dial/
//                NOTE: This code is not compatible with Boris's original hardware
//                due to changed pin-out (see Eagle files for details)
//
// Modified     : Matthew Millman 2018-05-29
//                http://tech.mattmillman.com/
//                Cleaned up implementation, modified to work more like the
//                Rotatone product.
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************

#ifndef __DTMF_H__
#define __DTMF_H__

#define DIGIT_BEEP          -10
#define DIGIT_BEEP_LOW      -13
#define DIGIT_TUNE_ASC      -11
#define DIGIT_TUNE_DESC     -12
#define DIGIT_TUNE_ASC2     -14
#define DIGIT_TUNE_DESC2    -15
#define DIGIT_OFF           -1
#define DIGIT_STAR          10
#define DIGIT_POUND         11
#define DIGIT_PRE2600       12  //Use to play pending 2600 tone

#define DIGIT_MF0           13
#define DIGIT_MF1           14
#define DIGIT_MF2           15
#define DIGIT_MF3           16
#define DIGIT_MF4           17
#define DIGIT_MF5           18
#define DIGIT_MF6           19
#define DIGIT_MF7           20
#define DIGIT_MF8           21
#define DIGIT_MF9           22
#define DIGIT_MFKP          23
#define DIGIT_MFST          24

#define DIGIT_2600          25

#define DTMF_DURATION_MS    100
#define MF_DURATION_MS      100
#define DURATION_MS_2600    1500

// PWM frequency = 4Mhz/256 = 15625Hz; overflow cycles per MS = 15
#define T0_OVERFLOW_PER_MS  15

#define PIN_PWM_OUT         PB0     // PB0 (OC0A) as PWM output

void dtmf_init(void);
void dtmf_generate_tone(int8_t digit, uint16_t duration_ms);
void sleep_ms(uint16_t msec);

extern volatile uint32_t _g_delay_counter;

#endif /* __DTMF_H__ */


