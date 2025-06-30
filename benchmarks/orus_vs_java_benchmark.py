#!/usr/bin/env python3
"""
Orus vs Java Performance Benchmark
Compares Orus VM performance against Java 21.
"""
import subprocess
import time
import statistics
import json
import os
import tempfile
import argparse

class OrusJavaBenchmark:
    def __init__(self, orus_binary="../orus", iterations=50, timeout=30):
        self.orus_binary = orus_binary
        self.iterations = iterations
        self.timeout = timeout
        self.results = []

    def check_setup(self):
        if not os.path.exists(self.orus_binary):
            print(f"❌ Orus binary not found at {self.orus_binary}")
            return False
        try:
            subprocess.run([self.orus_binary, "-h"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            print("✅ Orus binary found")
        except Exception as e:
            print(f"❌ Orus check failed: {e}")
            return False
        try:
            result = subprocess.run(["java", "-version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            version_line = result.stderr.decode().splitlines()[0]
            print(f"✅ Java found: {version_line}")
        except Exception as e:
            print(f"❌ Java check failed: {e}")
            return False
        return True

    def run_orus(self, code):
        times = []
        with tempfile.NamedTemporaryFile(mode="w", suffix=".orus", delete=False) as f:
            f.write(code)
            fname = f.name
        for _ in range(self.iterations):
            start = time.perf_counter()
            subprocess.run([self.orus_binary, fname], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=self.timeout, check=True)
            end = time.perf_counter()
            times.append(end - start)
        os.unlink(fname)
        return {
            "avg": statistics.mean(times),
            "min": min(times),
            "max": max(times)
        }

    def run_java(self, code):
        times = []
        with tempfile.TemporaryDirectory() as tmpdir:
            java_file = os.path.join(tmpdir, "Bench.java")
            class_name = "Bench"
            with open(java_file, "w") as f:
                f.write("public class Bench { public static void main(String[] args) { System.out.println(" + code + "); } }")
            compile_res = subprocess.run(["javac", java_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=self.timeout)
            if compile_res.returncode != 0:
                raise RuntimeError(f"Java compilation failed: {compile_res.stderr.decode()}")
            for _ in range(self.iterations):
                start = time.perf_counter()
                subprocess.run(["java", "-cp", tmpdir, class_name], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=self.timeout, check=True)
                end = time.perf_counter()
                times.append(end - start)
        return {
            "avg": statistics.mean(times),
            "min": min(times),
            "max": max(times)
        }

    def run_test(self, name, expr):
        print(f"\n🔄 Running test: {name}")
        print(f"   Iterations: {self.iterations}")
        orus_stats = self.run_orus(expr)
        java_stats = self.run_java(expr)
        speedup = java_stats["avg"] / orus_stats["avg"]
        print(f"   Orus: {orus_stats['avg']:.6f}s avg")
        print(f"   Java: {java_stats['avg']:.6f}s avg")
        if speedup > 1:
            print(f"   🏆 Orus is {speedup:.2f}x faster")
        else:
            print(f"   🏆 Java is {1/speedup:.2f}x faster")
        self.results.append({
            "name": name,
            "speedup": speedup,
            "orus": orus_stats,
            "java": java_stats
        })

    def summary(self, output_file="results.json"):
        if not self.results:
            return
        overall = statistics.mean(r["speedup"] for r in self.results)
        print("\n📊 SUMMARY")
        for r in self.results:
            print(f"   {r['name']}: {r['speedup']:.2f}x")
        print(f"Overall speedup: {overall:.2f}x")

        data = {
            "results": self.results,
            "overall_speedup": overall,
            "iterations": self.iterations,
            "orus_binary": self.orus_binary,
        }
        try:
            with open(output_file, "w") as f:
                json.dump(data, f, indent=2)
            print(f"\n💾 Detailed results saved to: {output_file}")
        except Exception as e:
            print(f"⚠️  Could not save results: {e}")


def main():
    parser = argparse.ArgumentParser(description="Orus vs Java Benchmark")
    parser.add_argument("--orus-binary", default="../orus", help="Path to Orus binary")
    parser.add_argument("--iterations", type=int, default=50, help="Iterations per test")
    parser.add_argument("--output", default="results.json", help="Output file for JSON results")
    args = parser.parse_args()

    bench = OrusJavaBenchmark(args.orus_binary, args.iterations)
    if not bench.check_setup():
        return
    tests = [
        ("Simple Addition", "1 + 2 + 3 + 4 + 5"),
        ("Complex Arithmetic", "(1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18/3+19%5"),
        ("100 Number Chain", " + ".join(str(i) for i in range(1, 101))),
        ("Mixed Operations", "2*2*2*2*2+10+5-3*4/2+7%3")
    ]
    for name, expr in tests:
        bench.run_test(name, expr)
    bench.summary(args.output)

if __name__ == "__main__":
    main()
