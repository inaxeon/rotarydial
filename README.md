This code is a branch from Matthew Millman's excellent Pulse > DTMF converter project, which converts a rotary dial telephone to DTMF operation.

I cleaned up a few operating anomalies and added adjustable DTMF tone length (saved in EEPROM) and a "hotline" mode that auto-dials the number stored in speed dial location zero whenever the phone is taken off-hook.

The time between going off-hook and the start of dialing is adjustable and also saved in EEPROM.

I updated the hardware design schematics to include a diode bridge for polarity protection, as the voltage regulator used is subject to damage if the telephone wiring is polarity reversed from the standard arrangement.

I also added a zener diode to the circuit to assure sufficient voltage is available to the input of the voltage regulator for most telephones and loop currents.

Without the zener diode, certain ATAs with low voltage/current or telephones with low internal network resistance would not develop enough voltage drop to power the unit reliably.

Bugs fixed are the failure to return to normal dialing mode after speed dialing or playing the * or # tones. This made it impossible to use DTMF on a connected call after initial dialing.

I also made it possible to save the * and # tones in speed dial and last number memories.
