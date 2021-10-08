# Operame

Dit is de broncode van de firmware voor de [ControlCO2-meter](https://controlco2.space), operame project origineel gestart door RevSpace.

## Language

The default language is Dutch; users can pick a different language using the
WiFi configuration portal. To change the default setting to English, change
`#define LANGUAGE "nl"` to `#define LANGUAGE "en"`.

## Gebruik

### Installatie

Deze repository gebruikt [PlatformIO](https://platformio.org/) voor installatie.

1. Installeer PlatformIO.
2. Kloon deze repository lokaal.
3. Ga in de commandline of terminal naar de map van deze repository en voer `pio run` uit.

## Tips

Indien het bord niet gevonden wordt, controleer of je de cp210x USB TO UART drivers van silicon labs hebt geinstalleerd.
https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
  
## Ervaring van een windows 10 gebruiker  
-windows 10 systeem

-visual studio code installeren

-platformio 2.3.2 extensie geinstalleerd (issue met 2.3.3) in VS code.

-python 3.9 geinstalleerd, zorg dat add to path aangevinkt is

-git installeren om repository te clonen

-bordje connecteren en in devices checken of driver er is anders cp210x driver van silicon lans installeren.

-dan in platformio.ini upload_port en flags commenten. zoek port dan automatisch
