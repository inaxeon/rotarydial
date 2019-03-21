##############################################################################
# Title        : AVR Makefile for Windows
#
# Created      : Matthew Millman 2018-05-29
#                http://tech.mattmillman.com/
#
# This code is distributed under the GNU Public License
# which can be found at http://www.gnu.org/licenses/gpl.txt
#
##############################################################################

# Fixes clash between windows and coreutils mkdir. Comment out the below line to compile on Linux
COREUTILS  = C:/Projects/coreutils/bin/

DEVICE     = attiny85
CLOCK      = 4000000
PROGRAMMER = -c stk500 -P COM10 
SRCS       = main.c dtmf.c
OBJS       = $(SRCS:.c=.o)
FUSES      = -U lfuse:w:0xFD:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
DEPDIR     = deps
DEPFLAGS   = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
RM         = rm
MV         = mv
MKDIR      = $(COREUTILS)mkdir

POSTCOMPILE = $(MV) $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE)
COMPILE = avr-gcc -Wall -Os $(DEPFLAGS) -DF_CPU=$(CLOCK) -mmcu=$(DEVICE)

all: main.hex

.c.o: $(DEPDIR)/%.d
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -c $< -o $@
	@$(POSTCOMPILE)

.S.o: $(DEPDIR)/%.d
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
	@$(POSTCOMPILE)

.c.s: $(DEPDIR)/%.d
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -S $< -o $@
	@$(POSTCOMPILE)

flash: all
	$(AVRDUDE) -U flash:w:main.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

install: flash fuse

clean:
	$(RM) -f main.hex main.elf $(OBJS)
	$(RM) -rf deps

main.elf: $(OBJS)
	$(COMPILE) -o main.elf $(OBJS)

main.hex: main.elf
	avr-objcopy -j .text -j .data -O ihex main.elf main.hex

disasm:	main.elf
	avr-objdump -d main.elf

cpp:
	$(COMPILE) -E $(SRCS)

$(DEPDIR)/%.d:
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS))))