# Host tests for the protocol logic. No hardware, no emulator.
#
#   make          build and run
#   make clean
#
# The sketch is #included rather than linked, which reaches its static
# functions and - the important part - tests the shipping source rather
# than a copy that would drift.

CXX    ?= g++
SHIM   := avr-host-shim
CXXFLAGS := -std=gnu++11 -O1 -g -Wall -Wextra -I$(SHIM)

.PHONY: all clean
all: test_protocol
	@./test_protocol

test_protocol: test_protocol.cpp host_regs.c ../firmware/ANANT_IONS_SIM900A_ATTINY85_V11.ino
	$(CXX) $(CXXFLAGS) test_protocol.cpp host_regs.c -o $@

clean:
	rm -f test_protocol
