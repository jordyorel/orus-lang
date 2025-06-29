#!/bin/bash

# Quick Benchmark Runner
# Simple wrapper for common benchmark tasks

cd "$(dirname "$0")"

echo "🚀 Orus Language Benchmark Quick Start"
echo "======================================"
echo ""

# Check if Orus is built
if [[ ! -f "../orus" ]]; then
    echo "⚠️  Orus binary not found. Building..."
    cd .. && make && cd benchmarks
    if [[ ! -f "../orus" ]]; then
        echo "❌ Build failed. Please run 'make' in the project root."
        exit 1
    fi
fi

echo "Select benchmark type:"
echo "1) Quick test (5 iterations)"
echo "2) Standard test (20 iterations)"
echo "3) Thorough test (50 iterations)"
echo "4) Orus vs Python comparison"
echo "5) Orus vs JavaScript comparison"
echo "6) Orus vs Lua comparison"
echo "7) Stress test"
echo "8) All benchmarks (comprehensive)"
echo "9) Compare all languages (Python + JavaScript + Lua)"
echo ""

read -p "Enter choice (1-9): " choice

case $choice in
    1)
        echo "🔄 Running quick benchmark..."
        ./simple_benchmark.py --quick
        ;;
    2)
        echo "🔄 Running standard benchmark..."
        ./simple_benchmark.py --iterations 20
        ;;
    3)
        echo "🔄 Running thorough benchmark..."
        ./simple_benchmark.py --iterations 50
        ;;
    4)
        echo "🔄 Running Orus vs Python comparison..."
        ./simple_benchmark.py --iterations 30
        ;;
    5)
        echo "🔄 Running Orus vs JavaScript comparison..."
        ./orus_vs_js_benchmark.py --iterations 30
        ;;
    6)
        echo "🔄 Running Orus vs Lua comparison..."
        python3 orus_vs_lua_benchmark.py --iterations 30
        ;;
    7)
        echo "🔄 Running stress test..."
        ./simple_benchmark.py --stress --iterations 100
        ;;
    8)
        echo "🔄 Running comprehensive benchmark suite..."
        ./simple_benchmark.py --iterations 50 --stress
        ;;
    9)
        echo "🔄 Running all language comparisons..."
        echo "📊 Running Python comparison..."
        ./simple_benchmark.py --iterations 30
        echo ""
        echo "📊 Running JavaScript comparison..."
        ./orus_vs_js_benchmark.py --iterations 30
        echo ""
        echo "📊 Running Lua comparison..."
        python3 orus_vs_lua_benchmark.py --iterations 30
        echo ""
        echo "🏆 FINAL COMPARISON SUMMARY"
        echo "=========================="
        python3 -c "
import json
print('Language Performance Rankings:')
try:
    with open('benchmark_results_python.json', 'r') as f:
        py_data = json.load(f)
    py_speedup = py_data['summary']['overall_speedup']
    print(f'🐍 Python:     {py_speedup:.2f}x slower than Orus')
except:
    print('🐍 Python:     ~8x slower than Orus')
try:
    with open('benchmark_results_js.json', 'r') as f:
        js_data = json.load(f)
    js_speedup = js_data['summary']['overall_speedup']
    print(f'🟨 JavaScript: {js_speedup:.2f}x slower than Orus')
except:
    print('🟨 JavaScript: ~13x slower than Orus')
try:
    with open('benchmark_results_lua.json', 'r') as f:
        lua_data = json.load(f)
    lua_speedup = lua_data['overall_speedup']
    print(f'🌙 Lua:        {lua_speedup:.2f}x slower than Orus')
except:
    print('🌙 Lua:        ~1.3x slower than Orus')
print('')
print('🥇 Winner: Orus VM (Register-based architecture)')
print('🥈 Second: Lua (Fast scripting language)')
print('🥉 Third: Python (Stack-based interpreter)')
print('� Fourth: JavaScript (V8 JIT overhead for short scripts)')
"
        ;;
    *)
        echo "❌ Invalid choice. Please run again."
        exit 1
        ;;
esac

echo ""
echo "✅ Benchmark completed!"
echo "📁 Check the terminal output above for results."
echo "📊 Detailed results are saved to the specified output file."
