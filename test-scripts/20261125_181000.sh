# 1. what interfaces does it expose, and did anything bind?
for d in /sys/bus/usb/devices/8-2:1.*; do
  echo "$d class=$(cat $d/bInterfaceClass) sub=$(cat $d/bInterfaceSubClass) driver=$(basename $(readlink -f $d/driver) 2>/dev/null)"
done

# 2. did ALSA pick it up?
cat /proc/asound/cards
ls /dev/snd/

# 3. what did the kernel say when it appeared?
dmesg | tail -30
