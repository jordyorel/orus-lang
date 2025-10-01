#!/usr/bin/env node
const { performance } = require('perf_hooks');

const TRIALS = 5;
const ITER_SIMPLE = 5_000_000;
const NEST_OUTER = 800;
const NEST_INNER = 800;
const ARRAY_LENGTH = 2048;
const ARRAY_REPEATS = 512;

const seedValues = new Int32Array(ARRAY_LENGTH);
for (let i = 0; i < ARRAY_LENGTH; i++) {
  seedValues[i] = i * 3;
}

function elapsedSeconds(start) {
  return Number(performance.now() - start) / 1000;
}

let totalSimple = 0;
let totalNested = 0;
let totalArray = 0;
let checksum = 0;

console.log("=== JavaScript Optimized Loop Benchmark ===");
console.log("trials:", TRIALS);

for (let trial = 0; trial < TRIALS; trial++) {
  const startSimple = performance.now();
  let simpleSum = 0;
  for (let i = 0; i < ITER_SIMPLE; i++) {
    simpleSum += i;
  }
  const elapsedSimple = elapsedSeconds(startSimple);
  totalSimple += elapsedSimple;

  const startNested = performance.now();
  let nestedAcc = 0;
  for (let outer = 0; outer < NEST_OUTER; outer++) {
    let inner = 0;
    while (inner < NEST_INNER) {
      const combined = outer * inner;
      if ((combined & 1) === 0) {
        nestedAcc += combined;
      } else {
        nestedAcc -= inner;
      }
      inner += 1;
    }
  }
  const elapsedNested = elapsedSeconds(startNested);
  totalNested += elapsedNested;

  const startArray = performance.now();
  let arrayTotal = 0;
  let repeat = 0;
  while (repeat < ARRAY_REPEATS) {
    for (let idx = 0; idx < seedValues.length; idx++) {
      arrayTotal += seedValues[idx];
    }
    repeat += 1;
  }
  const elapsedArray = elapsedSeconds(startArray);
  totalArray += elapsedArray;

  checksum += simpleSum + nestedAcc + arrayTotal;

  console.log(
    "trial",
    trial,
    "simple:",
    elapsedSimple,
    "nested:",
    elapsedNested,
    "array:",
    elapsedArray
  );
}

const trialsF = TRIALS;
console.log("average_simple:", totalSimple / trialsF);
console.log("average_nested:", totalNested / trialsF);
console.log("average_array:", totalArray / trialsF);
console.log("checksum:", checksum);
console.log("=== Benchmark complete ===");
