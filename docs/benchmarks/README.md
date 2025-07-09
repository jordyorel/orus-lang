# Orus Benchmarks Directory

This directory contains the historical benchmark tracking system for the Orus programming language.

## 📁 Directory Structure

```
benchmarks/
├── README.md                    # This file
├── HISTORICAL_INDEX.md          # Index of all historical benchmark runs
├── archive_benchmark.sh         # Script to create historical records
└── historical/                  # Historical benchmark results
    ├── 2025-07-09_v0.2.2_tail_call_optimization.md
    └── [future benchmark records...]
```

## 🎯 Purpose

This system provides:
- **Historical tracking** of benchmark performance over time
- **Clean separation** between latest and historical results
- **Automated archiving** of benchmark results
- **Performance regression detection** capabilities
- **Development insights** correlating performance with code changes

## 📋 Usage

### View Latest Results
Latest benchmark results are in the main file:
- [`../BENCHMARK_RESULTS.md`](../BENCHMARK_RESULTS.md)

### View Historical Results
1. **Index**: [`HISTORICAL_INDEX.md`](HISTORICAL_INDEX.md) - Complete list of all runs
2. **Details**: [`historical/`](historical/) - Detailed results by date and version

### Archive Current Results
Use the archive script to create a new historical record:

```bash
# Basic usage
./archive_benchmark.sh feature_name

# Run benchmarks and archive
./archive_benchmark.sh -r feature_name

# Specify custom date
./archive_benchmark.sh -d 2025-07-09 feature_name

# Get help
./archive_benchmark.sh --help
```

### Example Usage
```bash
# After implementing tail call optimization
./archive_benchmark.sh -r tail_call_optimization

# After adding new VM opcodes
./archive_benchmark.sh new_vm_opcodes

# For a specific date
./archive_benchmark.sh -d 2025-07-09 performance_improvements
```

## 🔧 File Naming Convention

Historical benchmark files use the format:
```
YYYY-MM-DD_vX.X.X_feature_name.md
```

Examples:
- `2025-07-09_v0.2.2_tail_call_optimization.md`
- `2025-07-15_v0.2.3_new_vm_opcodes.md`
- `2025-07-20_v0.3.0_type_system_improvements.md`

## 📊 Historical File Content

Each historical benchmark file contains:
- **Test configuration** (date, version, platform, features)
- **Complete benchmark results** (all categories and languages)
- **Performance analysis** (key achievements, trends)
- **Technical details** (implementation notes, optimizations)
- **Environment information** (system specs, methodology)

## 🚀 Benefits

### For Developers
- **Track performance over time** - see how optimizations affect performance
- **Identify regressions** - quickly spot performance decreases
- **Understand feature impact** - correlate changes with performance
- **Historical context** - understand performance evolution

### For Documentation
- **Clean main file** - BENCHMARK_RESULTS.md focuses on latest results
- **Complete records** - no historical data is lost
- **Easy navigation** - indexed and searchable
- **Professional presentation** - organized and comprehensive

### For Project Management
- **Performance milestones** - track achievement of performance goals
- **Release documentation** - benchmark results for each version
- **Regression reports** - data for performance analysis
- **Trend analysis** - long-term performance patterns

## 🔄 Workflow

### Regular Development
1. Continue using main `BENCHMARK_RESULTS.md` for latest results
2. Historical system works in background

### Major Releases/Features
1. Run benchmarks: `./archive_benchmark.sh -r feature_name`
2. Edit the generated historical file with actual results
3. Update main `BENCHMARK_RESULTS.md` with latest results
4. Commit all changes

### Performance Analysis
1. Check `HISTORICAL_INDEX.md` for overview
2. Compare specific versions using historical files
3. Analyze trends and patterns
4. Identify optimization opportunities

## 🛠️ Maintenance

### Adding New Entries
1. Use the `archive_benchmark.sh` script
2. Edit the generated template with actual results
3. Update `HISTORICAL_INDEX.md` with correct performance data
4. Commit changes

### Updating the System
- Modify `archive_benchmark.sh` for new features
- Update templates in the script as needed
- Enhance `HISTORICAL_INDEX.md` with new analysis

---

**Note**: This system is designed to grow with the project, maintaining comprehensive performance history while keeping the main benchmark results file clean and focused.