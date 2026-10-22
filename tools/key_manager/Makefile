.PHONY: help install reinstall

.DEFAULT_GOAL := help

## Show this help message.
help:
	@echo "FidoLock Firmware — available targets:"
	@echo ""
	@awk 'BEGIN{FS=":"} /^## /{h=substr($$0,4)} /^[a-zA-Z_-]+:/{if(h) printf "  %-18s %s\n", $$1, h; h=""}' \
		$(MAKEFILE_LIST)
	@echo ""
	@echo "Override board environment: make <target> ENV=esp32-s3  (default: $(ENV))"

.venv:
	python -m venv .venv

## Install the package in the current virtual environment.
install: .venv
	.venv/bin/pip install .

## Reinstall the package in the current virtual environment (useful for development).
reinstall: .venv
	.venv/bin/pip install .
