#!/bin/bash

echo "Running Function Tests"
echo "======================"

echo
echo "Test 1: Simple function test"
echo "Expected: 30"
echo "Actual:"
./orus_debug tests/functions/simple_test.orus
echo

echo "Test 2: Original function test"
echo "Expected: Result: 8"
echo "Actual:"
./orus_debug test_function_current.orus
echo

echo
echo "Function tests completed!"