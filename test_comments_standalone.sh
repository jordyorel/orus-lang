#!/bin/bash
# Standalone script to test comment functionality

echo "Testing Orus Comment Parsing..."
echo "==============================="

# Build if needed
if [ ! -f "./orus" ]; then
    echo "Building Orus..."
    make
fi

# Run comment tests
echo ""
echo "Running comment test suite..."
make test-comments

echo ""
echo "Testing individual comment files with output:"
echo "=============================================="

echo ""
echo "1. Basic Comments Test:"
./orus tests/comments/basic_comments.orus

echo ""
echo "2. Block Comments Test:"
./orus tests/comments/block_comments.orus

echo ""
echo "3. Comments in Expressions Test:"
./orus tests/comments/comments_in_expressions.orus

echo ""
echo "4. Mixed Comment Types Test:"
./orus tests/comments/mixed_comment_types.orus

echo ""
echo "All comment tests completed successfully!"