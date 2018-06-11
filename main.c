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
//                Rotatone commercial product.
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <avr/eeprom.h>

#include "dtmf.h" 

#define PIN_DIAL                    PB1
#define PIN_PULSE                   PB2

#define SF_DELAY_MS                 2000L
#define PG_DELAY_MS                 4000L
#define SPEED_DIAL_SIZE             30

#define STATE_DIAL                  0x00
#define STATE_SPECIAL_L1            0x01
#define STATE_SPECIAL_L2            0x02
#define STATE_PROGRAM_SD            0x03

#define F_NONE                      0x00
#define F_DETECT_SPECIAL_L1         0x01
#define F_DETECT_SPECIAL_L2         0x02

#define SPEED_DIAL_COUNT            8 // 8 Positions in total (Redail(3),4,5,6,7,8,9,0)
#define SPEED_DIAL_REDIAL           (SPEED_DIAL_COUNT - 1)

#define L2_STAR                     1
#define L2_POUND                    2
#define L2_REDIAL                   3

typedef struct
{
    uint8_t state;
    uint8_t flags;
    bool dial_pin_state;
    uint8_t speed_dial_index;
    uint8_t speed_dial_digit_index;
    int8_t speed_dial_digits[SPEED_DIAL_SIZE];
    int8_t dialed_digit;
} runstate_t;

static void init(void);
static void process_dialed_digit(runstate_t *rs);
static void dial_speed_dial_number(int8_t *speed_dial_digits, int8_t index);
static void write_current_speed_dial(int8_t *speed_dial_digits, int8_t index);

// Map speed dial numbers to memory locations
const int8_t _g_speed_dial_loc[] =
{
    0,
    -1 /* 1 - * */,
    -1 /* 2 - # */,
    -1 /* 3 - Redial */,
    1,
    2,
    3,
    4,
    5,
    6 
};

int8_t EEMEM _g_speed_dial_eeprom[SPEED_DIAL_COUNT][SPEED_DIAL_SIZE] = { [0 ... (SPEED_DIAL_COUNT - 1)][0 ... SPEED_DIAL_SIZE - 1] = DIGIT_OFF };
runstate_t _g_run_state;

int main(void)
{
    runstate_t *rs = &_g_run_state;
    bool dial_pin_prev_state;

    init();
    dtmf_init();

    // Turn PWM OFF
    dtmf_generate_tone(DIGIT_OFF, 0); 

    // Local dial status variables 
    rs->state = STATE_DIAL;
    rs->dial_pin_state = true;
    rs->flags = F_NONE;
    rs->speed_dial_digit_index = 0;
    rs->speed_dial_index = 0;
    dial_pin_prev_state = true;
    
    for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
        rs->speed_dial_digits[i] = DIGIT_OFF;

    while (1)
    {
        rs->dial_pin_state = bit_is_set(PINB, PIN_DIAL);

        if (dial_pin_prev_state != rs->dial_pin_state) 
        {
            if (!rs->dial_pin_state) 
            {
                // Dial just started
                // Enable special function detection
                rs->flags |= F_DETECT_SPECIAL_L1;
                rs->dialed_digit = 0;

                sleep_ms(50);
            }
            else 
            {
                // Disable SF detection (should be already disabled)
                rs->flags = F_NONE;

                // Check that we detect a valid digit
                if (rs->dialed_digit <= 0 || rs->dialed_digit > 10)
                {
                    // Should never happen - no pulses detected OR count more than 10 pulses
                    rs->dialed_digit = DIGIT_OFF;                    
                    // Do nothing
                    sleep_ms(50);
                }
                else 
                {
                    // Got a valid digit - process it            
                    if (rs->dialed_digit == 10)
                        rs->dialed_digit = 0; // 10 pulses => 0

                    process_dialed_digit(rs);
                }
            }    
        } 
        else 
        {
            if (rs->dial_pin_state) 
            {
                // Rotary dial at the rest position
                // Reset all variables
                rs->state = STATE_DIAL;
                rs->flags = F_NONE;
                rs->dialed_digit = DIGIT_OFF;
            }
        }

        dial_pin_prev_state = rs->dial_pin_state;

        // Don't power down if special function detection is active        
        if (rs->flags & F_DETECT_SPECIAL_L1)
        {
            // SF detection in progress - we need timer to run (IDLE mode)
            set_sleep_mode(SLEEP_MODE_IDLE);        
            sleep_mode();

            // Special function mode detected?
            if (_g_delay_counter >= SF_DELAY_MS * T0_OVERFLOW_PER_MS)
            {
                // SF mode detected
                rs->state = STATE_SPECIAL_L1;
                rs->flags &= ~F_DETECT_SPECIAL_L1;
                rs->flags |= F_DETECT_SPECIAL_L2;

                // Indicate that we entered L1 SF mode with short beep
                dtmf_generate_tone(DIGIT_BEEP_LOW, 200);
            }
        }
        else if (rs->flags & F_DETECT_SPECIAL_L2)
        {
            set_sleep_mode(SLEEP_MODE_IDLE);        
            sleep_mode();

            if (_g_delay_counter >= PG_DELAY_MS * T0_OVERFLOW_PER_MS)
            {
                // SF mode detected
                rs->state = STATE_SPECIAL_L2;
                rs->flags &= ~F_DETECT_SPECIAL_L2;

                // Indicate that we entered L2 SF mode with asc tone
                dtmf_generate_tone(DIGIT_TUNE_ASC, 200);
            }
        }
        else
        {
            // Don't need timer - sleep to power down mode
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_mode();
        }
    }

    return 0;
}

