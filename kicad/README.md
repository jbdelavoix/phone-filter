# PhoneFilter KiCad Starter

This folder is a practical starter to build the real schematic in KiCad.

## Files

- `phonefilter_mvp_bom.csv`: components and default values.
- `phonefilter_mvp_nets.md`: named nets and expected links.
- `phonefilter_mvp_erc_checklist.md`: quick ERC sanity checklist.

## 15-minute build flow

1. In KiCad, create a new project in this folder (example: `phonefilter_mvp.kicad_pro`).
2. Open schematic editor and place symbols from `phonefilter_mvp_bom.csv`.
3. Add global/local net labels exactly as listed in `phonefilter_mvp_nets.md`.
4. Wire blocks in this order:
   - ring detect: `LINE_A/B -> BR1 -> R1 -> U2(PC817) -> INCOMING_RING_IN(GPIO4)`
   - pulse detect: `PULSE_A/B -> R6 -> U3(PC817) -> OUTGOING_PULSE_IN(GPIO27)`
   - outgoing DTMF: `GPIO14/12 -> R4/R5 -> DTMF_LOW_IN/DTMF_HIGH_IN`
   - relay control: `GPIO26 -> K1.IN` (+ module `VCC/GND`)
5. Add power flags and run ERC.
6. Resolve all errors, then annotate and save.

## Notes

- Use your relay module as-is (no discrete relay driver needed).
- Keep line-side and logic-side separated on schematic and layout.
- `PC817 + MB10M + series resistor` is the default ring front-end.
