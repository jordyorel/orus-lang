#!/usr/bin/env python3
"""
Orus vs Lua Comprehensive Benchmark Suite
==========================================

Compares Orus VM performance against Lua 5.4, one of the fastest scripting languages.
Tests various computational scenarios to evaluate VM performance.
"""

import subprocess
import time
import argparse
import json
import sys
import os
from pathlib import Path

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def print_colored(text, color):
    print(f"{color}{text}{Colors.ENDC}")

def print_header(text):
    print_colored(f"\n🚀 {text}", Colors.HEADER + Colors.BOLD)
    print_colored("=" * (len(text) + 4), Colors.HEADER)

def print_success(text):
    print_colored(f"✅ {text}", Colors.GREEN)

def print_info(text):
    print_colored(f"🔄 {text}", Colors.BLUE)

def print_result(text):
    print_colored(f"📊 {text}", Colors.CYAN)

def print_winner(text):
    print_colored(f"🏆 {text}", Colors.YELLOW + Colors.BOLD)

def check_dependencies():
    """Check if required binaries exist"""
    orus_path = "../orus"
    if not os.path.exists(orus_path):
        print_colored("❌ Orus binary not found at ../orus", Colors.RED)
        return False
    
    try:
        result = subprocess.run(["lua", "-v"], capture_output=True, text=True)
        if result.returncode != 0:
            print_colored("❌ Lua not found", Colors.RED)
            return False
        # Lua version is in stderr, format: "Lua 5.4.8  Copyright..."
        version_line = result.stderr.strip()
        if "Lua" in version_line:
            lua_version = version_line.split()[1]
            print_success(f"Lua found: {lua_version}")
        else:
            print_success("Lua found")
    except FileNotFoundError:
        print_colored("❌ Lua not found", Colors.RED)
        return False
    
    return True

def run_orus(expression, iterations=1):
    """Run Orus with expression and measure time"""
    times = []
    orus_path = "../orus"
    
    for _ in range(iterations):
        start_time = time.perf_counter()
        try:
            result = subprocess.run(
                [orus_path], 
                input=expression,
                capture_output=True, 
                text=True, 
                timeout=30
            )
            end_time = time.perf_counter()
            
            if result.returncode != 0:
                print_colored(f"❌ Orus error: {result.stderr}", Colors.RED)
                return None, None
            
            times.append(end_time - start_time)
            output = result.stdout.strip()
            
        except subprocess.TimeoutExpired:
            print_colored("❌ Orus timeout", Colors.RED)
            return None, None
        except Exception as e:
            print_colored(f"❌ Orus execution error: {e}", Colors.RED)
            return None, None
    
    avg_time = sum(times) / len(times)
    return avg_time, output

def run_lua(expression, iterations=1):
    """Run Lua with expression and measure time"""
    times = []
    
    # Wrap expression in print() for Lua
    lua_expr = f"print({expression})"
    
    for _ in range(iterations):
        start_time = time.perf_counter()
        try:
            result = subprocess.run(
                ["lua", "-e", lua_expr],
                capture_output=True, 
                text=True, 
                timeout=30
            )
            end_time = time.perf_counter()
            
            if result.returncode != 0:
                print_colored(f"❌ Lua error: {result.stderr}", Colors.RED)
                return None, None
            
            times.append(end_time - start_time)
            output = result.stdout.strip()
            
        except subprocess.TimeoutExpired:
            print_colored("❌ Lua timeout", Colors.RED)
            return None, None
        except Exception as e:
            print_colored(f"❌ Lua execution error: {e}", Colors.RED)
            return None, None
    
    avg_time = sum(times) / len(times)
    return avg_time, output

def run_orus_file(filename, iterations=1):
    """Run Orus with file and measure time"""
    times = []
    orus_path = "../orus"
    
    for _ in range(iterations):
        start_time = time.perf_counter()
        try:
            result = subprocess.run(
                [orus_path, filename], 
                capture_output=True, 
                text=True, 
                timeout=30
            )
            end_time = time.perf_counter()
            
            if result.returncode != 0:
                print_colored(f"❌ Orus file error: {result.stderr}", Colors.RED)
                return None, None
            
            times.append(end_time - start_time)
            output = result.stdout.strip()
            
        except subprocess.TimeoutExpired:
            print_colored("❌ Orus file timeout", Colors.RED)
            return None, None
        except Exception as e:
            print_colored(f"❌ Orus file execution error: {e}", Colors.RED)
            return None, None
    
    avg_time = sum(times) / len(times)
    return avg_time, output

