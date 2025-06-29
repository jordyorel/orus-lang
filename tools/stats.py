#!/usr/bin/env python3
"""
Orus Language Code Statistics Tool
Analyzes the codebase and provides statistics about the project.
"""

import os
import sys
from pathlib import Path
from collections import defaultdict

def count_lines(file_path):
    """Count lines in a file."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
            total = len(lines)
            blank = sum(1 for line in lines if not line.strip())
            comment = sum(1 for line in lines if line.strip().startswith('//') or line.strip().startswith('/*'))
            code = total - blank - comment
            return total, code, comment, blank
    except Exception:
        return 0, 0, 0, 0

def analyze_directory(directory, extensions):
    """Analyze all files with given extensions in directory."""
    stats = defaultdict(lambda: {'files': 0, 'total': 0, 'code': 0, 'comment': 0, 'blank': 0})
    
    # Convert to absolute path
    directory = os.path.abspath(directory)
    
    for root, dirs, files in os.walk(directory):
        # Skip build directories and hidden directories
        dirs[:] = [d for d in dirs if not d.startswith('.') and d != 'build']
        
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                file_path = os.path.join(root, file)
                ext = next(ext for ext in extensions if file.endswith(ext))
                
                total, code, comment, blank = count_lines(file_path)
                stats[ext]['files'] += 1
                stats[ext]['total'] += total
                stats[ext]['code'] += code
                stats[ext]['comment'] += comment
                stats[ext]['blank'] += blank
    
    return stats

def main():
    """Main function."""
    project_root = Path(__file__).parent.parent
    
    print("Orus Language Code Statistics")
    print("=" * 40)
    
    # Define file types to analyze
    extensions = ['.c', '.h', '.py', '.md', '.txt', '.orus']
    
    stats = analyze_directory(project_root, extensions)
    
    if not stats:
        print("No files found to analyze.")
        return
    
    # Print statistics
    total_files = 0
    total_lines = 0
    total_code = 0
    total_comments = 0
    total_blank = 0
    
    print(f"{'Type':<8} {'Files':<6} {'Total':<8} {'Code':<8} {'Comments':<10} {'Blank':<6}")
    print("-" * 50)
    
    for ext in sorted(stats.keys()):
        s = stats[ext]
        print(f"{ext:<8} {s['files']:<6} {s['total']:<8} {s['code']:<8} {s['comment']:<10} {s['blank']:<6}")
        
        total_files += s['files']
        total_lines += s['total']
        total_code += s['code']
        total_comments += s['comment']
        total_blank += s['blank']
    
    print("-" * 50)
    print(f"{'Total':<8} {total_files:<6} {total_lines:<8} {total_code:<8} {total_comments:<10} {total_blank:<6}")
    
    # Additional statistics
    if total_lines > 0:
        print("\nProject Summary:")
        print(f"  Total files: {total_files}")
        print(f"  Total lines: {total_lines}")
        print(f"  Code lines: {total_code} ({total_code/total_lines*100:.1f}%)")
        print(f"  Comment lines: {total_comments} ({total_comments/total_lines*100:.1f}%)")
        print(f"  Blank lines: {total_blank} ({total_blank/total_lines*100:.1f}%)")
        
        # Language breakdown
        if '.c' in stats or '.h' in stats:
            c_code = stats.get('.c', {}).get('code', 0)
            h_code = stats.get('.h', {}).get('code', 0)
            print(f"  C/C++ lines: {c_code + h_code}")
        
        if '.py' in stats:
            py_code = stats['.py']['code']
            print(f"  Python lines: {py_code}")
        
        if '.md' in stats:
            md_lines = stats['.md']['total']
            print(f"  Documentation lines: {md_lines}")
    else:
        print("No code found to analyze.")

if __name__ == "__main__":
    main()
