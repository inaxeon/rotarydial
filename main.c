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
// Modified     : Donald Froula 2019-07-31
//                http://projectmf.homelinux.com/pulsetotone
//                Fixed not returning to normal dialing mode after * or # dialing or redial function.
//                Added support for storing * and # in both redial and speed dial memories.
//                Added hotline mode and EEPROM storage, shortened time to dial # and *.
//                Added programmable digit length and hotdelay delay, EEPROM saved.
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************

// Uncomment to build with reverse dial
//#define NZ_DIAL

#include <stdbool.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <avr/eeprom.h>

#include "dtmf.h" 

#define PIN_DIAL                    PB1
#define PIN_PULSE                   PB2

#define SPEED_DIAL_SIZE             32

#define STATE_DIAL                  0x00
#define STATE_SPECIAL_L1            0x01
#define STATE_SPECIAL_L2            0x02
#define STATE_PROGRAM_SD            0x03

#define F_NONE                      0x00
#define F_DETECT_SPECIAL_L1         0x01
#define F_DETECT_SPECIAL_L2         0x02
#define F_WDT_AWAKE                 0x04

#define SLEEP_64MS                  0x00
#define SLEEP_128MS                 0x01
#define SLEEP_500MS                 0x02
#define SLEEP_1S                    0x03
#define SLEEP_2S                    0x04

#define SPEED_DIAL_COUNT            8 // 8 Positions in total (Redail(3),4,5,6,7,8,9,0)
#define SPEED_DIAL_REDIAL           (SPEED_DIAL_COUNT - 1)

#define L2_STAR                     1
#define L2_POUND                    2
#define L2_REDIAL                   3

#define FEAT_HOTLINE                0    //Bit position, not value

#define DTMF_DURATION_UNIT          50   //Base unit of DTMF duration in milliseconds

#define FEAT_EE                     511  //Storage for feature flag - last location in EEPROM
#define HOTLINE_DELAY_EE            510  //Storage for hotline delay
#define DTMF_DURATION_EE            509  //Storage for DTMF duration

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

int8_t pending_digit = 0;     //(DRF) Stores * or # for injecting into main loop after dialing so they are picked up by the redial and programming functions.
int8_t prev_state;            //(DRF) Keeps track of the previous state whenever a state change occurs.
uint8_t feature_flags = 0;    //(DRF) Feature flags
uint8_t hotline_delay = 1;    //(DRF) Actual delay is 1000ms * this value, 1 second default
uint8_t dtmf_duration = 2;    //(DRF) DTMF tone and space duration * 50 milliseconds (default 100ms)

