# 🧭 Orus Self-Hosting Roadmap (Full Architecture Edition)

**Goal:**
Transform Orus from a hybrid C + Orus system into a fully self-hosted, cross-platform language runtime.
Once complete, Orus can compile and run itself anywhere, with no external dependencies or C toolchain required.

---

## 1. Architecture Overview

### Core Principle

> “A **tiny C kernel** and a **pure Orus world above it**.”

The Orus runtime is built in **three layers**:

| Layer                       | Language                                            Role                                                  |
| --------------------------- | -------- | ---------------------------------------------------------------------------------------------- |
| **Kernel Primitives**       | **C**    | Minimal system interface — memory, IO, FS, RNG, time, environment, process.                    |
| **Prelude**                 | **Orus** | Safe, typed façade over kernel primitives; provides `print`, `panic`, `alloc`, and formatting. |
| **Standard Library (std/)** | **Orus** | Pure Orus modules (`collections`, `fs`, `json`, `regex`, `time`, `random`, etc.) — no C code.  |

Above this stack live:

* The **Orus compiler (`orusc`)**,
* The **VM / runtime (`orusvm`)**,
* And all developer tools (formatter, REPL, package manager),
  which will themselves eventually be **written in Orus**.

---

## 2. Long-Term Vision

Once complete, the Orus toolchain will be **self-hosting**:

1. The Orus VM and kernel are compiled **once** in C for each platform.
2. The standard library, prelude, and compiler are written **in Orus**.
3. The shipped runtime is a **single native executable** that contains:

   * the VM,
   * the GC,
   * the kernel primitives,
   * the prelude and stdlib (bytecode or embedded cache).

Users run Orus programs **without any C compiler** installed.

---

## 3. Kernel Design (Implemented in C)

### 3.1 Purpose

The Orus kernel is a **portable runtime micro-kernel** providing stable, minimal system primitives.
It never exposes OS-specific details; it acts as an *ABI* for the Orus VM and GC.

### 3.2 Primitive Table (ABI v1)
| Domain      | Primitives                                                                           | Notes           |
| ----------- | ------------------------------------------------------------------------------------ | --------------- |
| Memory      | `mem_alloc`, `mem_realloc`, `mem_free`, `mem_zero`                                   | Core allocator  |
| IO          | `io_write`, `io_read`                                                                | Streams & files |
| FS          | `fs_*`                                                                               | File access     |
| RNG         | `rng_fill`                                                                           | Entropy         |
| Time        | `time_monotonic`, `time_utc`                                                         | Timing          |
| Env         | `env_get`, `env_iter`                                                                | Environment     |
| Process     | `proc_exit`, `proc_spawn`, `proc_wait`                                               | Process control |
| Network     | `net_socket`, `net_bind`, `net_listen`, `net_accept`, `net_connect`, `net_close`     | Networking      |
| System      | `sys_info`                                                                           | Platform info   |
| Telemetry   | `trace_emit`                                                                         | Debug metrics   |

> This ≈31-call ABI will remain frozen for stability. Future additions must use *new opcodes* — never mutate existing ones.

### 3.3 Feature Flags

* **Compile-time**: `ORUS_KERNEL_v1`, `ORUS_PORT_posix`, `ORUS_PORT_win`, `ORUS_PORT_wasm`.
* **Runtime**: `kernel_caps()` → bitset of supported features (`CAP_IO_NONBLOCK`, `CAP_RNG_CSPRNG`, etc.).
* **Fallbacks**: the Prelude provides safe polyfills or clear panics when a capability is missing.

### 3.4 Safety & Reliability

* All functions return `0` or positive on success, `-1` on error, with `kernel_errno()` to fetch codes.
* Thread-safe, reentrant, deterministic; no hidden global state.
* `rng_fill` fails closed if no entropy source.
* `time_monotonic` is guaranteed non-decreasing.

### 3.5 Observability

* Trace counters for allocations, syscalls, and failures.
* Optional debug builds expose `/proc/orus_metrics`.

### 3.6 Implementation Example

```c
void* orus_mem_alloc(size_t size, size_t align) {
    if (size == 0) return NULL;
    void* p = aligned_alloc(align, size);
    if (!p) errno = ENOMEM;
    return p;
}

void* orus_mem_realloc(void* ptr, size_t old, size_t new, size_t align) {
    (void)old; (void)align;
    void* np = realloc(ptr, new);
    if (!np && new) errno = ENOMEM;
    return np;
}

void orus_mem_free(void* ptr, size_t size, size_t align) {
    (void)size; (void)align;
    free(ptr);
}
```

---

## 4. Garbage Collector (in C, inside the VM)

### 4.1 Role

