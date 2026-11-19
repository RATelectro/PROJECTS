# Td5Gauge_SMD_ESP32 — Altium board reconstructed from Gerber files

`Td5Gauge_SMD_ESP32.PcbDoc` is an Altium Designer PCB document rebuilt from the
KiCad-generated Gerber/Excellon manufacturing data in
`BinOwl_Td5Gauge_PCB-master.zip` (2-layer board, ESP32 TD5 gauge, from
https://github.com/BinOwl/Td5Gauge_PCB).

## Files

| File | Description |
|---|---|
| `Td5Gauge_SMD_ESP32.PcbDoc` | Binary Altium PCB document (like `SOM_Codec_WM8731.PcbDoc`). Open with *File → Open* in Altium Designer. |
| `Td5Gauge_SMD_ESP32_ASCII.PcbDoc` | Same board in Altium's ASCII record format. Altium opens this as well; handy for diffing/version control. |
| `Td5Gauge_SMD_ESP32_preview.png` | Render of the binary document for a quick look without Altium. |

## What the reconstruction contains

- Board shape from the Edge_Cuts profile (64.0 × 49.8 mm rectangle).
- Top/bottom copper: 7,021 tracks, 25 polygon regions (incl. the two big
  ground pours with their clearance holes), 184 pads, 58 vias.
- Through-hole pads matched from the PTH drill file (plated), the two 3.2 mm
  mounting holes from the NPTH file (non-plated), SMD pads from flashes
  (rounded-rectangle/obround pads preserved via corner-radius attributes).
- 66 electrical nets, recovered by geometric connectivity analysis and named
  `Net1…Net66` (largest = the ground pour). Real net names are not present in
  Gerber data; rename as needed (e.g. from the schematic).
- Top/bottom silkscreen and the fabrication drawing (F_Fab) as vector
  primitives. Silkscreen text arrives as stroke geometry (like any
  Gerber → PCB reverse flow), so it is not editable as text.

## Deliberate omissions

- **Solder mask / paste layers** are not imported — Altium regenerates them
  from pads/vias using its design rules.
- **Components**: footprints/components cannot be recovered from Gerber; all
  pads are free primitives. Use the schematic to re-link parts if needed.
- Design rules, layer stackup details (impedance, exact dielectrics) are
  Altium defaults for a 2-layer board; via hole sizes for drills not in the
  drill file are heuristics.

## How it was made (reproducible)

The pipeline lives in `../work/`:

1. `work/convert.py` — parses the Gerber/Excellon files (gerbonara +
   shapely), reconstructs primitives, connectivity and board outline, and
   emits an ASCII `.PcbDoc` (Altium ASCII record grammar).
2. `work/conv/run.mjs` — validates the ASCII (0 errors/warnings, strict
   profile), renders SVG, and serializes to the native binary OLE/CFB
   `.PcbDoc` via the open-source `altiumts` library.

Verified by: altiumts strict validation, binary round-trip parse
(184 pads / 58 vias / 7,021 tracks / 25 regions / 66 nets), pixel-level
comparison against the source Gerbers, and the independent hosted Altium
viewer (altiumviewer.tscircuit.com), which reads the binary natively.

## Alternative: inside Altium Designer

If you have Altium available, the official reverse-engineering workflow is
CAMtastic: *File → New → CAM Document*, *File → Import → Quick Load* (pick the
folder with the Gerbers + drills), fix layer types in *Tables → Layers*,
extract netlist (*Tools → Netlist → Extract*), then *File → Export → Export to
PCB*. See the Altium KB article "Reverse Engineer a PCB Document from Gerber
or ODB++". The file provided here skips those manual steps.
