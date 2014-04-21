#!/bin/sh

sudo i2cset -y 1 0x24 0x03 0xBF
sudo i2cset -y 1 0x24 0x01 0x00
cat data.eeprom > /sys/bus/i2c/drivers/at24/1-0054/eeprom