The **GC manages the Orus heap** — all dynamically allocated Orus objects (strings, arrays, maps, closures, etc.) live here.
It uses the kernel’s `mem_*` primitives to request raw pages but owns all higher-level memory lifecycle management.

### 4.2 Responsibilities

* Maintain the Orus heap (object headers, metadata, references).
* Perform root scanning (stack, globals).
* Mark reachable objects.
* Sweep or compact unreachable ones.
* Run finalizers (e.g., freeing kernel buffers from `Seq`).
* Handle GC stats, manual `gc.collect()`, and debug info.

### 4.3 Recommended Algorithm Path

| Stage  | Algorithm                  | Notes                                |
| ------ | -------------------------- | ------------------------------------ |
| MVP    | **Precise Mark & Sweep**   | Simple and stable.                   |
| Later  | **Generational GC**        | Optimize for long-running processes. |
| Future | **Incremental / Parallel** | Optional for low-latency workloads.  |

### 4.4 Heap Ownership Model

| Object Type                                   | Allocated By         | Freed By             |
| --------------------------------------------- | -------------------- | -------------------- |
| Small Orus objects (strings, closures)        | GC                   | GC                   |
| Large raw buffers (`seq`, `bytes`, `builder`) | Kernel (`mem_alloc`) | GC finalizer         |
| VM internals                                  | Kernel (`malloc`)    | Manually freed in VM |

### 4.5 GC Integration Flow

```
Orus code
  ↓
Stdlib alloc() / seq.push()
  ↓
Prelude.alloc() → calls mem_alloc()
  ↓
GC tracks object
  ↓
GC triggers sweep when needed
  ↓
Kernel frees memory pages
```

---

## 5. Prelude (Orus)

### 5.1 Purpose

* Wrap kernel intrinsics into safe, idiomatic Orus functions.
* Provide panic, print, alloc, and formatting utilities.
* Define deterministic RNG for test mode.

### 5.2 Example

```orus
extern fn mem_alloc(usize, usize) -> RawPtr
extern fn mem_realloc(RawPtr, usize, usize, usize) -> RawPtr
extern fn mem_free(RawPtr, usize, usize)

fn alloc(size: usize) -> RawPtr:
    p = mem_alloc(size, alignof(u8))
    if p == null: panic("Out of memory")
    return p
```

---

## 6. Runtime Packaging & Distribution

### 6.1 Build-time

* Requires a C compiler **only to build** the Orus runtime binary (`orusvm`).
* Compiles:

  * Kernel primitives (C)
  * GC (C)
  * VM core (C)
  * Prelude + stdlib (Orus → bytecode)

### 6.2 Runtime

* End-user receives a **prebuilt executable** (e.g., `orus`) + stdlib bytecode.
* No compiler required on their system.
* Runs `.orus` or `.orbc` files directly.

### 6.3 Typical Layout

```
orus/
├── bin/orus          ← native runtime (C + GC + VM)
├── lib/std/          ← compiled Orus stdlib
│   ├── prelude.orbc
│   ├── collections/seq.orbc
│   ├── io.orbc
│   └── ...
└── tools/orusc       ← compiler front-end (Orus)
```

---

## 7. Memory & Allocation Model

| Layer                     | Role                                 | Examples                          |
| ------------------------- | ------------------------------------ | --------------------------------- |
| **Kernel (C)**            | System allocator (`mem_*`)           | `malloc`, `mmap`, `aligned_alloc` |
| **GC (C)**                | Manages Orus heap using kernel pages | heap growth, collection           |
| **Prelude/Stdlib (Orus)** | Safe wrappers (`alloc`, `try_alloc`) | `seq.push`, `json.parse`          |
| **User code (Orus)**      | Uses containers                      | No manual `free()`                |

The memory lifecycle is fully managed by the GC; `Seq` and similar types simply register finalizers for their raw buffers.

---

## 8. Phase Plan (Full Detail)

### Phase –1 — **Kernel Primitive Definition & Feature Flagging**

* Implement all primitives above.
* Generate `kernel.idl` and `std/__kernel.orus`.
* Build `std/prelude.orus` implementing `print`, formatting, panic hooks, and RNG seeding in pure Orus.
* Add compile-time + runtime feature flags.
* Add conformance, fault-injection, and fuzz tests.

**Acceptance:**
Green CI on x86-64 + ARM64; ABI documented and stable.

---

### Phase 0 — **Prelude Bootstrap**

* Author `std/prelude.orus` and generated kernel façade.
* Implement panic hook, formatter, and test RNG.
* Integrate with VM boot sequence.

**Acceptance:**
VM loads prelude before user code; printing and panic work.

---

### Phase 1 — **Core Services (IO, Time, Random)**

