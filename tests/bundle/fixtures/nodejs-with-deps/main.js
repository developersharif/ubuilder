const pico = require("picocolors");
const args = process.argv.slice(2);
console.log("ubuilder:m8b:hello");
console.log("argv:" + args.join(","));
console.log("picocolors-isColorSupported:" + (typeof pico.isColorSupported === "boolean"));