def run_lua_file(filename, iterations=1):
    """Run Lua with file and measure time"""
    times = []
    
    for _ in range(iterations):
        start_time = time.perf_counter()
        try:
            result = subprocess.run(
                ["lua", filename],
                capture_output=True, 
                text=True, 
                timeout=30
            )
            end_time = time.perf_counter()
            
            if result.returncode != 0:
                print_colored(f"❌ Lua file error: {result.stderr}", Colors.RED)
                return None, None
            
            times.append(end_time - start_time)
            output = result.stdout.strip()
            
        except subprocess.TimeoutExpired:
            print_colored("❌ Lua file timeout", Colors.RED)
            return None, None
        except Exception as e:
            print_colored(f"❌ Lua file execution error: {e}", Colors.RED)
            return None, None
    
    avg_time = sum(times) / len(times)
    return avg_time, output

def create_lua_test_file(expression, filename):
    """Create Lua test file with expression"""
    with open(filename, 'w') as f:
        f.write(f"print({expression})\n")

def benchmark_test(name, orus_expr, lua_expr, iterations, description=""):
    """Run a single benchmark test"""
    print_info(f"Running test: {name}")
    if description:
        print(f"   {description}")
    print(f"   Iterations: {iterations}")
    
    # Test Orus
    print_info("   🔄 Testing Orus...")
    orus_time, orus_output = run_orus(orus_expr, iterations)
    if orus_time is None:
        return None
    
    # Test Lua
    print_info("   🔄 Testing Lua...")
    lua_time, lua_output = run_lua(lua_expr, iterations)
    if lua_time is None:
        return None
    
    # Verify outputs match (basic check)
    if orus_output != lua_output:
        print_colored(f"   ⚠️  Output mismatch: Orus={orus_output}, Lua={lua_output}", Colors.YELLOW)
    
    # Calculate speedup
    speedup = lua_time / orus_time
    
    print_result(f"   Results for {name}:")
    print_result(f"      Orus:    {orus_time:.6f}s (avg)")
    print_result(f"      Lua:     {lua_time:.6f}s (avg)")
    print_result(f"      Speedup: {speedup:.2f}x")
    
    if speedup > 1.0:
        print_winner(f"      🏆 Orus is {speedup:.2f}x FASTER!")
    else:
        print_colored(f"      🥈 Lua is {1/speedup:.2f}x faster", Colors.YELLOW)
    
    return {
        'name': name,
        'description': description,
        'orus_time': orus_time,
        'lua_time': lua_time,
        'speedup': speedup,
        'orus_output': orus_output,
        'lua_output': lua_output,
        'orus_wins': speedup > 1.0
    }

def benchmark_file_test(name, orus_file, lua_file, iterations, description=""):
    """Run a file-based benchmark test"""
    print_info(f"Running test: {name}")
    if description:
        print(f"   {description}")
    print(f"   Iterations: {iterations}")
    
    # Test Orus
    print_info("   🔄 Testing Orus...")
    orus_time, orus_output = run_orus_file(orus_file, iterations)
    if orus_time is None:
        return None
    
    # Test Lua
    print_info("   🔄 Testing Lua...")
    lua_time, lua_output = run_lua_file(lua_file, iterations)
    if lua_time is None:
        return None
    
    # Calculate speedup
    speedup = lua_time / orus_time
    
    print_result(f"   Results for {name}:")
    print_result(f"      Orus:    {orus_time:.6f}s (avg)")
    print_result(f"      Lua:     {lua_time:.6f}s (avg)")
    print_result(f"      Speedup: {speedup:.2f}x")
    
    if speedup > 1.0:
        print_winner(f"      🏆 Orus is {speedup:.2f}x FASTER!")
    else:
        print_colored(f"      🥈 Lua is {1/speedup:.2f}x faster", Colors.YELLOW)
    
    return {
        'name': name,
        'description': description,
        'orus_time': orus_time,
        'lua_time': lua_time,
        'speedup': speedup,
        'orus_output': orus_output,
        'lua_output': lua_output,
        'orus_wins': speedup > 1.0
    }

