const args = process.argv.slice(2);
console.log("ubuilder:nodejs:hello");
console.log("argv:" + args.join(","));
let sum = 0;
for (let i = 1; i <= 10; i++) sum += i;
console.log("sum:" + sum);
