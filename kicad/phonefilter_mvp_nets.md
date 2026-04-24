# PhoneFilter MVP Nets

Use these exact net names in KiCad labels.

## Power

- `+3V3`
- `GND`

## Line / ring side

- `LINE_A`
- `LINE_B`
- `RING_RECT_P`
- `RING_RECT_N`

## Pulse side

- `PULSE_A`
- `PULSE_B`

## ESP32 signal nets

- `INCOMING_RING_IN` -> `GPIO4`
- `OUTGOING_PULSE_IN` -> `GPIO27`
- `DTMF_PWM_LOW` -> `GPIO14`
- `DTMF_PWM_HIGH` -> `GPIO12`
- `RELAY_CTRL` -> `GPIO26`

## External stage nets

- `DTMF_LOW_IN`
- `DTMF_HIGH_IN`
- `RELAY_IN`
- `RELAY_VCC`
- `RELAY_GND`
- `RELAY_COM`
- `RELAY_NO`
- `RELAY_NC`

## Connection map

- `LINE_A/B` -> `BR1(~,~)`
- `BR1(+)` -> `R1` -> `U2 LED A`
- `BR1(-)` -> `U2 LED K`
- `U2 transistor C` -> `INCOMING_RING_IN`
- `U2 transistor E` -> `GND`
- `R2` from `INCOMING_RING_IN` to `+3V3`
- `C1` from `INCOMING_RING_IN` to `GND` (optional)

- `PULSE_A` -> `R6` -> `U3 LED A`
- `PULSE_B` -> `U3 LED K` (or return side through interface)
- `U3 transistor C` -> `OUTGOING_PULSE_IN`
- `U3 transistor E` -> `GND`
- `R3` from `OUTGOING_PULSE_IN` to `+3V3`
- `C2` from `OUTGOING_PULSE_IN` to `GND` (optional)

- `DTMF_PWM_LOW` -> `R4` -> `DTMF_LOW_IN`
- `DTMF_PWM_HIGH` -> `R5` -> `DTMF_HIGH_IN`

- `RELAY_CTRL` -> `RELAY_IN`
- `RELAY_VCC/GND` -> module supply
- contacts: `RELAY_COM/NO/NC` to telephony path
