#!/bin/bash

# Orus Benchmark Suite - Quick Reference
# Shows all available benchmark commands

echo "🚀 Orus Language Benchmark Suite"
echo "================================="
echo ""
echo "📁 Current directory: $(pwd)"
echo "🔧 Orus binary: $(realpath ../orus 2>/dev/null || echo 'NOT FOUND')"
echo ""

echo "🎯 Quick Start Commands:"
echo ""
echo "1. Interactive Benchmark Menu:"
echo "   ./quick_bench.sh"
echo ""
echo "2. Run All Benchmarks (Shell Script):"
echo "   ./run_benchmarks.sh all"
echo ""
echo "3. Run All Benchmarks (Python Script):"
echo "   ./advanced_benchmark.py --all"
echo ""
echo "4. Compare Orus vs Python:"
echo "   ./advanced_benchmark.py --comparison"
echo ""
echo "5. Quick Performance Test:"
echo "   ./advanced_benchmark.py --test-files complex_expression.orus --iterations 5"
echo ""

echo "⚙️  Advanced Options:"
echo ""
echo "• Custom iterations:"
echo "  ./advanced_benchmark.py --iterations 50 --all"
echo ""
echo "• Stress testing:"
echo "  ./advanced_benchmark.py --stress-test"
echo ""
echo "• Specific test files:"
echo "  ./run_benchmarks.sh single test_150_ops_fixed.orus"
echo ""
echo "• Custom output directory:"
echo "  ./advanced_benchmark.py --output-dir ./my_results --all"
echo ""

echo "📊 Available Test Files:"
for file in *.orus; do
    if [[ -f "$file" ]]; then
        size=$(wc -c < "$file" 2>/dev/null || echo "0")
        ops=$(grep -o '[+\-*/]' "$file" 2>/dev/null | wc -l || echo "0")
        echo "  • $file (${size} bytes, ~${ops} operations)"
    fi
done
echo ""

echo "🔍 Validation & Help:"
echo ""
echo "• Validate setup:"
echo "  ./validate_setup.sh"
echo ""
echo "• Show detailed help:"
echo "  ./run_benchmarks.sh --help"
echo "  ./advanced_benchmark.py --help"
echo ""
echo "• Read documentation:"
echo "  cat README.md"
echo ""

echo "📈 Recent Results:"
if [[ -d results && -n "$(ls -A results 2>/dev/null)" ]]; then
    echo "  Results directory: ./results/"
    ls -lt results/ | head -5 | tail -n +2 | while read -r line; do
        echo "  • $line"
    done
else
    echo "  No results yet - run a benchmark to generate results!"
fi
echo ""

echo "💡 Pro Tips:"
echo "• Start with './quick_bench.sh' for an easy interactive experience"
echo "• Use higher iterations (50-100) for more accurate results"
echo "• Check results/ directory for detailed reports after benchmarks"
echo "• Run './validate_setup.sh' if you encounter any issues"
