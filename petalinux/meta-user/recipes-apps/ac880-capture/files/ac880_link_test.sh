#!/bin/sh
# AC880 capture-to-LCD waveform viewer for tty1.

TTY=/dev/tty1

printf '\033[2J\033[H' > "$TTY"
printf 'AC880 ACM108 waveform viewer\n' > "$TTY"
printf 'Waiting for capture DMA and framebuffer ...\n' > "$TTY"

if [ ! -e /dev/uio0 ] && command -v modprobe >/dev/null 2>&1; then
	modprobe uio_pdrv_genirq 2>/dev/null || true
fi

i=0
while [ ! -e /dev/uio0 ] && [ "$i" -lt 20 ]; do
	sleep 1
	i=$((i + 1))
done

if [ ! -e /dev/uio0 ]; then
	printf '\nERROR: /dev/uio0 is not available\n' > "$TTY"
	exit 1
fi

ac880_capture_ctl /dev/uio0 clear
# The RTL reset default is the internal incrementing test source. Select the
# external 8-bit ADC before committing and starting continuous acquisition.
ac880_capture_ctl /dev/uio0 adc

if command -v modprobe >/dev/null 2>&1; then
	modprobe ac880_capture_dma 2>/dev/null || true
fi

i=0
while [ ! -e /dev/ac880_capture_dma ] && [ "$i" -lt 20 ]; do
	sleep 1
	i=$((i + 1))
done

if [ ! -e /dev/ac880_capture_dma ]; then
	printf '\nERROR: /dev/ac880_capture_dma is not available\n' > "$TTY"
	ac880_capture_ctl /dev/uio0 stop >/dev/null 2>&1 || true
	exit 1
fi

printf 'DMA ready; displaying sampled waveform ...\n' > "$TTY"
/usr/bin/ac880_capture_lcd /dev/ac880_capture_dma /dev/fb0
rc=$?
ac880_capture_ctl /dev/uio0 stop >/dev/null 2>&1 || true
exit "$rc"