def create_stress_test_files(num_ops):
    """Create stress test files for large operations"""
    # Create Orus file
    orus_expr = " + ".join(str(i) for i in range(1, num_ops + 1))
    orus_file = f"stress_test_{num_ops}_ops.orus"
    with open(orus_file, 'w') as f:
        f.write(orus_expr + "\n")
    
    # Create Lua file
    lua_expr = " + ".join(str(i) for i in range(1, num_ops + 1))
    lua_file = f"stress_test_{num_ops}_ops.lua"
    with open(lua_file, 'w') as f:
        f.write(f"print({lua_expr})\n")
    
    return orus_file, lua_file

def run_benchmark_suite(iterations=30):
    """Run the complete benchmark suite"""
    print_header("Advanced Orus vs Lua Benchmark Suite")
    print_colored("System: macOS arm64", Colors.CYAN)
    
    # Check system info
    try:
        lua_result = subprocess.run(["lua", "-v"], capture_output=True, text=True)
        version_line = lua_result.stderr.strip()
        if "Lua" in version_line:
            lua_version = version_line.split()[1]
            print_colored(f"Lua: {lua_version}", Colors.CYAN)
        else:
            print_colored("Lua: Version detected", Colors.CYAN)
    except:
        print_colored("Lua: Unknown version", Colors.CYAN)
    
    if not check_dependencies():
        return None
    
    print_success("Orus binary is working")
    
    # Define test cases
    test_cases = [
        {
            'name': 'Simple Addition',
            'orus': '1 + 2 + 3 + 4 + 5',
            'lua': '1 + 2 + 3 + 4 + 5',
            'description': 'Basic arithmetic operations'
        },
        {
            'name': 'Complex Arithmetic',
            'orus': '(1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18/3+19%5',
            'lua': '(1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18/3+19%5',
            'description': 'Complex expression with operator precedence'
        },
        {
            'name': '100 Number Chain',
            'orus': ' + '.join(str(i) for i in range(1, 101)),
            'lua': ' + '.join(str(i) for i in range(1, 101)),
            'description': 'Chain of 100 addition operations'
        },
        {
            'name': 'Mixed Operations',
            'orus': '2*2*2*2*2+10+5-3*4/2+7%3',
            'lua': '2*2*2*2*2+10+5-3*4/2+7%3',
            'description': 'Mixed arithmetic operations'
        },
        {
            'name': 'Power Operations',
            'orus': '2*2*2*2*2*2*2*2*2*2',
            'lua': '2^10',
            'description': 'Exponentiation vs multiplication'
        },
        {
            'name': 'Large Numbers',
            'orus': '999999 + 999999 + 999999 + 999999 + 999999',
            'lua': '999999 + 999999 + 999999 + 999999 + 999999',
            'description': 'Large number arithmetic'
        }
    ]
    
    results = []
    
    # Run expression tests
    for test in test_cases:
        result = benchmark_test(
            test['name'],
            test['orus'],
            test['lua'],
            iterations,
            test['description']
        )
        if result:
            results.append(result)
    
    # Run file-based tests
    if os.path.exists('complex_expression.orus'):
        # Create equivalent Lua file
        create_lua_test_file(
            '(1 + 2 + 3 + 4 + 5) * (6 + 7 + 8 + 9 + 10) - (11 + 12 + 13 + 14 + 15) + 16 * 17 - 18 / 3 + 19 % 5',
            'complex_expression.lua'
        )
        result = benchmark_file_test(
            'File: Complex Expression',
            'complex_expression.orus',
            'complex_expression.lua',
            iterations,
            'From complex_expression.orus'
        )
        if result:
            results.append(result)
    
    if os.path.exists('test_150_ops_fixed.orus'):
        # Create equivalent Lua file
        expr_150 = ' + '.join(str(i) for i in range(1, 151))
        create_lua_test_file(expr_150, 'test_150_ops.lua')
        result = benchmark_file_test(
            'File: 150 Operations',
            'test_150_ops_fixed.orus',
            'test_150_ops.lua',
            iterations,
            'From test_150_ops_fixed.orus'
        )
        if result:
            results.append(result)
    
    # Stress tests
    stress_tests = [50, 100, 150, 200, 250, 300, 350, 400, 450, 500]
    
    print_header("🔥 Stress Test: 50 to 500 operations")
    
    for num_ops in stress_tests:
        orus_file, lua_file = create_stress_test_files(num_ops)
        result = benchmark_file_test(
            f'Stress Test {num_ops} ops',
            orus_file,
            lua_file,
            iterations,
            f'Chain of {num_ops} operations'
        )
        if result:
            results.append(result)
        
        # Clean up temporary files
        try:
            os.remove(orus_file)
            os.remove(lua_file)
        except:
            pass
    
    return results

