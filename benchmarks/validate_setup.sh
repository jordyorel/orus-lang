#!/bin/bash

# Benchmark Suite Validation Script
# Tests all benchmark scripts to ensure they work correctly

cd "$(dirname "$0")"

echo "🔍 Validating Orus Benchmark Suite"
echo "=================================="
echo ""

errors=0

# Check if Orus binary exists
if [[ ! -f "../orus" ]]; then
    echo "❌ Orus binary not found at ../orus"
    echo "   Please run 'make' in the project root first"
    errors=$((errors + 1))
else
    echo "✅ Orus binary found"
fi

# Check script permissions
scripts=("run_benchmarks.sh" "advanced_benchmark.py" "quick_bench.sh")
for script in "${scripts[@]}"; do
    if [[ -x "$script" ]]; then
        echo "✅ $script is executable"
    else
        echo "❌ $script is not executable"
        chmod +x "$script" 2>/dev/null && echo "   → Fixed permissions" || errors=$((errors + 1))
    fi
done

# Check Python availability and modules
if command -v python3 &> /dev/null; then
    echo "✅ Python3 is available"
    
    # Check required modules
    required_modules=("json" "time" "statistics" "subprocess" "platform" "pathlib" "dataclasses" "tempfile")
    for module in "${required_modules[@]}"; do
        if python3 -c "import $module" 2>/dev/null; then
            echo "✅ Python module '$module' available"
        else
            echo "❌ Python module '$module' missing"
            errors=$((errors + 1))
        fi
    done
else
    echo "❌ Python3 not found"
    errors=$((errors + 1))
fi

# Check test files
test_files=("complex_expression.orus" "test_150_ops_fixed.orus" "test_500_ops_fixed.orus")
for file in "${test_files[@]}"; do
    if [[ -f "$file" ]]; then
        echo "✅ Test file '$file' found"
    else
        echo "❌ Test file '$file' missing"
        errors=$((errors + 1))
    fi
done

# Test basic functionality (if Orus binary exists)
if [[ -f "../orus" && $errors -eq 0 ]]; then
    echo ""
    echo "🧪 Running basic functionality test..."
    
    # Create a simple test
    echo "1 + 2 + 3" > test_validation.orus
    
    if ../orus test_validation.orus >/dev/null 2>&1; then
        echo "✅ Basic Orus execution works"
    else
        echo "❌ Basic Orus execution failed"
        errors=$((errors + 1))
    fi
    
    # Clean up
    rm -f test_validation.orus
    
    # Test benchmark script (quick test)
    echo "🧪 Testing benchmark script with minimal run..."
    if ./advanced_benchmark.py --test-files complex_expression.orus --iterations 2 --output-dir ./test_output >/dev/null 2>&1; then
        echo "✅ Benchmark script basic functionality works"
        rm -rf ./test_output
    else
        echo "❌ Benchmark script test failed"
        errors=$((errors + 1))
    fi
fi

echo ""
echo "🔍 Validation Summary"
echo "===================="

if [[ $errors -eq 0 ]]; then
    echo "✅ All checks passed! Benchmark suite is ready to use."
    echo ""
    echo "Quick start commands:"
    echo "  ./quick_bench.sh                    # Interactive menu"
    echo "  ./run_benchmarks.sh all             # Full benchmark suite"
    echo "  ./advanced_benchmark.py --comparison # Compare with Python"
    echo ""
    echo "📚 See README.md for detailed usage instructions"
else
    echo "❌ $errors issues found. Please resolve them before running benchmarks."
    echo ""
    echo "Common solutions:"
    echo "  - Run 'make' in the project root to build Orus"
    echo "  - Ensure Python 3.6+ is installed"
    echo "  - Check file permissions with 'ls -la *.sh *.py'"
fi

exit $errors