static void init(void);
static void process_dialed_digit(runstate_t *rs);
static void dial_speed_dial_number(int8_t *speed_dial_digits, int8_t index);
static void write_current_speed_dial(int8_t *speed_dial_digits, int8_t index);
static void wdt_timer_start(uint8_t delay);
static void start_sleep(void);
static void wdt_stop(void);

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

    // Wait for the decoupling capacitors to charge
    wdt_timer_start(SLEEP_128MS);
    start_sleep();
    wdt_stop();

    dtmf_init();

    // Local dial status variables 
    rs->state = STATE_DIAL;
    rs->dial_pin_state = true;
    rs->flags = F_NONE;
    rs->speed_dial_digit_index = 0;
    rs->speed_dial_index = 0;
    dial_pin_prev_state = true;
    
    for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
        rs->speed_dial_digits[i] = DIGIT_OFF;
	
     //(DRF)Retrieve feature flags
    feature_flags = eeprom_read_byte((uint8_t*)FEAT_EE);

	//Initialize EEPROM location, if needed
	if(feature_flags > (1 << FEAT_HOTLINE)){
		feature_flags = 0;
	    eeprom_write_byte((uint8_t*)FEAT_EE, feature_flags);
	    }		
		
	//Retrieve hotline delay
	hotline_delay = eeprom_read_byte((uint8_t*)HOTLINE_DELAY_EE);
	
	//Initialize EEPROM location, if needed
	if((hotline_delay > 4) || (hotline_delay < 1)){
		hotline_delay = 1;
		eeprom_write_byte((uint8_t*)HOTLINE_DELAY_EE, hotline_delay);
		} 
		
	//Retrieve DTMF duration
	dtmf_duration = eeprom_read_byte((uint8_t*)DTMF_DURATION_EE);
	
	//Initialize EEPROM location, if needed
	if((dtmf_duration > 4) || (dtmf_duration < 1)){
		dtmf_duration = 2;      //* Initialize to 100ms
		eeprom_write_byte((uint8_t*)DTMF_DURATION_EE, dtmf_duration);
		} 
	
	//(DRF)For Hotline operation, dial location zero on powerup if enabled
	if(feature_flags & (1 << FEAT_HOTLINE)){sleep_ms(hotline_delay * 1000); dial_speed_dial_number(rs->speed_dial_digits, _g_speed_dial_loc[0]);}

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

                wdt_timer_start(SLEEP_64MS);
                start_sleep();
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
                    wdt_timer_start(SLEEP_64MS);
                    start_sleep();
                }
                else 
                {
                    // Got a valid digit - process it            
#ifdef NZ_DIAL
                    // NZPO Phones only. 0 is same as GPO but 1-9 are reversed.
                    rs->dialed_digit = (10 - rs->dialed_digit);
#else
                    if (rs->dialed_digit == 10)
                        rs->dialed_digit = 0; // 10 pulses => 0
#endif
                    wdt_timer_start(SLEEP_128MS);
                    start_sleep();
                    wdt_stop();

                    process_dialed_digit(rs);
                }
            }    
        } 
        else 
        {
            if (rs->dial_pin_state) 
            {
                // Rotary dial at the rest position
                // Reset all variablesrs->state
				prev_state = rs->state;
                rs->state = STATE_DIAL;
                rs->flags = F_NONE;
                rs->dialed_digit = DIGIT_OFF;
            }
        }

        dial_pin_prev_state = rs->dial_pin_state;
		
		//(DRF) If a pending * or # digit has been saved, play it immediately after returning to normal dial mode
		if(pending_digit){rs->dialed_digit = pending_digit; pending_digit = 0; process_dialed_digit(rs);}

        // Don't power down if special function detection is active        
        if (rs->flags & F_DETECT_SPECIAL_L1)
        {
            // Put MCU to sleep - to be awoken either by pin interrupt or WDT
            wdt_timer_start(SLEEP_2S); 
            start_sleep();

            // Special function mode detected?
            if (rs->flags & F_WDT_AWAKE)
            {
                // SF mode detected
                rs->flags &= ~F_WDT_AWAKE;
				prev_state = rs->state;
                rs->state = STATE_SPECIAL_L1;
                rs->flags &= ~F_DETECT_SPECIAL_L1;
                rs->flags |= F_DETECT_SPECIAL_L2;

                // Indicate that we entered L1 SF mode with short beep
                dtmf_generate_tone(DIGIT_BEEP_LOW, 200);
            }
        }
        else if (rs->flags & F_DETECT_SPECIAL_L2)
        {
            // Put MCU to sleep - to be awoken either by pin interrupt or WDT
            wdt_timer_start(SLEEP_2S);
            start_sleep();

            if (rs->flags & F_WDT_AWAKE)
            {
                // SF mode detected
                rs->flags &= ~F_WDT_AWAKE;
				prev_state = rs->state;
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
        dtmf_generate_tone(rs->dialed_digit, dtmf_duration * DTMF_DURATION_UNIT);

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
            //dtmf_generate_tone(DIGIT_STAR, dtmf_duration * DTMF_DURATION_UNIT);  
			pending_digit = DIGIT_STAR;   //(DRF) Just push the tone to the pending_digit variable to be played immediately when returned to STATE_DIAL
        }
        else if (rs->dialed_digit == L2_POUND)
        {
            // SF 2-#
            //dtmf_generate_tone(DIGIT_POUND, dtmf_duration * DTMF_DURATION_UNIT);  
			pending_digit = DIGIT_POUND;  //(DRF) Just push the tone to the pending_digit variable to be played immediately when returned to STATE_DIAL
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
		rs->state = prev_state;  //(DRF) Return to the previous state after entering STATE_SPECIAL_L1. Allows normal dialing after using 
		                         //special functions, not possible before the fix. Also allows * and # to be programmed into speed dial,
								 //returning to programming mode if a * or # is dialed while programming.
    }
    else if (rs->state == STATE_SPECIAL_L2)
    {
        if (_g_speed_dial_loc[rs->dialed_digit] >= 0)
        {
            rs->speed_dial_index = _g_speed_dial_loc[rs->dialed_digit];
            rs->speed_dial_digit_index = 0;

            for (uint8_t i = 0; i < SPEED_DIAL_SIZE; i++)
                rs->speed_dial_digits[i] = DIGIT_OFF;
            prev_state = rs->state;
            rs->state = STATE_PROGRAM_SD;
        }
	    //Set Hotline flag and cycle through delays
        else if(rs->dialed_digit == 1){
			if(feature_flags & (1 << FEAT_HOTLINE)){  //If feature is "on"...
			    hotline_delay += 1;  //Increase the delay by one second
			    if(hotline_delay > 4){
				   hotline_delay = 1;				
				   feature_flags &= ~(1 << FEAT_HOTLINE);  //Turn off the hotline feature if more than 4 seconds
				   dtmf_generate_tone(DIGIT_TUNE_DESC2, 800);
				   }
				 else{
					 for(int i=1; i<=hotline_delay; i++){ //Beep out number of seconds set
						 dtmf_generate_tone(DIGIT_BEEP_LOW, 200); 
						 sleep_ms(200);
						 } //Beep out number of seconds set
				     }
			}
			else {  //If hotline feature is off
				feature_flags |= (1 << FEAT_HOTLINE); //Turn it on
				for(int i=1; i<=hotline_delay; i++){
					if(i == 1){dtmf_generate_tone(DIGIT_TUNE_ASC2, 800); sleep_ms(200);}
					dtmf_generate_tone(DIGIT_BEEP_LOW, 200); 
					sleep_ms(200);
					} //Beep out number of seconds set
				}
			eeprom_write_byte((uint8_t*)FEAT_EE,feature_flags);		
			eeprom_write_byte((uint8_t*)HOTLINE_DELAY_EE, hotline_delay);
			prev_state = rs->state;
            rs->state = STATE_DIAL;
			   }
	    //Cycle through DTMF duration settings in 50ms steps
        else if(rs->dialed_digit == 2){
			    dtmf_duration += 1;  //Increase the duration by 50ms
			    if(dtmf_duration > 4){dtmf_duration = 1;}
                for(int i=1; i<=dtmf_duration; i++){ //Beep out number of 50ms units set
						 dtmf_generate_tone(DIGIT_BEEP, 200); 
						 sleep_ms(200);
						 } 			   
			eeprom_write_byte((uint8_t*)DTMF_DURATION_EE, dtmf_duration);
			prev_state = rs->state;
            rs->state = STATE_DIAL;
			}			   
        else
        {
            // Not a speed dial position. Revert back to ordinary dial
            prev_state = rs->state;			
            rs->state = STATE_DIAL;
        }
    }
    else if (rs->state == STATE_PROGRAM_SD)
    {
        // Do we have too many digits entered?
        if (rs->speed_dial_digit_index >= SPEED_DIAL_SIZE)
        {
            // Exit speed dial mode
			prev_state = rs->state;
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
            dtmf_generate_tone(DIGIT_BEEP_LOW, dtmf_duration * DTMF_DURATION_UNIT);
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
                dtmf_generate_tone(speed_dial_digits[i], dtmf_duration * DTMF_DURATION_UNIT);  
                // Pause between DTMF tones
                sleep_ms(dtmf_duration * DTMF_DURATION_UNIT);    
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
    CLKPR = 0;

    // Enable pull-ups
    PORTB |= (_BV(PIN_DIAL) | _BV(PIN_PULSE));

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

static void wdt_timer_start(uint8_t delay)
{
    wdt_reset();
    cli();
    MCUSR = 0x00;
    WDTCR |= _BV(WDCE) | _BV(WDE);
    switch (delay)
    {
        case SLEEP_64MS:
            WDTCR = _BV(WDIE) | _BV(WDP1);
            break;
        case SLEEP_128MS:
            WDTCR = _BV(WDIE) | _BV(WDP1) | _BV(WDP0);
            break;
        case SLEEP_500MS:
            WDTCR = _BV(WDIE) | _BV(WDP0) | _BV(WDP2); // 500ms
		case SLEEP_1S:
            WDTCR = _BV(WDIE) | _BV(WDP1) | _BV(WDP2); // 1024ms
            break;
        case SLEEP_2S:
            WDTCR = _BV(WDIE) | _BV(WDP0) | _BV(WDP1) | _BV(WDP2); // 2048ms
            break;
    }
    sei();
}

static void wdt_stop(void)
{
    wdt_reset();
    cli();
    MCUSR = 0x00;
    WDTCR |= _BV(WDCE) | _BV(WDE);
    WDTCR = 0x00;
    sei();
}

static void start_sleep(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    cli();                          // stop interrupts to ensure the BOD timed sequence executes as required
    sleep_enable();
    sleep_bod_disable();            // disable brown-out detection (good for 20-25µA)
    sei();                          // ensure interrupts enabled so we can wake up again
    sleep_cpu();                    // go to sleep
    sleep_disable();                // wake up here
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

// Interrupt initiated by pin change on any enabled pin
ISR(PCINT0_vect)
{
}

// Handler for any unspecified 'bad' interrupts
ISR(BADISR_vect)
{
    // Do nothing, just wake up MCU
}

ISR(WDT_vect)
{
    _g_run_state.flags |= F_WDT_AWAKE;
}
