#!/usr/bin/env node

const start = process.hrtime.bigint();

let x = 0n;  // Use BigInt for large number precision
for (let i = 0n; i < 1000000000n; i++) {
    x += i;
}

const end = process.hrtime.bigint();
console.log(x.toString());

// Print timing to stderr
const duration = Number(end - start) / 1000000000;  // Convert to seconds
console.error(`Node.js execution time: ${duration.toFixed(6)} seconds`);