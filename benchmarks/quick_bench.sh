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
echo "5) Stress test"
echo "6) All benchmarks (comprehensive)"
echo ""

read -p "Enter choice (1-6): " choice

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
        echo "🔄 Running stress test..."
        ./simple_benchmark.py --stress --iterations 100
        ;;
    6)
        echo "🔄 Running comprehensive benchmark suite..."
        ./simple_benchmark.py --iterations 50 --stress
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
