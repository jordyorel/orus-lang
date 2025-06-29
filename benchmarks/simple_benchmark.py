#!/usr/bin/env python3
"""
Advanced Orus vs Python Benchmark Suite
Complete performance analysis in one script
"""

import subprocess
import time
import statistics
import json
import os
import sys
import argparse
import platform
import tempfile
from pathlib import Path

class OrusPythonBenchmark:
    """Advanced benchmark suite for Orus vs Python performance comparison"""
    
    def __init__(self, orus_binary="../orus", iterations=50, timeout=30):
        self.orus_binary = orus_binary
        self.iterations = iterations
        self.timeout = timeout
        self.results = []
        
    def check_setup(self):
        """Verify Orus binary exists and is working"""
        if not os.path.exists(self.orus_binary):
            print(f"❌ Orus binary not found at {self.orus_binary}")
            print("   Please build Orus first: cd .. && make")
            return False
            
        # Test basic functionality
        try:
            with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
                f.write("1 + 1")
                test_file = f.name
            
            result = subprocess.run([self.orus_binary, test_file], 
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, 
                                  timeout=5, check=True)
            os.unlink(test_file)
            print("✅ Orus binary is working")
            return True
            
        except Exception as e:
            print(f"❌ Orus binary test failed: {e}")
            return False
    
    def run_single_comparison(self, expression, test_name, orus_file=None):
        """Run comparison for a single expression"""
        print(f"\n🔄 Running test: {test_name}")
        print(f"   Expression: {expression}")
        print(f"   Iterations: {self.iterations}")
        
        # Benchmark Orus
        orus_times = []
        temp_file = None
        
        try:
            if orus_file and os.path.exists(orus_file):
                test_file = orus_file
            else:
                temp_file = tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False)
                temp_file.write(expression)
                temp_file.close()
                test_file = temp_file.name
            
            print("   🔄 Testing Orus...")
            for i in range(self.iterations):
                start = time.perf_counter()
                subprocess.run([self.orus_binary, test_file], 
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=self.timeout, check=True)
                end = time.perf_counter()
                orus_times.append(end - start)
                
                if (i + 1) % 10 == 0:
                    print(f"      Progress: {i + 1}/{self.iterations}")
        
        except Exception as e:
            print(f"   ❌ Orus test failed: {e}")
            return None
        
        finally:
            if temp_file:
                os.unlink(temp_file.name)
        
        # Benchmark Python
        python_times = []
        python_expr = expression.replace('/', '//').replace('%', '%')  # Integer ops
        
        try:
            print("   🔄 Testing Python...")
            for i in range(self.iterations):
                start = time.perf_counter()
                subprocess.run(['python3', '-c', f'print({python_expr})'], 
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=self.timeout, check=True)
                end = time.perf_counter()
                python_times.append(end - start)
                
                if (i + 1) % 10 == 0:
                    print(f"      Progress: {i + 1}/{self.iterations}")
        
        except Exception as e:
            print(f"   ❌ Python test failed: {e}")
            return None
        
        # Calculate statistics
        orus_stats = self._calculate_stats(orus_times)
        python_stats = self._calculate_stats(python_times)
        speedup = python_stats['avg'] / orus_stats['avg']
        
        result = {
            'test_name': test_name,
            'expression': expression,
            'iterations': self.iterations,
            'orus': orus_stats,
            'python': python_stats,
            'speedup': speedup,
            'winner': 'Orus' if speedup > 1 else 'Python'
        }
        
        self.results.append(result)
        self._print_result(result)
        return result
    
    def _calculate_stats(self, times):
        """Calculate comprehensive statistics"""
        return {
            'avg': statistics.mean(times),
            'min': min(times),
            'max': max(times),
            'median': statistics.median(times),
            'std_dev': statistics.stdev(times) if len(times) > 1 else 0,
            'total_time': sum(times)
        }
    
    def _print_result(self, result):
        """Print formatted result"""
        print(f"\n   📊 Results for {result['test_name']}:")
        print(f"      Orus:    {result['orus']['avg']:.6f}s (avg)")
        print(f"      Python:  {result['python']['avg']:.6f}s (avg)")
        print(f"      Speedup: {result['speedup']:.2f}x")
        
        if result['speedup'] > 1:
            print(f"      🏆 Orus is {result['speedup']:.2f}x FASTER!")
        else:
            print(f"      🏆 Python is {1/result['speedup']:.2f}x faster")
    
    def run_comprehensive_benchmark(self):
        """Run multiple benchmark tests"""
        print("🚀 Advanced Orus vs Python Benchmark Suite")
        print("=" * 50)
        print(f"System: {platform.system()} {platform.machine()}")
        print(f"Python: {platform.python_version()}")
        print()
        
        if not self.check_setup():
            return False
        
        # Test 1: Simple arithmetic
        self.run_single_comparison(
            "1 + 2 + 3 + 4 + 5",
            "Simple Addition"
        )
        
        # Test 2: Complex expression
        self.run_single_comparison(
            "(1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18/3+19%5",
            "Complex Arithmetic"
        )
        
        # Test 3: Longer chain
        chain_100 = " + ".join(str(i) for i in range(1, 101))
        self.run_single_comparison(
            chain_100,
            "100 Number Chain"
        )
        
        # Test 4: Mixed operations
        self.run_single_comparison(
            "2*2*2*2*2+10+5-3*4/2+7%3",
            "Mixed Operations"
        )
        
        # Test 5: Test files if they exist
        test_files = [
            ("complex_expression.orus", "File: Complex Expression"),
            ("test_150_ops_fixed.orus", "File: 150 Operations"),
        ]
        
        for filename, test_name in test_files:
            if os.path.exists(filename):
                with open(filename, 'r') as f:
                    content = f.read().strip()
                self.run_single_comparison(content, test_name, filename)
        
        return True
    
    def stress_test(self, max_ops=500, step=50):
        """Run stress test with increasing operation counts"""
        print(f"\n� Stress Test: {step} to {max_ops} operations")
        print("=" * 50)
        
        stress_results = []
        
        for ops in range(step, max_ops + 1, step):
            expression = " + ".join(str(i) for i in range(1, ops + 1))
            result = self.run_single_comparison(expression, f"Stress Test {ops} ops")
            
            if result:
                stress_results.append(result)
                
                # Stop if performance degrades significantly
                if result['orus']['avg'] > 0.1:  # More than 100ms
                    print(f"   ⚠️ Stopping stress test - performance threshold reached")
                    break
            else:
                print(f"   ❌ Stress test failed at {ops} operations")
                break
        
        return stress_results
    
    def generate_summary(self):
        """Generate comprehensive summary report"""
        if not self.results:
            print("⚠️ No results to summarize")
            return
        
        print("\n" + "=" * 60)
        print("📊 COMPREHENSIVE BENCHMARK SUMMARY")
        print("=" * 60)
        
        total_orus_time = sum(r['orus']['total_time'] for r in self.results)
        total_python_time = sum(r['python']['total_time'] for r in self.results)
        overall_speedup = total_python_time / total_orus_time
        
        orus_wins = sum(1 for r in self.results if r['winner'] == 'Orus')
        python_wins = len(self.results) - orus_wins
        
        print(f"Total Tests:     {len(self.results)}")
        print(f"Orus Wins:       {orus_wins}")
        print(f"Python Wins:     {python_wins}")
        print(f"Overall Speedup: {overall_speedup:.2f}x")
        print()
        
        if overall_speedup > 1:
            print(f"🏆 OVERALL WINNER: Orus is {overall_speedup:.2f}x FASTER!")
        else:
            print(f"🏆 OVERALL WINNER: Python is {1/overall_speedup:.2f}x faster")
        
        print("\n� Performance Analysis:")
        if overall_speedup > 10:
            print("   🚀 EXCEPTIONAL: Orus shows exceptional performance!")
        elif overall_speedup > 5:
            print("   ⭐ EXCELLENT: Orus significantly outperforms Python!")
        elif overall_speedup > 2:
            print("   👍 GOOD: Orus shows solid performance advantages!")
        elif overall_speedup > 1.5:
            print("   ✅ MODERATE: Orus has moderate performance gains!")
        elif overall_speedup > 1:
            print("   📊 SLIGHT: Orus has slight performance advantages!")
        else:
            print("   🔧 OPTIMIZATION NEEDED: Python currently faster!")
        
        # Individual test summary
        print(f"\n📋 Individual Test Results:")
        for result in self.results:
            winner_icon = "🟢" if result['winner'] == 'Orus' else "🔴"
            print(f"   {winner_icon} {result['test_name']}: {result['speedup']:.2f}x")
    
    def save_results(self, filename="advanced_benchmark_results.json"):
        """Save detailed results to JSON file"""
        output = {
            'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
            'system_info': {
                'os': platform.system(),
                'arch': platform.machine(),
                'python_version': platform.python_version(),
                'hostname': platform.node()
            },
            'benchmark_config': {
                'orus_binary': self.orus_binary,
                'iterations': self.iterations,
                'timeout': self.timeout
            },
            'summary': {
                'total_tests': len(self.results),
                'orus_wins': sum(1 for r in self.results if r['winner'] == 'Orus'),
                'overall_speedup': sum(r['python']['total_time'] for r in self.results) / 
                                 sum(r['orus']['total_time'] for r in self.results) if self.results else 0
            },
            'results': self.results
        }
        
        with open(filename, 'w') as f:
            json.dump(output, f, indent=2)
        
        print(f"\n💾 Detailed results saved to: {filename}")

