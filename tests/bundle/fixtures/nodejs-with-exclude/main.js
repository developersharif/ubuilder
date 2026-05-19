const pico = require("picocolors");
// `is-number` is in package.json but --exclude=is-number drops it before
// npm install runs; require() must throw MODULE_NOT_FOUND. Catch and
// print a deterministic absence marker.
let isNumberAbsent = false;
try {
  require("is-number");
} catch (e) {
  if (e && e.code === "MODULE_NOT_FOUND") isNumberAbsent = true;
}

const args = process.argv.slice(2);
console.log("ubuilder:node-exclude:hello");
console.log("argv:" + args.join(","));
console.log("picocolors-isColorSupported:" + (typeof pico.isColorSupported === "boolean"));
console.log("is-number-absent:" + isNumberAbsent);
