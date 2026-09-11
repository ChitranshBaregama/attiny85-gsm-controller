.PHONY: all firmware test size clean

all: test firmware

test:
	@$(MAKE) -s -C tests

firmware:
	@$(MAKE) -s -C firmware

size: firmware

clean:
	@$(MAKE) -s -C tests clean
	@$(MAKE) -s -C firmware clean