def main():
    parser = argparse.ArgumentParser(description="Advanced Orus vs Python Benchmark")
    parser.add_argument('--iterations', '-i', type=int, default=50, 
                       help='Number of iterations per test (default: 50)')
    parser.add_argument('--orus-binary', default='../orus', 
                       help='Path to Orus binary (default: ../orus)')
    parser.add_argument('--quick', action='store_true', 
                       help='Run quick benchmark (10 iterations)')
    parser.add_argument('--stress', action='store_true', 
                       help='Include stress testing')
    parser.add_argument('--output', '-o', default='benchmark_results_python.json',
                       help='Output file for results')
    
    args = parser.parse_args()
    
    # Adjust iterations for quick mode
    iterations = 10 if args.quick else args.iterations
    
    # Run benchmark
    benchmark = OrusPythonBenchmark(
        orus_binary=args.orus_binary,
        iterations=iterations,
        timeout=30
    )
    
    success = benchmark.run_comprehensive_benchmark()
    
    if success:
        if args.stress:
            benchmark.stress_test()
        
        benchmark.generate_summary()
        benchmark.save_results(args.output)
        
        print(f"\n🎉 Benchmark completed successfully!")
        print(f"� Check {args.output} for detailed results")
    else:
        print(f"\n❌ Benchmark failed to complete")
        sys.exit(1)

if __name__ == '__main__':
    main()
