This code is a branch from Matthew Millman's excellent Pulse > DTMF converter project, which converts a rotary dial telephone to DTMF operation. This branch adds a "blue box" or MF multifrequency mode.

I cleaned up a few operating anomalies and added adjustable DTMF tone length (saved in EEPROM) and a "hotline" mode that auto-dials the number stored in speed dial location zero whenever the phone is taken off-hook.

The time between going off-hook and the start of dialing is adjustable and also saved in EEPROM.

I updated the hardware design schematics to include a diode bridge for polarity protection, as the voltage regulator used is subject to damage if the telephone wiring is polarity reversed from the standard arrangement.

I also added a zener diode to the circuit to assure sufficient voltage is available to the input of the voltage regulator for most telephones and loop currents.

Without the zener diode, certain ATAs with low voltage/current or telephones with low internal network resistance would not develop enough voltage drop to power the unit reliably.

Bugs fixed are the failure to return to normal dialing mode after speed dialing or playing the * or # tones. This made it impossible to use DTMF on a connected call after initial dialing.

I also made it possible to save the * and # tones in speed dial and last number memories.

I also added a "blue box" mode that adds the multifrequency dual-tone "MF" signals historically used by the US long distance network.

"Phone phreaks" could use these tones in conjunction with the 2600Hz supervisory tone to take over a long distance call and re-route it, for free. Google for additional info.

I run an Asterisk server at +1 630-485-2995 which accepts these tones and allows blue box phreaking in a legal environment using a blue box or this circuit with a rotary phone.

Blue box mode is entered by hold-dialing digit 3 for four seconds. MF mode is signified by ascending beeps.

In blue box mode, normal dialing results in the special MF tones. The dialing sequence for * and # generates "KP" and "ST" tones.

Last number dialed recall is disabled in MF mode. Instead, hold-dialing digit 3 produces a 2600Hz tone to seize a trunk.

With this code version, the tone mode (DTMF or MF) is stored in EEPROM memory. Therefore, a mix of DTMF and MF tone sequences may be saved in the speed dial memories. 2600Hz may also be saved and played back.

Both DTMF and MF tone sequences in speed dial memories may be played back in either DTMF or MF mode.

Exit MF mode by hold-dialing digit 3 for 4 seconds. Descending tones indicate entry to DTMF mode.

The hot line number will always play back in DTMF mode. Memory zero should be programmed in DTMF mode if using the hotdial feature.

Numbers are not saved to the last number dialed memory while in MF mode and are therefor retained when returning to the DTMF mode.
