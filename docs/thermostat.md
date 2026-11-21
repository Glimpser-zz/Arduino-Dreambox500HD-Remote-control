# Thermostat (version 3.0)

The Arduino also works as a heating and cooling thermostat. The AM2302 sensor measures temperature and humidity, the LCD shows them with the 24 hour minimum and maximum,
the buttons of the keypad shield change the settings, and the relay module switches the heating and the cooling. Wiring is in [hardware.md](hardware.md).
The IR remotes keep priority: the sensor is read and the display is written only while no remote key is being handled.

## Modes

| Mode | Heating relay (IN1) | Cooling relay (IN2) |
|---|---|---|
| **Off** | off | off |
| **Winter** | on when the temperature is at or below `set - hysteresis`, off at or above `set` | off |
| **Summer** | off | on when the temperature is at or above `set + hysteresis`, off at or below `set` |

Example: set 22.0 C, hysteresis 0.5 C. In winter mode the heating switches on at 21.5 C and off at 22.0 C. In summer mode the cooling switches on at 22.5 C and off at 22.0 C.
The heating and the cooling are never on at the same time. After a first start or after a settings reset the mode is **Off**, so nothing is switched until you choose a mode.

## Display and buttons

LEFT and RIGHT change the page. After 30 seconds without a button the display returns to the main page.

| Page | Shows | Buttons |
|---|---|---|
| 0 Main | line 1: temperature and humidity. Line 2: mode, set temperature and the relay (`HEAT:ON`, `COOL:--`) | UP / DOWN: set temperature in steps of 0.5 C (they repeat when held). SELECT: mode Off, Winter, Summer |
| 1 Temperature 24 h | minimum / maximum of the last 24 hours and how many hours the history covers | |
| 2 Humidity 24 h | the same for the humidity | |
| 3 Settings | one field at a time | SELECT: next field. UP / DOWN: change it |

Settings fields (page 3):

| Field | Range | Step | Default |
|---|---|---|---|
| Mode | Off, Winter, Summer | | Off |
| Setpoint | 5.0 to 35.0 C | 0.5 | 22.0 |
| Hysteresis | 0.1 to 10.0 C | 0.1 | 0.5 |
| Offset (sensor correction) | -10.0 to +10.0 C | 0.1 | 0.0 |
| Min ON (shortest time a relay stays on) | 0 to 1800 s | 10 | 30 |
| Min OFF (shortest time a relay stays off) | 0 to 3600 s | 30 | 120 |

The settings are saved to the EEPROM 3 seconds after the last change and are loaded at every start. A stored copy that fails its checksum is replaced by the defaults.

## Protections

* **Minimum ON and OFF time** protect the relays and the equipment from fast switching. The minimum OFF time also counts from the moment the Arduino starts, so a cooler does not start at once after a power cut.
* **Sensor fail-safe:** if there is no good reading for 2 minutes (cable fault, sensor fault), both relays are switched off and the display shows `SENSOR ERROR`.
* A mode change always switches both relays off first.
* At start both relay pins are set to "off" before they become outputs, so the relays do not click at power-up.

## 24 hour history

The sketch keeps one minimum and one maximum per hour for 24 hours and shows the minimum and maximum over all of them. There is no real-time clock, so the window is the time since the Arduino started,
at most 24 hours; the display shows how much time it covers. The history is kept in RAM, so it starts again after a power cut. A value falls out of the window about 24 hours after it was measured.

## What is tested

The logic (modes, hysteresis, minimum times, fail-safe, buttons, EEPROM, history, priority of the IR part) was tested in a simulator, including a 30 hour run.
The AM2302 reading, the display, the buttons and the relays have **not** been tested on the real hardware yet.
