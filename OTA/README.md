# Uitleg hoe je via de commandline de firmware kunt uploaden via OTA

OTA = Over The Air - ofwel: via WiFi de firmware updaten

De onderstaande procedure werkt voor MacOS. Voor Windows zou het ook kunnen werken met kleine aanpassingen.


Stappen:
1. Stel via de web interface van operame OTA in (checkbox "Draadloos herprogrammeren inschakelen.").
2. Kies een portaal wachtwoord (xxxxxx in het voorbeeld hieronder).
3. Na een reboot zal operame een IP adres toegewezen kregen en zichzelf bekend maken op het lokale netwerk via mDNS. Op de Mac kan je met de Discovery tool (te downloaden van de App store via https://apps.apple.com/us/app/discovery-dns-sd-browser/id1381004916) het IP adres en bijbehorende port nummer vinden. Zie hieronder voor een screen shot (onder de tag ```_arduino._tcp```). Noteer het IP adres en port nummer (in dit geval ```192.168.22.228``` en ```3232```).

![Discovery Tool](Discovery_tool.png?raw=true "Discovery tool output")

4. Download de bestanden ```espota.py``` en ```firmware_mqtt.bin``` naar een folder.
5. Start terminal (command line tool) en navigeer naar de bovenstaande folder.
6. Start upload met het commando

````
python espota.py -i 192.168.22.228 -p 3232 --auth=xxxxxx -f ./firmware_mqtt.bin 
````

Waarbij 192.168.22.228 vervangen wordt door het IP adres van jouw operame device en 3232 het port nummer (3232 is de default en is waarschijnlijk altijd hetzelfde).

````
Sending invitation to 192.168.22.228 
Authenticating...OK
Uploading.........................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
````


