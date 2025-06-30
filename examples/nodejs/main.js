#!/usr/bin/env node

function main() {
    console.log("Hello from UBuilder Node.js Application!");
    console.log("This application was packaged as a single executable.");
    
    console.log(`Node.js version: ${process.version}`);
    console.log(`Arguments: ${process.argv.join(" ")}`);
    console.log(`Current working directory: ${process.cwd()}`);
    console.log(`Executable path: ${process.execPath}`);
}

if (require.main === module) {
    main();
}
