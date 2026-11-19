// Validate the generated ASCII PcbDoc, render SVGs, and emit a binary PcbDoc.
import { readFile, writeFile } from "node:fs/promises"
import {
  parseAltiumPcbDoc,
  parseAltiumBinaryPcbDoc,
  serializeAltiumPcbDocToBinary,
  serializeAltiumPcbToSvg,
  validateAltiumDocument,
} from "altiumts"

const asciiPath = process.argv[2]
const binPath = process.argv[3]

// 1. structural validation via CLI-style entry: parse below validates too
const src = await readFile(asciiPath, "utf8")

// 2. parse ASCII
const board = parseAltiumPcbDoc(src)
console.log(`parsed ASCII board: ${board.records.length} records`)

// 3. render SVG from ASCII model
await writeFile(asciiPath + ".svg", serializeAltiumPcbToSvg(board))
console.log("wrote ASCII svg")

// 4. to binary
const bytes = serializeAltiumPcbDocToBinary(board)
await writeFile(binPath, bytes)
console.log(`wrote binary ${binPath} (${bytes.length} bytes)`)

// 5. parse binary back + render
const bin = parseAltiumBinaryPcbDoc(new Uint8Array(await readFile(binPath)))
console.log(`parsed binary: pads=${bin.pads.length} vias=${bin.vias.length} tracks=${bin.tracks.length} arcs=${bin.arcs.length} regions=${bin.regions.length} texts=${bin.texts.length} comps=${bin.components.length} nets=${bin.nets.length}`)
await writeFile(binPath + ".svg", serializeAltiumPcbToSvg(bin))
console.log("wrote binary svg")
