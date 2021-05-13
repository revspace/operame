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
3. Ga naar de map van deze repository en voer `pio run` uit.
