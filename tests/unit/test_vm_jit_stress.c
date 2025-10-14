#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/jit_benchmark.h"

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define TEST_CASE(name) static bool name(void)

static bool
run_jit_program(const char* program, const char* tag, OrusJitRunStats* stats) {
    if (!program) {
        return false;
    }
    size_t length = strlen(program);
    return vm_jit_run_source_benchmark(program, length, tag, true, stats);
}

static const char* kLongRunningProgram =
    "M: i32 = 200000\n"
    "mut i: i32 = 0\n"
    "mut acc: i64 = 0\n"
    "while i < M:\n"
    "    acc = acc + (i as i64)\n"
    "    if (i % 97) == 0:\n"
    "        acc = acc - 1\n"
    "    i = i + 1\n"
    "print(\"acc\", acc)\n";

static const char* kGcHeavyProgram =
    "ITERATIONS: i32 = 6000\n"
    "mut pieces: [string] = []\n"
    "mut i: i32 = 0\n"
    "mut consumed: i32 = 0\n"
    "while i < ITERATIONS:\n"
    "    push(pieces, \"payload\")\n"
    "    if len(pieces) > 256:\n"
    "        consumed = consumed + len(pieces)\n"
    "    i = i + 1\n"
    "print(\"pieces\", len(pieces), \"consumed\", consumed)\n";

static const char* kConcurrencyProgram =
    "TRIALS: i32 = 4\n"
    "mut trial: i32 = 0\n"
    "mut total: i64 = 0\n"
    "while trial < TRIALS:\n"
    "    mut j: i32 = 0\n"
    "    mut sum: i64 = 0\n"
    "    while j < 120000:\n"
    "        sum = sum + (j as i64)\n"
    "        j = j + 1\n"
    "    total = total + sum\n"
    "    trial = trial + 1\n"
    "print(\"total\", total)\n";

TEST_CASE(test_jit_stress_long_running) {
    OrusJitRunStats stats = {0};
    if (!run_jit_program(kLongRunningProgram, "jit_stress_long", &stats)) {
        fprintf(stderr, "Failed to run long-running JIT stress program\n");
        return false;
    }
    if (stats.native_dispatches == 0) {
        fprintf(stderr, "Expected native dispatches during long-running stress (dispatches=%llu)\n",
                (unsigned long long)stats.native_dispatches);
        return false;
    }
    if (stats.duration_ns <= 0.0) {
        fprintf(stderr, "Duration should be positive for long-running stress (%.2f)\n",
                stats.duration_ns);
        return false;
    }
    return true;
}

TEST_CASE(test_jit_stress_gc_heavy) {
    OrusJitRunStats stats = {0};
    if (!run_jit_program(kGcHeavyProgram, "jit_stress_gc", &stats)) {
        fprintf(stderr, "Failed to run GC-heavy JIT stress program\n");
        return false;
    }
    if (stats.translation_failure != 0) {
        fprintf(stderr, "GC-heavy stress encountered translation failures (%llu)\n",
                (unsigned long long)stats.translation_failure);
        return false;
    }
    if (stats.duration_ns <= 0.0) {
        fprintf(stderr, "GC-heavy stress duration should be positive\n");
        return false;
    }
    return true;
}

TEST_CASE(test_jit_stress_concurrency) {
#if defined(_WIN32)
    printf("[SKIP] concurrency stress requires fork(); skipping on Windows.\n");
    return true;
#else
    const int workers = 4;
    pid_t pids[workers];
    memset(pids, 0, sizeof(pids));

    for (int i = 0; i < workers; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return false;
        }
        if (pid == 0) {
            OrusJitRunStats stats = {0};
            bool ok = run_jit_program(kConcurrencyProgram, "jit_stress_concurrency", &stats);
            if (!ok || stats.native_dispatches == 0) {
                _exit(1);
            }
            _exit(0);
        }
        pids[i] = pid;
    }

    bool success = true;
    for (int i = 0; i < workers; ++i) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid");
            success = false;
            continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Worker %d exited with status %d\n", i, status);
            success = false;
        }
    }
    return success;
#endif
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"test_jit_stress_long_running", test_jit_stress_long_running},
        {"test_jit_stress_gc_heavy", test_jit_stress_gc_heavy},
        {"test_jit_stress_concurrency", test_jit_stress_concurrency},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i) {
        bool ok = tests[i].fn();
        if (ok) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    printf("%d/%d JIT stress tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