def print_summary(results):
    """Print benchmark summary"""
    if not results:
        print_colored("❌ No results to summarize", Colors.RED)
        return
    
    print_header("📊 COMPREHENSIVE BENCHMARK SUMMARY")
    
    total_tests = len(results)
    orus_wins = sum(1 for r in results if r['orus_wins'])
    lua_wins = total_tests - orus_wins
    
    speedups = [r['speedup'] for r in results if r['speedup'] > 0]
    overall_speedup = sum(speedups) / len(speedups) if speedups else 0
    
    print_result(f"Total Tests:     {total_tests}")
    print_result(f"Orus Wins:       {orus_wins}")
    print_result(f"Lua Wins:        {lua_wins}")
    print_result(f"Overall Speedup: {overall_speedup:.2f}x")
    
    if orus_wins > lua_wins:
        print_winner(f"🏆 OVERALL WINNER: Orus is {overall_speedup:.2f}x FASTER!")
    else:
        print_colored(f"🥈 OVERALL WINNER: Lua is {1/overall_speedup:.2f}x faster", Colors.YELLOW)
    
    # Performance analysis
    if overall_speedup >= 2.0:
        print_colored("⭐ Performance Analysis:", Colors.CYAN)
        print_colored("   ⭐ EXCELLENT: Orus significantly outperforms Lua!", Colors.GREEN)
    elif overall_speedup >= 1.5:
        print_colored("⭐ Performance Analysis:", Colors.CYAN)
        print_colored("   ✅ GOOD: Orus outperforms Lua!", Colors.GREEN)
    elif overall_speedup >= 1.0:
        print_colored("⭐ Performance Analysis:", Colors.CYAN)
        print_colored("   🟡 CLOSE: Orus slightly outperforms Lua", Colors.YELLOW)
    else:
        print_colored("⭐ Performance Analysis:", Colors.CYAN)
        print_colored("   🟡 ROOM FOR IMPROVEMENT: Lua outperforms Orus", Colors.YELLOW)
    
    # Individual results
    print_colored("\n📋 Individual Test Results:", Colors.CYAN)
    for result in results:
        if result['orus_wins']:
            status = "🟢"
        else:
            status = "🟡"
        print_colored(f"   {status} {result['name']}: {result['speedup']:.2f}x", Colors.CYAN)

def save_results(results, filename):
    """Save results to JSON file"""
    if not results:
        return
    
    output = {
        'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
        'total_tests': len(results),
        'orus_wins': sum(1 for r in results if r['orus_wins']),
        'lua_wins': sum(1 for r in results if not r['orus_wins']),
        'overall_speedup': sum(r['speedup'] for r in results) / len(results),
        'results': results
    }
    
    with open(filename, 'w') as f:
        json.dump(output, f, indent=2)
    
    print_colored(f"\n💾 Detailed results saved to: {filename}", Colors.GREEN)

def main():
    parser = argparse.ArgumentParser(description='Benchmark Orus vs Lua performance')
    parser.add_argument('--iterations', type=int, default=30, help='Number of iterations per test')
    parser.add_argument('--output', type=str, default='results.json', help='Output file for results')
    
    args = parser.parse_args()
    
    results = run_benchmark_suite(args.iterations)
    
    if results:
        print_summary(results)
        save_results(results, args.output)
        print_colored("\n🎉 Benchmark completed successfully!", Colors.GREEN)
        print_colored(f"📁 Check {args.output} for detailed results", Colors.BLUE)
    else:
        print_colored("❌ Benchmark failed", Colors.RED)
        sys.exit(1)

if __name__ == '__main__':
    main()
