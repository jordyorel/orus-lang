#!/bin/bash

# Archive Benchmark Results Script
# Creates a historical benchmark record and updates the main benchmark file

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HISTORICAL_DIR="$SCRIPT_DIR/historical"
MAIN_BENCHMARK_FILE="$SCRIPT_DIR/../BENCHMARK_RESULTS.md"
HISTORICAL_INDEX="$SCRIPT_DIR/HISTORICAL_INDEX.md"
ORUS_BINARY="$SCRIPT_DIR/../../orus"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Get current date and version
CURRENT_DATE=$(date +%Y-%m-%d)
if [[ -f "$ORUS_BINARY" ]]; then
    VERSION_INFO=$($ORUS_BINARY --version 2>/dev/null || echo "Unknown")
    VERSION=$(echo "$VERSION_INFO" | head -1 | grep -o "v[0-9]\+\.[0-9]\+\.[0-9]\+" || echo "vX.X.X")
else
    VERSION="vX.X.X"
    VERSION_INFO="Unknown"
fi

# Get git commit hash
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
else
    COMMIT_HASH="unknown"
fi

# Function to display usage
show_usage() {
    echo "Usage: $0 [OPTIONS] <feature_name>"
    echo ""
    echo "Archive current benchmark results with historical tracking"
    echo ""
    echo "Arguments:"
    echo "  feature_name    Brief description of main feature (use underscores)"
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -v, --version  Show version information"
    echo "  -d, --date     Specify date (YYYY-MM-DD format, default: today)"
    echo "  -r, --run      Run benchmarks before archiving"
    echo ""
    echo "Examples:"
    echo "  $0 tail_call_optimization"
    echo "  $0 -r new_vm_opcodes"
    echo "  $0 -d 2025-07-09 performance_improvements"
}

# Parse command line arguments
RUN_BENCHMARKS=false
CUSTOM_DATE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -v|--version)
            echo "Archive Benchmark Script v1.0"
            exit 0
            ;;
        -d|--date)
            CUSTOM_DATE="$2"
            shift 2
            ;;
        -r|--run)
            RUN_BENCHMARKS=true
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
        *)
            FEATURE_NAME="$1"
            shift
            ;;
    esac
done

# Validate arguments
if [[ -z "$FEATURE_NAME" ]]; then
    echo -e "${RED}Error: Feature name is required${NC}"
    show_usage
    exit 1
fi

