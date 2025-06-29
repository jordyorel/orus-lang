#!/usr/bin/env python3
"""
Orus vs JavaScript Performance Benchmark Suite
Compares Orus VM performance against Node.js V8 engine
"""

import subprocess
import time
import statistics
import json
import os
import sys
import tempfile
from pathlib import Path
import platform

class OrusJavaScriptBenchmark:
    """Advanced benchmark suite for Orus vs JavaScript performance comparison"""
    
    def __init__(self, orus_binary="../orus", iterations=50, timeout=30):
        self.orus_binary = orus_binary
        self.iterations = iterations
        self.timeout = timeout
        self.results = []
        
    def check_setup(self):
        """Verify Orus binary and Node.js exist and are working"""
        # Check Orus
        if not os.path.exists(self.orus_binary):
            print(f"❌ Orus binary not found at {self.orus_binary}")
            print("   Please build Orus first: cd .. && make")
            return False
            
        # Test Orus functionality
        try:
            with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
                f.write("1 + 1")
                test_file = f.name
            
            subprocess.run([self.orus_binary, test_file], 
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, 
                          timeout=5, check=True)
            os.unlink(test_file)
            print("✅ Orus binary is working")
        except Exception as e:
            print(f"❌ Orus binary test failed: {e}")
            return False
            
        # Check Node.js
        try:
            result = subprocess.run(['node', '--version'], 
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=5, check=True)
            node_version = result.stdout.decode().strip()
            print(f"✅ Node.js found: {node_version}")
        except FileNotFoundError:
            print("❌ Node.js not found. Please install Node.js to run JavaScript benchmarks.")
            return False
        except Exception as e:
            print(f"❌ Node.js test failed: {e}")
            return False
            
        return True
        
    def get_system_info(self):
        """Get system information for benchmark context"""
        try:
            node_result = subprocess.run(['node', '--version'], 
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            node_version = node_result.stdout.decode().strip() if node_result.returncode == 0 else "unknown"
        except:
            node_version = "unknown"
            
        return {
            "os": platform.system(),
            "arch": platform.machine(),
            "node_version": node_version,
            "hostname": platform.node()
        }
        
    def run_orus_benchmark(self, code, test_name):
        """Run Orus benchmark for given code"""
        times = []
        
        for i in range(self.iterations):
            try:
                # Create temporary file with Orus code
                with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
                    f.write(code)
                    temp_file = f.name
                
                # Time the execution
                start_time = time.perf_counter()
                result = subprocess.run([self.orus_binary, temp_file],
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                      timeout=self.timeout,
                                      check=True)
                end_time = time.perf_counter()
                
                times.append(end_time - start_time)
                
                # Clean up
                os.unlink(temp_file)
                
                # Progress indicator
                if self.iterations >= 10 and (i + 1) % (self.iterations // 10) == 0:
                    print(f"      Progress: {i + 1}/{self.iterations}")
                elif self.iterations < 10 and (i + 1) == self.iterations:
                    print(f"      Progress: {i + 1}/{self.iterations}")
                    
            except subprocess.TimeoutExpired:
                print(f"   ⚠️  Timeout for {test_name}")
                os.unlink(temp_file)
                return None
            except Exception as e:
                print(f"   ❌ Error in {test_name}: {e}")
                if 'temp_file' in locals():
                    os.unlink(temp_file)
                return None
                
        return {
            "avg": statistics.mean(times),
            "min": min(times),
            "max": max(times),
            "median": statistics.median(times),
            "std_dev": statistics.stdev(times) if len(times) > 1 else 0,
            "total_time": sum(times)
        }
        
    def run_javascript_benchmark(self, code, test_name):
        """Run JavaScript benchmark for given code"""
        times = []
        
        # Convert Orus code to JavaScript equivalent
        js_code = self.convert_orus_to_js(code)
        
        for i in range(self.iterations):
            try:
                # Create temporary JavaScript file
                with tempfile.NamedTemporaryFile(mode='w', suffix='.js', delete=False) as f:
                    f.write(js_code)
                    temp_file = f.name
                
                # Time the execution
                start_time = time.perf_counter()
                subprocess.run(['node', temp_file],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              timeout=self.timeout,
                              check=True)
                end_time = time.perf_counter()
                
                times.append(end_time - start_time)
                
                # Clean up
                os.unlink(temp_file)
                
                # Progress indicator
                if self.iterations >= 10 and (i + 1) % (self.iterations // 10) == 0:
                    print(f"      Progress: {i + 1}/{self.iterations}")
                elif self.iterations < 10 and (i + 1) == self.iterations:
                    print(f"      Progress: {i + 1}/{self.iterations}")
                    
            except subprocess.TimeoutExpired:
                print(f"   ⚠️  Timeout for {test_name}")
                os.unlink(temp_file)
                return None
            except Exception as e:
                print(f"   ❌ Error in {test_name}: {e}")
                if 'temp_file' in locals():
                    os.unlink(temp_file)
                return None
                
        return {
            "avg": statistics.mean(times),
            "min": min(times),
            "max": max(times),
            "median": statistics.median(times),
            "std_dev": statistics.stdev(times) if len(times) > 1 else 0,
            "total_time": sum(times)
        }
        
    def convert_orus_to_js(self, orus_code):
        """Convert Orus code to equivalent JavaScript"""
        # Handle basic arithmetic expressions
        js_code = orus_code
        
        # Convert print statements
        if 'print(' in js_code:
            js_code = js_code.replace('print(', 'console.log(')
        
        # Convert let statements (Orus style to JS style)
        lines = js_code.split('\n')
        js_lines = []
        
        for line in lines:
            line = line.strip()
            if not line or line.startswith('//'):
                js_lines.append(line)
                continue
                
            # Handle variable declarations
            if line.startswith('let ') and '=' in line:
                js_lines.append(line + ';')
            # Handle simple expressions (add to result)
            elif any(op in line for op in ['+', '-', '*', '/', '%']) and '=' not in line and 'print(' not in line:
                js_lines.append(f'console.log({line});')
            # Handle print statements
            elif 'print(' in line:
                if not line.endswith(';'):
                    line += ';'
                js_lines.append(line.replace('print(', 'console.log('))
            # Other statements
            else:
                if line and not line.endswith(';'):
                    line += ';'
                js_lines.append(line)
                
        return '\n'.join(js_lines)
        
    def run_comparison(self, test_name, orus_code, description=""):
        """Run comparison between Orus and JavaScript"""
        print(f"🔄 Running test: {test_name}")
        if description:
            print(f"   {description}")
        print(f"   Iterations: {self.iterations}")
        
        # Run Orus benchmark
        print("   🔄 Testing Orus...")
        orus_result = self.run_orus_benchmark(orus_code, test_name)
        
        if orus_result is None:
            print(f"   ❌ Orus benchmark failed for {test_name}")
            return None
            
        # Run JavaScript benchmark
        print("   🔄 Testing JavaScript...")
        js_result = self.run_javascript_benchmark(orus_code, test_name)
        
        if js_result is None:
            print(f"   ❌ JavaScript benchmark failed for {test_name}")
            return None
            
        # Calculate speedup
        speedup = js_result["avg"] / orus_result["avg"]
        winner = "Orus" if speedup > 1 else "JavaScript"
        
        result = {
            "test_name": test_name,
            "description": description,
            "iterations": self.iterations,
            "orus": orus_result,
            "javascript": js_result,
            "speedup": speedup,
            "winner": winner
        }
        
        # Display results
        print(f"\n   📊 Results for {test_name}:")
        print(f"      Orus:       {orus_result['avg']:.6f}s (avg)")
        print(f"      JavaScript: {js_result['avg']:.6f}s (avg)")
        if speedup > 1:
            print(f"      Speedup: {speedup:.2f}x")
            print(f"      🏆 Orus is {speedup:.2f}x FASTER!")
        else:
            print(f"      Speedup: {1/speedup:.2f}x")
            print(f"      🏆 JavaScript is {1/speedup:.2f}x FASTER!")
        print()
        
        self.results.append(result)
        return result
        
    def generate_stress_test(self, num_ops):
        """Generate a stress test with specified number of operations"""
        return " + ".join(str(i) for i in range(1, num_ops + 1))
        
    def run_comprehensive_benchmark(self):
        """Run the complete benchmark suite"""
        print("🚀 Orus vs JavaScript Benchmark Suite")
        print("=" * 50)
        
        system_info = self.get_system_info()
        print(f"System: {system_info['os']} {system_info['arch']}")
        print(f"Node.js: {system_info['node_version']}")
        print()
        
        if not self.check_setup():
            return None
            
        # Define test cases
        test_cases = [
            ("Simple Addition", "1 + 2 + 3 + 4 + 5", "Basic arithmetic operations"),
            ("Complex Arithmetic", "(1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18/3+19%5", "Complex expression with operator precedence"),
            ("100 Number Chain", self.generate_stress_test(100), "Chain of 100 addition operations"),
            ("Mixed Operations", "2*2*2*2*2+10+5-3*4/2+7%3", "Mixed arithmetic operations"),
        ]
        
        # Run file-based tests
        benchmark_files = [
            ("complex_expression.orus", "Complex Expression from file"),
            ("test_150_ops_fixed.orus", "150 Operations from file"),
        ]
        
        for filename, description in benchmark_files:
            file_path = Path(filename)
            if file_path.exists():
                try:
                    with open(file_path, 'r') as f:
                        content = f.read().strip()
                        test_cases.append((f"File: {description}", content, f"From {filename}"))
                except Exception as e:
                    print(f"   ⚠️  Could not read {filename}: {e}")
                    
        # Run stress tests
        print("🚀 Stress Test: 50 to 500 operations")
        print("=" * 50)
        
        stress_tests = [50, 100, 150, 200, 250, 300, 350, 400, 450, 500]
        for ops in stress_tests:
            test_cases.append((f"Stress Test {ops} ops", self.generate_stress_test(ops), f"Chain of {ops} operations"))
            
        # Run all tests
        for test_name, code, description in test_cases:
            self.run_comparison(test_name, code, description)
            
        # Generate summary
        self.generate_summary(system_info)
        
        return self.results
        
    def generate_summary(self, system_info):
        """Generate comprehensive benchmark summary"""
        if not self.results:
            print("❌ No results to summarize")
            return
            
        orus_wins = sum(1 for r in self.results if r["winner"] == "Orus")
        js_wins = sum(1 for r in self.results if r["winner"] == "JavaScript")
        
        speedups = [r["speedup"] if r["winner"] == "Orus" else 1/r["speedup"] for r in self.results]
        overall_speedup = statistics.mean(speedups)
        
        print("=" * 60)
        print("📊 COMPREHENSIVE BENCHMARK SUMMARY")
        print("=" * 60)
        print(f"Total Tests:     {len(self.results)}")
        print(f"Orus Wins:       {orus_wins}")
        print(f"JavaScript Wins: {js_wins}")
        print(f"Overall Speedup: {overall_speedup:.2f}x")
        print()
        
        if orus_wins > js_wins:
            print(f"🏆 OVERALL WINNER: Orus is {overall_speedup:.2f}x FASTER!")
        elif js_wins > orus_wins:
            print(f"🏆 OVERALL WINNER: JavaScript is {overall_speedup:.2f}x FASTER!")
        else:
            print("🤝 TIE: Both languages performed equally well!")
        print()
        
        # Performance analysis
        if overall_speedup >= 3:
            print("⭐ Performance Analysis:")
            print("   ⭐ EXCELLENT: Orus significantly outperforms JavaScript!")
        elif overall_speedup >= 1.5:
            print("⭐ Performance Analysis:")
            print("   🟢 GOOD: Orus shows solid performance advantage!")
        elif overall_speedup >= 0.8:
            print("⭐ Performance Analysis:")
            print("   🟡 COMPETITIVE: Very close performance between both!")
        else:
            print("⭐ Performance Analysis:")
            print("   🔴 JavaScript has a performance advantage!")
        print()
        
        # Individual results
        print("📋 Individual Test Results:")
        for result in self.results:
            if result["winner"] == "Orus":
                print(f"   🟢 {result['test_name']}: {result['speedup']:.2f}x")
            else:
                print(f"   🔴 {result['test_name']}: {1/result['speedup']:.2f}x")
        print()
        
        # Save detailed results
        output_file = "benchmark_results_js.json"
        detailed_results = {
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "system_info": system_info,
            "benchmark_config": {
                "orus_binary": self.orus_binary,
                "iterations": self.iterations,
                "timeout": self.timeout
            },
            "summary": {
                "total_tests": len(self.results),
                "orus_wins": orus_wins,
                "javascript_wins": js_wins,
                "overall_speedup": overall_speedup
            },
            "results": self.results
        }
        
        try:
            with open(output_file, 'w') as f:
                json.dump(detailed_results, f, indent=2)
            print(f"💾 Detailed results saved to: {output_file}")
        except Exception as e:
            print(f"⚠️  Could not save results: {e}")
        print()

def main():
    """Main function"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Orus vs JavaScript Performance Benchmark")
    parser.add_argument("--orus-binary", default="../orus", help="Path to Orus binary")
    parser.add_argument("--iterations", type=int, default=50, help="Number of iterations per test")
    parser.add_argument("--timeout", type=int, default=30, help="Timeout per test in seconds")
    
    args = parser.parse_args()
    
    benchmark = OrusJavaScriptBenchmark(
        orus_binary=args.orus_binary,
        iterations=args.iterations,
        timeout=args.timeout
    )
    
    results = benchmark.run_comprehensive_benchmark()
    
    if results:
        print("🎉 Benchmark completed successfully!")
        print("📁 Check benchmark_results_js.json for detailed results")
    else:
        print("❌ Benchmark failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()