* Implement `std/io`, `std/time`, and `std/random` atop the new primitives.
* Provide deterministic RNG (e.g., xoroshiro128+) in Orus with `rng_fill` reserved for entropy refreshes.
* Backfill unit and property tests for IO buffering, monotonic time bounds, and RNG distribution.

---

### Phase 2 — **Data & Memory Utilities**

* Implement `std/collections/seq`, `slice`, `bytes/buffer`, and `string.builder`.
* Move array algorithms (`push`, `pop`, `map`, `filter`, `reduce`, `reserve`) into `std/collections` written in Orus.
* Retire all VM-level array helpers. 
* Port UTF-8 encode/decode and substring manipulation into Orus, replacing the VM helpers once parity is achieved.
* All data structures rely on GC-tracked metadata + kernel buffers.

---

### Phase 3 — **Math Rewrite**

* Pure Orus implementations of `sqrt`, `pow`, `trig`, `sin`, `cos` etc...
* Build accuracy tiers (`fast`, `default`, `strict`).

---

### Phase 4 — **Filesystem & Environment**

* `std/fs`, `std/env`; buffered file access; dir iteration via caps.

---

### Phase 4.1 — **Networking Layer**

* Add kernel primitives:
  `net_socket`, `net_bind`, `net_listen`, `net_accept`, `net_connect`, `net_close`.
* Expose safe wrappers in Prelude (`socket_tcp`, `socket_udp`, etc.).
* Implement `std/net/` modules entirely in Orus:
  std/net/
  ├── tcp.orus          → stream sockets
  ├── udp.orus          → datagram sockets
  ├── addr.orus         → IP & port parsing
  ├── dns.orus          → DNS resolver (pure Orus)
  ├── tls.orus          → optional TLS wrapper
  └── http.orus         → HTTP client/server built on tcp
* All networking uses GC-managed handles with automatic `net_close` finalizers.
* Add capability flags (`CAP_NET_TCP`, `CAP_NET_UDP`, `CAP_NET_UNIX`, `CAP_NET_TLS`) for cross-platform support.
* Provide graceful fallbacks on restricted platforms (e.g., WASM).
* No TLS in kernel — to be implemented later in `std/net/tls` purely in Orus.

**Acceptance:**
TCP echo server and HTTP GET client written in pure Orus both run successfully across macOS, Linux, and Windows.

---

### Phase 5 — **Extended Utilities**

* `map`, `set`, `deque`, `regex`, `json`, `smallseq`, `stableseq`.
* Property & fuzz tests; deterministic perf harness.

---

### Phase 6 — **Finalize Kernel Interface**

* Remove transitional hooks.
* Freeze ABI v1.
* Enforce rule: *no `std/` module may import kernel directly*.

---

## 9. Long-Term Self-Hosting Milestones

| Milestone    | Description                                                                         |
| ------------ | ----------------------------------------------------------------------------------- |
| **Phase 7**  | Rewrite compiler front-end (lexer, parser, AST, type checker) in Orus using stdlib. |
| **Phase 8**  | Implement Orus backend (bytecode/codegen) in Orus using `io` + `fs`.                |
| **Phase 9**  | Bootstrap: compile the Orus compiler with itself.                                   |
| **Phase 10** | Remove all C except kernel + GC. Tag **Orus 1.0 (fully self-hosted)**.              |

---

## 10. Testing & Quality Gates

* Conformance tests for every kernel primitive.
* Fault-injection for all error paths (`ENOMEM`, `EINTR`, `EAGAIN`).
* Property tests for `seq`, `map`, `regex`, `json`.
* GC stress + leak tests (fail-after-N allocator).
* Performance gates per module.
* CI matrix: x86-64, ARM64, macOS, Linux, Windows.

---

## 11. Risk Management & Future-Proofing

* ABI freeze after Phase 6.
* Backward compatibility via new opcode IDs only.
* Optional GC versions (generational, concurrent) under feature flags.
* Portable kernel C code — no platform-specific hacks.
* Fallback paths for missing caps (e.g., RNG entropy).
* Full docs: `docs/kernel_abi.md`, `docs/runtime_arch.md`.

---

## 12. Summary — The Orus Runtime Contract

| Component          | Language | Role                                   |
| ------------------ | -------- | -------------------------------------- |
| **Kernel**         | C        | Raw system interface (≈ 25 calls).     |
| **GC**             | C        | Manages Orus heap; uses kernel memory. |
| **Prelude**        | Orus     | Safe façade, panic, formatting.        |
| **Stdlib**         | Orus     | Pure Orus, built on prelude.           |
| **Compiler/Tools** | Orus     | Built on stdlib; self-hosted.          |

Once these layers are complete:

✅ Orus can **run and compile itself anywhere**,
✅ with **no C compiler** on user machines,
✅ and a **frozen, minimal kernel ABI** that will last for decades.

