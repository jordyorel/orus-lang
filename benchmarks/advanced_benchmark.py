#!/usr/bin/env python3
"""
Advanced Orus Language Benchmark Suite
Comprehensive performance testing and analysis tool
"""

import subprocess
import time
import statistics
import json
import sys
import os
import argparse
import platform
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, asdict
import tempfile

@dataclass
class BenchmarkResult:
    """Represents the result of a single benchmark run"""
    test_name: str
    avg_time: float
    min_time: float
    max_time: float
    median_time: float
    std_dev: float
    successful_runs: int
    total_runs: int
    operations_per_second: Optional[float] = None
    memory_usage: Optional[float] = None

@dataclass
class SystemInfo:
    """System information for benchmark context"""
    os: str
    arch: str
    hostname: str
    python_version: str
    cpu_count: int

class OrusBenchmark:
    """Main benchmark runner for Orus language"""
    
    def __init__(self, orus_binary: str, iterations: int = 20, timeout: int = 30):
        self.orus_binary = Path(orus_binary)
        self.iterations = iterations
        self.timeout = timeout
        self.results: List[BenchmarkResult] = []
        
        if not self.orus_binary.exists():
            raise FileNotFoundError(f"Orus binary not found: {orus_binary}")
    
    def get_system_info(self) -> SystemInfo:
        """Collect system information"""
        return SystemInfo(
            os=platform.system(),
            arch=platform.machine(),
            hostname=platform.node(),
            python_version=platform.python_version(),
            cpu_count=os.cpu_count() or 1
        )
    
    def run_single_benchmark(self, test_file: Path, test_name: str = None) -> BenchmarkResult:
        """Run benchmark on a single test file"""
        if test_name is None:
            test_name = test_file.stem
            
        print(f"🔄 Running benchmark: {test_name}")
        
        times = []
        errors = []
        
        for i in range(self.iterations):
            try:
                start_time = time.perf_counter()
                
                result = subprocess.run(
                    [str(self.orus_binary), str(test_file)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=self.timeout,
                    check=True,
                    text=True
                )
                
                end_time = time.perf_counter()
                execution_time = end_time - start_time
                times.append(execution_time)
                
                # Progress indicator
                if (i + 1) % 5 == 0:
                    print(f"  Progress: {i + 1}/{self.iterations}")
                    
            except subprocess.TimeoutExpired:
                errors.append(f"Timeout on iteration {i + 1}")
            except subprocess.CalledProcessError as e:
                errors.append(f"Error on iteration {i + 1}: {e.stderr.strip()}")
            except Exception as e:
                errors.append(f"Unexpected error on iteration {i + 1}: {str(e)}")
        
        if not times:
            raise RuntimeError(f"All benchmark runs failed for {test_name}. Errors: {errors}")
        
        # Calculate statistics
        avg_time = statistics.mean(times)
        min_time = min(times)
        max_time = max(times)
        median_time = statistics.median(times)
        std_dev = statistics.stdev(times) if len(times) > 1 else 0.0
        
        # Estimate operations per second (rough approximation)
        # Count arithmetic operations in the file
        operations_count = self._count_operations(test_file)
        ops_per_second = operations_count / avg_time if operations_count > 0 else None
        
        result = BenchmarkResult(
            test_name=test_name,
            avg_time=avg_time,
            min_time=min_time,
            max_time=max_time,
            median_time=median_time,
            std_dev=std_dev,
            successful_runs=len(times),
            total_runs=self.iterations,
            operations_per_second=ops_per_second
        )
        
        self.results.append(result)
        print(f"✅ Completed: {test_name} (avg: {avg_time:.6f}s)")
        
        if errors:
            print(f"⚠️  {len(errors)} failed runs out of {self.iterations}")
            
        return result
    
    def _count_operations(self, test_file: Path) -> int:
        """Count arithmetic operations in a test file (rough estimate)"""
        try:
            content = test_file.read_text()
            ops = content.count('+') + content.count('-') + content.count('*') + content.count('/') + content.count('%')
            return ops
        except:
            return 0
    
    def compare_with_python(self, expression: str) -> Dict:
        """Compare Orus performance with Python for the same expression"""
        print("🔄 Running Orus vs Python comparison...")
        
        # Create temporary Orus file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
            f.write(expression)
            temp_orus_file = f.name
        
        try:
            # Benchmark Orus
            orus_times = []
            for _ in range(self.iterations):
                start = time.perf_counter()
                subprocess.run([str(self.orus_binary), temp_orus_file], 
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
                end = time.perf_counter()
                orus_times.append(end - start)
            
            # Benchmark Python
            python_times = []
            python_expr = expression.replace('/', '//').replace('%', '%')  # Adjust for integer division
            for _ in range(self.iterations):
                start = time.perf_counter()
                subprocess.run([sys.executable, '-c', f'print({python_expr})'], 
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
                end = time.perf_counter()
                python_times.append(end - start)
            
            orus_avg = statistics.mean(orus_times)
            python_avg = statistics.mean(python_times)
            speedup = python_avg / orus_avg
            
            result = {
                'expression': expression,
                'iterations': self.iterations,
                'orus': {
                    'avg': orus_avg,
                    'min': min(orus_times),
                    'max': max(orus_times),
                    'std_dev': statistics.stdev(orus_times) if len(orus_times) > 1 else 0
                },
                'python': {
                    'avg': python_avg,
                    'min': min(python_times),
                    'max': max(python_times),
                    'std_dev': statistics.stdev(python_times) if len(python_times) > 1 else 0
                },
                'speedup': speedup
            }
            
            print(f"✅ Comparison completed:")
            print(f"   Orus:   {orus_avg:.6f}s")
            print(f"   Python: {python_avg:.6f}s")
            print(f"   Speedup: {speedup:.2f}x")
            
            return result
            
        finally:
            os.unlink(temp_orus_file)
    
    def run_stress_test(self, base_expression: str, max_operations: int = 1000, step: int = 100) -> List[BenchmarkResult]:
        """Run stress test with increasing operation counts"""
        print(f"🔄 Running stress test up to {max_operations} operations...")
        
        stress_results = []
        
        for ops_count in range(step, max_operations + 1, step):
            # Generate expression with specified number of operations
            expression = " + ".join(str(i) for i in range(1, ops_count + 1))
            
            with tempfile.NamedTemporaryFile(mode='w', suffix='.orus', delete=False) as f:
                f.write(expression)
                temp_file = f.name
            
            try:
                test_name = f"stress_{ops_count}_ops"
                result = self.run_single_benchmark(Path(temp_file), test_name)
                stress_results.append(result)
                
                # Check if performance is degrading significantly
                if result.avg_time > 1.0:  # More than 1 second
                    print(f"⚠️  Performance degradation detected at {ops_count} operations")
                    break
                    
            except Exception as e:
                print(f"❌ Stress test failed at {ops_count} operations: {e}")
                break
            finally:
                os.unlink(temp_file)
        
        return stress_results
    
    def save_results(self, output_file: Path):
        """Save benchmark results to JSON file"""
        data = {
            'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
            'system_info': asdict(self.get_system_info()),
            'benchmark_config': {
                'orus_binary': str(self.orus_binary),
                'iterations': self.iterations,
                'timeout': self.timeout
            },
            'results': [asdict(result) for result in self.results]
        }
        
        output_file.parent.mkdir(parents=True, exist_ok=True)
        with open(output_file, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"💾 Results saved to: {output_file}")
    
    def generate_report(self, output_file: Path):
        """Generate a markdown report"""
        with open(output_file, 'w') as f:
            f.write("# Orus Language Benchmark Report\n\n")
            f.write(f"**Generated:** {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"**System:** {platform.system()} {platform.machine()}\n")
            f.write(f"**Iterations:** {self.iterations}\n\n")
            
            f.write("## Results Summary\n\n")
            
            for result in self.results:
                f.write(f"### {result.test_name}\n\n")
                f.write(f"- **Average time:** {result.avg_time:.6f}s\n")
                f.write(f"- **Min time:** {result.min_time:.6f}s\n")
                f.write(f"- **Max time:** {result.max_time:.6f}s\n")
                f.write(f"- **Median time:** {result.median_time:.6f}s\n")
                f.write(f"- **Std deviation:** {result.std_dev:.6f}s\n")
                f.write(f"- **Success rate:** {result.successful_runs}/{result.total_runs} ({result.successful_runs/result.total_runs*100:.1f}%)\n")
                
                if result.operations_per_second:
                    f.write(f"- **Operations/sec:** {result.operations_per_second:.0f}\n")
                
                f.write("\n")
        
        print(f"📊 Report generated: {output_file}")

def main():
    parser = argparse.ArgumentParser(description="Orus Language Benchmark Suite")
    parser.add_argument('--orus-binary', default='../orus', help='Path to Orus binary')
    parser.add_argument('--iterations', '-i', type=int, default=20, help='Number of iterations per test')
    parser.add_argument('--timeout', '-t', type=int, default=30, help='Timeout per test run (seconds)')
    parser.add_argument('--output-dir', '-o', default='./results', help='Output directory for results')
    parser.add_argument('--test-files', nargs='*', help='Specific test files to run')
    parser.add_argument('--comparison', action='store_true', help='Run Orus vs Python comparison')
    parser.add_argument('--stress-test', action='store_true', help='Run stress test with increasing operations')
    parser.add_argument('--all', action='store_true', help='Run all available tests')
    
    args = parser.parse_args()
    
    # Initialize benchmark runner
    try:
        benchmark = OrusBenchmark(args.orus_binary, args.iterations, args.timeout)
    except FileNotFoundError as e:
        print(f"❌ {e}")
        print("Please build the Orus interpreter first: make")
        sys.exit(1)
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    timestamp = time.strftime('%Y%m%d_%H%M%S')
    
    # Run benchmarks based on arguments
    if args.test_files:
        # Run specific test files
        for test_file in args.test_files:
            test_path = Path(test_file)
            if test_path.exists():
                benchmark.run_single_benchmark(test_path)
            else:
                print(f"❌ Test file not found: {test_file}")
    
    elif args.all or not any([args.comparison, args.stress_test]):
        # Run all available .orus files
        test_files = list(Path('.').glob('*.orus'))
        if test_files:
            print(f"🎯 Found {len(test_files)} test files")
            for test_file in sorted(test_files):
                benchmark.run_single_benchmark(test_file)
        else:
            print("⚠️  No .orus test files found in current directory")
    
    # Run comparison if requested
    if args.comparison:
        expression = "(1 + 2 + 3 + 4 + 5) * (6 + 7 + 8 + 9 + 10) - (11 + 12 + 13 + 14 + 15) + 16 * 17 - 18 / 3 + 19 % 5"
        comparison_result = benchmark.compare_with_python(expression)
        
        # Save comparison result
        comparison_file = output_dir / f"comparison_{timestamp}.json"
        with open(comparison_file, 'w') as f:
            json.dump(comparison_result, f, indent=2)
    
    # Run stress test if requested
    if args.stress_test:
        stress_results = benchmark.run_stress_test("1", max_operations=500, step=50)
        
        # Save stress test results
        stress_file = output_dir / f"stress_test_{timestamp}.json"
        stress_data = {
            'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
            'results': [asdict(result) for result in stress_results]
        }
        with open(stress_file, 'w') as f:
            json.dump(stress_data, f, indent=2)
    
    # Save results and generate report
    if benchmark.results:
        results_file = output_dir / f"benchmark_results_{timestamp}.json"
        report_file = output_dir / f"benchmark_report_{timestamp}.md"
        
        benchmark.save_results(results_file)
        benchmark.generate_report(report_file)
        
        print(f"\n🎉 Benchmark suite completed!")
        print(f"📈 {len(benchmark.results)} tests executed")
        print(f"📁 Results available in: {output_dir}")
    else:
        print("⚠️  No benchmarks were executed")

if __name__ == '__main__':
    main()
