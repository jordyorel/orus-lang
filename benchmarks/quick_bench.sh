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
        ./advanced_benchmark.py --iterations 5 --all
        ;;
    2)
        echo "🔄 Running standard benchmark..."
        ./advanced_benchmark.py --iterations 20 --all
        ;;
    3)
        echo "🔄 Running thorough benchmark..."
        ./advanced_benchmark.py --iterations 50 --all
        ;;
    4)
        echo "🔄 Running comparison benchmark..."
        ./advanced_benchmark.py --comparison
        ;;
    5)
        echo "🔄 Running stress test..."
        ./advanced_benchmark.py --stress-test
        ;;
    6)
        echo "🔄 Running comprehensive benchmark suite..."
        ./run_benchmarks.sh all
        ;;
    *)
        echo "❌ Invalid choice. Please run again."
        exit 1
        ;;
esac

echo ""
echo "✅ Benchmark completed!"
echo "📁 Results saved in: ./results/"
echo "📊 Check the generated reports for detailed analysis."
