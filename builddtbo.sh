#!/bin/sh
DTC_FLAGS="-b 0 -Wno-unit_address_vs_reg -I dts -O dtb"

dtc -@ $DTC_FLAGS -o seeed-4mic-voicecard.dtbo seeed-4mic-voicecard-overlay.dts

# cp *.dtbo /boot/overlays
