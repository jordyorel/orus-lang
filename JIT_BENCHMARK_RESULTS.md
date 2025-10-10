# Orus JIT Benchmark Results

The following measurements were collected by running `make test` on the release build profile inside the Orus repository.

- **Average tier-up latency:** 44,097 ns over 5 runs
- **Native entry latency:** 3.10 ns per call (322.67 million calls/sec)
- **Native compilations recorded:** 5
- **Native invocations recorded:** 1,000,000

These numbers come from the `tests/unit/test_vm_jit_benchmark.c` harness, which emits DynASM-compiled stubs and executes them via the Orus JIT backend. The benchmark confirms that the native tier is entered for hot loops and reports both the compilation and invocation counters maintained by the VM.
