# Aanpassing firmware voor mqtt server met authenticatie

## Uitleg hoe je via de commandline de firmware kunt uploaden via OTA

OTA = Over The Air - ofwel: via WiFi de firmware updaten

De onderstaande procedure werkt voor MacOS. Voor Windows zou het ook kunnen werken met kleine aanpassingen.


Stappen:
1. Stel via de web interface van ```operame``` OTA in (checkbox "Draadloos herprogrammeren inschakelen.").
2. Kies een portaal wachtwoord (xxxxxx in het voorbeeld hieronder).
3. Na een reboot zal ```operame``` een IP adres toegewezen krijgen en zichzelf bekend maken op het lokale netwerk via mDNS. Op de Mac kan je met de Discovery tool (te downloaden vanuit de App store via https://apps.apple.com/us/app/discovery-dns-sd-browser/id1381004916) het IP adres en bijbehorende port nummer vinden. Zie hieronder voor een screen shot (onder de tag ```_arduino._tcp```). Noteer het IP adres en port nummer (in dit geval ```192.168.22.228``` en ```3232```).

![Discovery Tool](Discovery_tool.png?raw=true "Discovery tool output")

4. Download de bestanden ```espota.py``` en ```firmware_mqtt.bin``` naar een folder.
5. Start terminal (command line tool) en navigeer naar de bovenstaande folder.
6. Start upload met het commando

````
python espota.py -i 192.168.22.228 -p 3232 --auth=xxxxxx -f ./firmware_mqtt.bin 
````

Waarbij ```192.168.22.228``` vervangen wordt door het IP adres van jouw operame device en ```3232``` het port nummer (3232 is de default en is waarschijnlijk altijd hetzelfde). xxxxxx moet je vervangen door het in stap 2 ingestelde wachtwoord.

Je zult op de ```operame``` het uploaden zien (met percentage oplopend van 0..100%). Output op de terminal:
````
$ python espota.py -i 192.168.22.228 -p 3232 --auth=xxxxxx -f ./firmware_mqtt.bin 
Sending invitation to 192.168.22.228 
Authenticating...OK
Uploading.........................................................................................................
..................................................................................................................
..................................................................................................................
..................................................................................................................
.....................................................................................................
$ 
````

## Gebruik maken van Adafruit IoT server voor het weergeven van CO2 waardes

Via https://accounts.adafruit.com/ kun je een gratis account aanmaken om vervolgens via https://io.adafruit.com een dashboard van maximaal 5 operame sensors te tonen (als je er meer hebt heb je een betaald account nodig).

Voorbeeld van mijn data: https://io.adafruit.com/bart59/dashboards/co2
Binnen adafruit definieer je een dashboard en een feed voor elke operame sensor.

Instellingen die je moet doen via de ```operame``` web interface:

````
Enable Mqtt: 	check checkbox
Mqtt Host:	io.adafruit.com
Mqtt Port:	1883
Mqtt Username: 	Your Adafruit IO Username
Mqtt Password:	Your Adafruit IO Key
Mqtt topic: 	<adafruit username>/feeds/<feed name> (in mijn geval: bart59/feeds/CO2_home)
Mqtt template:	{}
````