# Use custom date if provided
if [[ -n "$CUSTOM_DATE" ]]; then
    if [[ ! "$CUSTOM_DATE" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
        echo -e "${RED}Error: Date must be in YYYY-MM-DD format${NC}"
        exit 1
    fi
    CURRENT_DATE="$CUSTOM_DATE"
fi

# Create filename
HISTORICAL_FILE="$HISTORICAL_DIR/${CURRENT_DATE}_${VERSION}_${FEATURE_NAME}.md"

echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}              Orus Benchmark Archiving Tool${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""

echo -e "${CYAN}📋 Archive Configuration:${NC}"
echo "  Date:           $CURRENT_DATE"
echo "  Version:        $VERSION"
echo "  Feature:        $FEATURE_NAME"
echo "  Commit:         $COMMIT_HASH"
echo "  Output File:    $(basename "$HISTORICAL_FILE")"
echo ""

# Run benchmarks if requested
if [[ "$RUN_BENCHMARKS" == true ]]; then
    echo -e "${CYAN}🏃 Running benchmarks...${NC}"
    if [[ -f "$SCRIPT_DIR/../../tests/benchmarks/unified_benchmark.sh" ]]; then
        cd "$SCRIPT_DIR/../../tests/benchmarks"
        ./unified_benchmark.sh > /tmp/benchmark_output.txt 2>&1
        echo -e "${GREEN}✅ Benchmarks completed${NC}"
    else
        echo -e "${YELLOW}⚠️  Benchmark script not found, skipping...${NC}"
    fi
fi

# Create historical benchmark file
echo -e "${CYAN}📝 Creating historical benchmark record...${NC}"

cat > "$HISTORICAL_FILE" << EOF
# Benchmark Results - $(date -j -f "%Y-%m-%d" "$CURRENT_DATE" +"%B %d, %Y" 2>/dev/null || echo "$CURRENT_DATE")
## Orus $VERSION - $(echo "$FEATURE_NAME" | sed 's/_/ /g' | sed 's/\b\w/\u&/g')

### 🎯 Test Configuration
- **Date**: $CURRENT_DATE
- **Orus Version**: $VERSION
- **Platform**: $(uname -s) $(uname -m)
- **Dispatch Mode**: Computed Goto
- **Key Features**: $(echo "$FEATURE_NAME" | sed 's/_/ /g' | sed 's/\b\w/\u&/g')
- **Benchmark Iterations**: 5 runs with warmup
- **Commit Hash**: $COMMIT_HASH

### 📊 Performance Results

#### Arithmetic Operations Benchmark
| Language | Time (ms) | Relative Performance | Status |
|----------|----------|---------------------|---------|
| 🥇 **Orus** | XXms | 1.0x (fastest) | ✅ |
| 🥈 **Lua** | XXms | X.XXx slower | ✅ |
| 🥉 **JavaScript** | XXms | X.XXx slower | ✅ |
| 🔸 **Python** | XXms | X.XXx slower | ✅ |

#### Control Flow Benchmark
| Language | Time (ms) | Relative Performance | Status |
|----------|----------|---------------------|---------|
| 🥇 **Orus** | XXms | 1.0x (fastest) | ✅ |
| 🥈 **Lua** | XXms | X.XXx slower | ✅ |
| 🥉 **JavaScript** | XXms | X.XXx slower | ✅ |
| 🔸 **Python** | XXms | X.XXx slower | ✅ |

#### Function Calls Benchmark
| Language | Time (ms) | Relative Performance | Status |
|----------|----------|---------------------|---------|
| 🥇 **Language** | XXms | 1.0x (fastest) | ✅ |
| 🥈 **Language** | XXms | X.XXx slower | ✅ |
| 🥉 **Language** | XXms | X.XXx slower | ✅ |
| 🔸 **Language** | XXms | X.XXx slower | ✅ |

### 🏆 Overall Performance Ranking
| Rank | Language | Average Time | Classification | Architecture |
|------|----------|-------------|---------------|--------------|
| 🥇 | **Orus** | XXms | Excellent | Register-based VM with Computed Goto |
| 🥈 | **Lua** | XXms | Excellent | Mature scripting language |
| 🥉 | **JavaScript** | XXms | Excellent | V8 JIT compilation |
| 4th | **Python** | XXms | Good | Interpreted language |

### 🚀 Key Achievements
- **TODO**: Update with actual achievements
- **TODO**: Add performance highlights
- **TODO**: Note any significant improvements

### 🔧 Technical Implementation
- **TODO**: Describe key technical changes
- **TODO**: Note implementation details
- **TODO**: Highlight optimization strategies

### 📈 Performance Trends
- **TODO**: Compare with previous versions
- **TODO**: Identify performance patterns
- **TODO**: Note any regressions or improvements

### 🎯 Benchmark Environment
- **System**: $(uname -s) $(uname -r)
- **Methodology**: 5-run average with 2 warmup runs
- **Fairness**: Equivalent algorithms across all languages
- **Tools**: Unified benchmark script with high-precision timing

---

**Summary**: TODO - Update with summary of this benchmark run and its significance.
EOF

echo -e "${GREEN}✅ Historical benchmark file created: $HISTORICAL_FILE${NC}"

# Update historical index
echo -e "${CYAN}📋 Updating historical index...${NC}"

# Create a temporary file for the new entry
NEW_ENTRY="| $CURRENT_DATE | $VERSION | $(echo "$FEATURE_NAME" | sed 's/_/ /g' | sed 's/\b\w/\u&/g') | 🥇 XXms (TODO) | [Details](historical/$(basename "$HISTORICAL_FILE")) |"

# Check if the index file exists and update it
if [[ -f "$HISTORICAL_INDEX" ]]; then
    # Insert the new entry after the header row
    sed -i '' "/^| Date | Version | Key Features | Overall Performance | Link |$/a\\
$NEW_ENTRY" "$HISTORICAL_INDEX"
    echo -e "${GREEN}✅ Historical index updated${NC}"
else
    echo -e "${YELLOW}⚠️  Historical index not found${NC}"
fi

echo ""
echo -e "${GREEN}🎉 Benchmark archiving complete!${NC}"
echo ""
echo -e "${CYAN}📝 Next steps:${NC}"
echo "  1. Edit $HISTORICAL_FILE to add actual benchmark results"
echo "  2. Update the main BENCHMARK_RESULTS.md with latest results"
echo "  3. Update the historical index entry with actual performance"
echo "  4. Commit the changes to git"
echo ""
echo -e "${CYAN}💡 Pro tip:${NC} Use the -r flag to run benchmarks automatically before archiving"
EOF