static void process_dialed_digit(runstate_t *rs)
{
    if (rs->state == STATE_DIAL)
    {
        // Standard (no speed dial, no special function) mode
        // Generate DTMF code
        dtmf_generate_tone(rs->dialed_digit, DTMF_DURATION_MS);

        if (rs->speed_dial_digit_index < SPEED_DIAL_SIZE)
        {
            // During regular dial always save into the 'Redial' position of the speed dial memory
            rs->speed_dial_digits[rs->speed_dial_digit_index] = rs->dialed_digit;
            rs->speed_dial_digit_index++;
            
            write_current_speed_dial(rs->speed_dial_digits, SPEED_DIAL_REDIAL);
        }
    }
    else if (rs->state == STATE_SPECIAL_L1)
    {
        if (rs->dialed_digit == L2_STAR)
        {
            // SF 1-*
            dtmf_generate_tone(DIGIT_STAR, DTMF_DURATION_MS);  
        }
        else if (rs->dialed_digit == L2_POUND)
        {
            // SF 2-#
            dtmf_generate_tone(DIGIT_POUND, DTMF_DURATION_MS);  
        }
        else if (rs->dialed_digit == L2_REDIAL)
        {
            // SF 3 (Redial)
            dial_speed_dial_number(rs->speed_dial_digits, SPEED_DIAL_REDIAL);
        }
        else if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            // Call speed dial number
            dial_speed_dial_number(rs->speed_dial_digits, _g_speed_dial_loc[rs->dialed_digit]);
        }
    }
    else if (rs->state == STATE_SPECIAL_L2)
    {
        if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            rs->speed_dial_index = _g_speed_dial_loc[rs->dialed_digit];
            rs->speed_dial_digit_index = 0;

            for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
                rs->speed_dial_digits[i] = DIGIT_OFF;

            rs->state = STATE_PROGRAM_SD;
        }
        else
        {
            // Not a speed dial position. Revert back to ordinary dial        
            rs->state = STATE_DIAL;
        }
    }
    else if (rs->state == STATE_PROGRAM_SD)
    {
        // Do we have too many digits entered?
        if (rs->speed_dial_digit_index >= SPEED_DIAL_SIZE)
        {
            // Exit speed dial mode
            rs->state = STATE_DIAL;
            // Beep to indicate that we done
            dtmf_generate_tone(DIGIT_TUNE_DESC, 800);
        } 
        else
        {
            // Next digit
            rs->speed_dial_digits[rs->speed_dial_digit_index] = rs->dialed_digit;
            rs->speed_dial_digit_index++;

            // Generic beep - do not gererate DTMF code
            dtmf_generate_tone(DIGIT_BEEP_LOW, DTMF_DURATION_MS);
        }

        // Write SD on every digit so user can hang up to save
        write_current_speed_dial(rs->speed_dial_digits, rs->speed_dial_index);
    }
}

// Dial speed dial number (it erases current SD number in the global structure)
static void dial_speed_dial_number(int8_t *speed_dial_digits, int8_t index)
{
    if (index >= 0 && index < SPEED_DIAL_COUNT)
    {
        eeprom_read_block(speed_dial_digits, &_g_speed_dial_eeprom[index][0], SPEED_DIAL_SIZE);

        for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
        {
            // Dial the number
            // Skip dialing invalid digits
            if (speed_dial_digits[i] >= 0 && speed_dial_digits[i] <= DIGIT_POUND)
            {
                dtmf_generate_tone(speed_dial_digits[i], DTMF_DURATION_MS);  
                // Pause between DTMF tones
                sleep_ms(DTMF_DURATION_MS);    
            }
        }
    }
}

static void write_current_speed_dial(int8_t *speed_dial_digits, int8_t index)
{
    if (index >= 0 && index < SPEED_DIAL_COUNT)
    {
        // If dialed index SPEED_DIAL_FIRST => using array index 0
        eeprom_update_block(speed_dial_digits, &_g_speed_dial_eeprom[index][0], SPEED_DIAL_SIZE);
    }
}

static void init(void)
{
    // Program clock prescaller to divide + frequency by 1
    // Write CLKPCE 1 and other bits 0    
    CLKPR = _BV(CLKPCE);    
    // Write prescaler value with CLKPCE = 0
    CLKPR = 0x00;
    // Disable Pull-ups - external HW debounce
    PORTB = 0;
    // Disable unused modules to save power
    PRR = _BV(PRTIM1) | _BV(PRUSI) | _BV(PRADC);
    ACSR = _BV(ACD);
    // Configure pin change interrupt
    MCUCR = _BV(ISC01) | _BV(ISC00);         // Set INT0 for falling edge detection
    GIMSK = _BV(INT0) | _BV(PCIE);           // Added INT0
    PCMSK = _BV(PIN_DIAL) | _BV(PIN_PULSE);
    // Enable interrupts
    sei();                              
}

// Handler for external interrupt on INT0 (PB2, pin 7)
ISR(INT0_vect)
{
    if (!_g_run_state.dial_pin_state)
    {
        // Disabling SF detection
        _g_run_state.flags = F_NONE;
        // A pulse just started
        _g_run_state.dialed_digit++;
    }
}

// Interrupt handlers updated to new code convention
// Interrupt initiated by pin change on any enabled pin
ISR(PCINT0_vect)
{
    // Do nothing, just wake up MCU
    _delay_us(100);
}

// Handler for any unspecified 'bad' interrupts
ISR(BADISR_vect)
{
    // Do nothing, just wake up MCU
    _delay_us(100);
}
