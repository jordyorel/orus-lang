// JavaScript Control Flow (For Loop) Benchmark
// Run with: node control_flow_benchmark.js

function nowMs() {
  return Number(process.hrtime.bigint()) / 1e6;
}

console.log("=== JavaScript Control Flow (For Loop) Benchmark ===");
const start = nowMs();

const N1 = 2_000_000; // simple loop iterations
const O2 = 1000;      // nested outer
const I2 = 1000;      // nested inner
const N3 = 1_000_000; // while-like iterations

// Phase 1: simple for-loop sum
console.log("Phase 1: simple sum loop");
let sum1 = 0;
for (let i = 1; i <= N1; i++) {
  sum1 += i;
}

// Phase 2: nested loops with branch
console.log("Phase 2: nested loops with branch");
let acc2 = 0;
for (let i = 0; i <= O2; i++) {
  const base = i;
  for (let j = 0; j <= I2; j++) {
    const t = base + j;
    if ((t & 1) === 0) {
      acc2 += t;
    } else {
      acc2 -= 1;
    }
  }
}

// Phase 3: even sum with stepping loop
console.log("Phase 3: even sum with stepping loop");
let sum3 = 0;
for (let k = 0; k <= N3 * 2; k += 2) {
  sum3 += k;
}

const checksum = sum1 + acc2 + sum3;
const elapsed = (nowMs() - start) / 1000.0;

console.log("Checksum:", checksum);
console.log("Total execution time:", elapsed);
console.log("=== JavaScript Control Flow Benchmark Complete ===");

