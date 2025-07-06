#!/bin/bash

# CI Environment Optimization Script for Linux
# Optimizes the system for more consistent performance measurements in CI

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}🔧 Optimizing CI environment for performance testing...${NC}"

# Check if running on Linux
if [[ "$(uname)" != "Linux" ]]; then
    echo -e "${YELLOW}⚠️  This script is optimized for Linux environments${NC}"
    exit 0
fi

# Function to run command with error handling
run_optimization() {
    local description="$1"
    local command="$2"
    
    echo -n "  $description... "
    
    if eval "$command" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${YELLOW}⚠️${NC}"
    fi
}

# System information
echo -e "${BLUE}📊 System Information:${NC}"
echo "  Kernel: $(uname -r)"
echo "  CPU: $(nproc) cores"
echo "  Memory: $(free -h | awk '/^Mem:/ {print $2}')"
echo "  Architecture: $(uname -m)"
echo ""

# CPU optimizations
echo -e "${BLUE}⚡ CPU Optimizations:${NC}"

# Set CPU governor to performance mode (if available)
run_optimization "Setting CPU governor to performance" \
    'for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [[ -w "$cpu" ]] && echo performance | sudo tee "$cpu"
     done'

# Disable CPU frequency scaling (if available)
run_optimization "Disabling CPU frequency scaling" \
    'for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq; do
        [[ -r "$cpu" ]] && cat "$cpu" | sudo tee "${cpu/max/min}"
     done'

# Set CPU affinity for current process
run_optimization "Setting CPU affinity" \
    'taskset -cp 0 $$'

# Memory optimizations
echo -e "${BLUE}💾 Memory Optimizations:${NC}"

# Clear filesystem caches
run_optimization "Clearing filesystem caches" \
    'sync && echo 3 | sudo tee /proc/sys/vm/drop_caches'

# Disable swap (if enabled)
run_optimization "Disabling swap" \
    'sudo swapoff -a'

# Set memory compaction
run_optimization "Optimizing memory compaction" \
    'echo 1 | sudo tee /proc/sys/vm/compact_memory'

# Process optimizations
echo -e "${BLUE}🚀 Process Optimizations:${NC}"

# Set higher process priority
run_optimization "Setting high process priority" \
    'renice -n -10 $$'

# Set real-time scheduling (if available)
run_optimization "Setting real-time scheduling" \
    'chrt -r 10 $$'

# Disable unnecessary services (CI-specific)
echo -e "${BLUE}🛑 Service Optimizations:${NC}"

# Stop unnecessary daemons (common in CI)
for service in snapd snapd.socket systemd-timesyncd cron; do
    run_optimization "Stopping $service" \
        "sudo systemctl stop $service"
done

# Network optimizations
echo -e "${BLUE}🌐 Network Optimizations:${NC}"

# Disable network interfaces that aren't needed
run_optimization "Optimizing network stack" \
    'echo 0 | sudo tee /proc/sys/net/core/netdev_max_backlog'

# I/O optimizations
echo -e "${BLUE}💿 I/O Optimizations:${NC}"

# Set I/O scheduler to deadline or noop for better performance
run_optimization "Setting I/O scheduler" \
    'for disk in /sys/block/*/queue/scheduler; do
        [[ -w "$disk" ]] && echo deadline | sudo tee "$disk"
     done'

# Increase I/O queue depth
run_optimization "Optimizing I/O queue depth" \
    'for disk in /sys/block/*/queue/nr_requests; do
        [[ -w "$disk" ]] && echo 128 | sudo tee "$disk"
     done'

# Timing optimizations
echo -e "${BLUE}⏱️  Timing Optimizations:${NC}"

# Increase timer frequency
run_optimization "Optimizing timer frequency" \
    'echo 1000 | sudo tee /proc/sys/kernel/timer_migration'

# Disable address space layout randomization for consistent performance
run_optimization "Disabling ASLR" \
    'echo 0 | sudo tee /proc/sys/kernel/randomize_va_space'

# Environment variables for better performance
echo -e "${BLUE}🌟 Environment Setup:${NC}"

export MALLOC_TRIM_THRESHOLD_=100000
export MALLOC_TOP_PAD_=100000
export MALLOC_MMAP_THRESHOLD_=100000
export OMP_NUM_THREADS=$(nproc)

echo "  ✓ Set memory allocation optimizations"
echo "  ✓ Set OpenMP thread count to $(nproc)"

# Create performance-optimized environment file
cat > /tmp/performance_env.sh << 'EOF'
#!/bin/bash
# Performance-optimized environment for Orus benchmarks

# Memory allocation optimizations
export MALLOC_TRIM_THRESHOLD_=100000
export MALLOC_TOP_PAD_=100000
export MALLOC_MMAP_THRESHOLD_=100000

# Parallel processing optimizations
export OMP_NUM_THREADS=$(nproc)
export MKL_NUM_THREADS=$(nproc)

# Disable debug features that might affect performance
export ORUS_DEBUG=0
export ORUS_TRACE=0

# Force consistent locale
export LC_ALL=C

echo "Performance environment loaded"
EOF

chmod +x /tmp/performance_env.sh
echo "  ✓ Created performance environment file: /tmp/performance_env.sh"

# Final system state
echo ""
echo -e "${CYAN}📋 Optimization Summary:${NC}"
echo "  CPU cores: $(nproc)"
echo "  CPU governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo 'N/A')"
echo "  Free memory: $(free -h | awk '/^Mem:/ {print $7}')"
echo "  Process priority: $(ps -o ni= -p $$)"
echo "  I/O scheduler: $(cat /sys/block/*/queue/scheduler 2>/dev/null | head -1 | grep -o '\[.*\]' || echo 'N/A')"
echo ""

echo -e "${GREEN}✅ CI environment optimization complete!${NC}"
echo -e "${CYAN}💡 Source the environment file before running benchmarks:${NC}"
echo "   source /tmp/performance_env.sh"
echo ""
echo -e "${YELLOW}⚠️  Note: Some optimizations may require sudo privileges${NC}"
echo -e "${YELLOW}   Run with: sudo ./optimize_for_ci.sh${NC}"