# PhoneFilter MVP ERC Checklist

Run this after wiring and before PCB routing.

## Must-pass items

- All power pins connected (`+3V3`, `GND`).
- No floating optocoupler transistor collectors.
- `INCOMING_RING_IN` has pull-up (`R2`).
- `OUTGOING_PULSE_IN` has pull-up (`R3`).
- `GPIO14/12` routed through `R4/R5` before external stage.
- `GPIO26` routed to relay module `IN`.
- Every connector pin has an intentional net (no accidental NC).

## Typical ERC warnings to resolve

- Power input pin not driven -> add proper power symbol / PWR_FLAG if needed.
- Unconnected input pin on module symbols -> either wire or mark as no-connect explicitly.
- Net label typo mismatch (`OUTGOING_PULSE_IN` vs `OUTGOING_PULSEIN`) -> normalize names.

## Visual sanity checks

- Line-side symbols (`J1`, `BR1`, LED sides of optos) grouped left.
- Logic-side (`U1`, pull-ups, PWM, relay control) grouped right.
- Clear isolation boundary between line-side and logic-side.
