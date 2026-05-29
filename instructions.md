0. turn off firewall and vpn!

1. find the address mqtt needs to talk to.
macos: ipconfig getifaddr en0
windows: ipconfig
linux: hostname -I | awk '{print $1}'

2. once you have the ip address, make sure to change this in the main.c file and the index.html file.

3. build, flash and monitor the esp with the changes. on startup the esp will post it's own ip. change this in the index.html and server.py files as well.

4. run the docker containers 'docker compose up --build'

5. open index.html

6. login with username=admin, password=esp32_secret