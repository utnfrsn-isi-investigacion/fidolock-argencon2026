# FidoLock Firmware — convenience wrapper around PlatformIO CLI
#
# Usage:
#   make flash-monitor   Upload firmware and immediately open serial monitor
#   make flash           Upload firmware only
#   make monitor         Open serial monitor only
#   make build           Compile without uploading
#   make test            Run native unit tests
#   make clean           Remove build artefacts
#
# Build-flag overrides (pass as environment variables or on the make command line):
#   LOG_LEVEL=<0-5>          ESP-IDF log verbosity (default: 3 / INFO)
#   DEBUG_NFC=1              Enable verbose NFC frame logging (default: 0)
#   PERFORMANCE_MONITOR=1    Print [TIMING] metrics for each auth phase to serial
#                            (detection, challenge, ECDSA verify, total)

ENV            ?= esp32dev
PIO            := pio
MONITOR_FILTER := default
LOG_LEVEL          ?= 3
DEBUG_NFC          ?= 0
PERFORMANCE_MONITOR ?= 0

export LOG_LEVEL DEBUG_NFC PERFORMANCE_MONITOR

.DEFAULT_GOAL := help

.PHONY: help flash-monitor flash uploadfs monitor build test clean ports check

## Show this help message.
help:
	@echo "FidoLock Firmware — available targets:"
	@echo ""
	@awk 'BEGIN{FS=":"} /^## /{h=substr($$0,4)} /^[a-zA-Z_-]+:/{if(h) printf "  %-18s %s\n", $$1, h; h=""}' \
		$(MAKEFILE_LIST)
	@echo ""
	@echo "Override board environment:  make <target> ENV=esp32-s3          (default: $(ENV))"
	@echo "Override build flags:        make <target> PERFORMANCE_MONITOR=1"
	@echo "                             make <target> LOG_LEVEL=4"
	@echo "                             make <target> DEBUG_NFC=1"

## Upload firmware then immediately open the serial monitor.
flash-monitor:
	$(PIO) run --environment $(ENV) --target upload && \
	$(PIO) device monitor --filter $(MONITOR_FILTER)

## Upload firmware only.
flash:
	$(PIO) run --environment $(ENV) --target upload

## Flash LittleFS filesystem image from firmware/data/ (run after make flash on first flash).
uploadfs:
	$(PIO) run --environment $(ENV) --target uploadfs

## Open serial monitor only (board must already be running).
monitor:
	$(PIO) device monitor --filter $(MONITOR_FILTER)

## Compile without uploading.
build:
	$(PIO) run --environment $(ENV)

## Run native unit tests.
test:
	$(PIO) test --environment native

## Remove PlatformIO build artefacts.
clean:
	$(PIO) run --target clean

## List available serial ports.
ports:
	$(PIO) device list

## Check device connectivity (e.g. after plugging in or resetting the board).
check:
	$(PIO) device list | grep -E 'usbserial|ttyUSB|ttyACM' || (echo "No serial devices found" && exit 1)
