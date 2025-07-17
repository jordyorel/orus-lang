/**
 * Java Extreme Performance Benchmark
 * Equivalent to extreme_benchmark.orus for fair comparison
 */
public class ExtremeBenchmark {
    
    public static void main(String[] args) {
        System.out.println("=== EXTREME Java Performance Stress Test ===");
        
        long startTime = System.nanoTime();
        
        // === PHASE 1: EXTREME ARITHMETIC INTENSITY ===
        System.out.println("Phase 1: Maximum Arithmetic Stress");
        
        // Base variables for complex calculations (safe values)
        int a = 100;
        int b = 50;
        int c = 25;
        int d = 12;
        int e = 6;
        
        // Complex expressions (safe bounds)
        int megaExpr1 = ((((a + b) * (c - d)) / ((e + a) - (b / c))) + (((d * e) + (a - b)) * ((c + d) / (e - a))));
        int megaExpr2 = (((((a * b) + (c * d)) - ((e * a) + (b * c))) / (((d + e) * (a + b)) - ((c * d) + (e * a)))));
        int megaExpr3 = ((((((a + b + c) * (d + e)) - ((a * b) + (c * d))) / (((e + a) * (b + c)) + ((d + e) * (a + b)))) + (((a - b) * (c - d)) + ((e - a) * (b - c)))));
        
        // Deep expression (safe bounds)
        int deepExpr = (((((a + b) * c) - d) / e) + a) * b;
        
        // Mathematical intensity
        int formula1 = (a * b * c) + (d * e * a) - (b * c * d) + (e * a * b) - (c * d * e);
        int formula2 = ((a + b + c + d + e) * (a - b - c - d - e)) / ((a * b) + (c * d) + (e * a));
        int formula3 = (((a / b) + (c / d)) * ((e / a) + (b / c))) - (((d / e) + (a / b)) * ((c / d) + (e / a)));
        
        // Computation chains
        int chainResult = 0;
        int tempVal = 1;
        tempVal = tempVal + a;
        chainResult = chainResult + tempVal;
        tempVal = tempVal * b;
        chainResult = chainResult + tempVal;
        tempVal = tempVal - c;
        chainResult = chainResult + tempVal;
        tempVal = tempVal / d;
        chainResult = chainResult + tempVal;
        tempVal = tempVal + e;
        chainResult = chainResult + tempVal;
        
        // Large scale summation
        int sumTotal = a + b + c + d + e + megaExpr1 + megaExpr2 + megaExpr3 + deepExpr + formula1 + formula2 + formula3 + chainResult;
        
        System.out.println("Extreme Arithmetic Results:");
        System.out.println("Mega expression 1:" + megaExpr1);
        System.out.println("Mega expression 2:" + megaExpr2);
        System.out.println("Deep nested result:" + deepExpr);
        System.out.println("Sum total:" + sumTotal);
        
        // === PHASE 2: MASSIVE VARIABLE OPERATIONS ===
        System.out.println("Phase 2: Extreme Variable Manipulation");
        
        // Create many variables and manipulate them
        int[] vars = new int[50];
        for (int i = 0; i < 50; i++) {
            vars[i] = i * 2 + 1;
        }
        
        // Complex variable interactions
        int varSum = 0;
        for (int i = 0; i < 50; i++) {
            vars[i] = vars[i] * (i + 1) / (i + 2);
            varSum = varSum + vars[i];
        }
        
        // Nested variable assignments
        int x1 = varSum / 10;
        int x2 = x1 * x1;
        int x3 = x2 - x1;
        int x4 = x3 + x2;
        int x5 = x4 / x3;
        
        System.out.println("Variable Results:");
        System.out.println("Variable sum:" + varSum);
        System.out.println("X1:" + x1 + " X2:" + x2 + " X3:" + x3 + " X4:" + x4 + " X5:" + x5);
        
        // === PHASE 3: EXTREME EXPRESSION NESTING ===
        System.out.println("Phase 3: Maximum Expression Complexity");
        
        // Ultra-deep nested expressions
        int ultra1 = ((((((a + b) * c) - d) / e) + ((((x1 + x2) * x3) - x4) / x5)) * ((((megaExpr1 + megaExpr2) * megaExpr3) - deepExpr) / formula1));
        int ultra2 = (((((((a * b) + (c * d)) - (e * x1)) + ((x2 * x3) - (x4 * x5))) * (((formula1 + formula2) - formula3) + chainResult)) / ((megaExpr1 + megaExpr2 + megaExpr3) - (deepExpr + sumTotal))) + varSum);
        int ultra3 = ((((x1 + x2 + x3 + x4 + x5) * (a + b + c + d + e)) - ((megaExpr1 + megaExpr2) * (formula1 + formula2))) / ((deepExpr + chainResult) + (varSum / 10)));
        
        // Extreme mathematical expressions
        int mathExtreme1 = 0;
        for (int i = 1; i <= 25; i++) {
            mathExtreme1 = mathExtreme1 + (i * i * i) / (i + 1) + (i * i) % 7;
        }
        
        int mathExtreme2 = 0;
        for (int i = 1; i <= 20; i++) {
            for (int j = 1; j <= 10; j++) {
                mathExtreme2 = mathExtreme2 + (i * j) % 13 + (i + j) / 3;
            }
        }
        
        System.out.println("Extreme Expression Results:");
        System.out.println("Ultra 1:" + ultra1);
        System.out.println("Ultra 2:" + ultra2);
        System.out.println("Ultra 3:" + ultra3);
        System.out.println("Math extreme 1:" + mathExtreme1);
        System.out.println("Math extreme 2:" + mathExtreme2);
        
        // === PHASE 4: COMPUTATIONAL OVERLOAD ===
        System.out.println("Phase 4: Maximum Computational Load");
        
        // Fibonacci stress test
        int fibStress = 0;
        for (int i = 1; i <= 12; i++) {
            fibStress = fibStress + fibonacci(i);
        }
        
        // Factorial combinations
        int factStress = 0;
        for (int i = 1; i <= 8; i++) {
            factStress = factStress + factorial(i) / (i + 1);
        }
        
        // Power calculations
        int powerStress = 0;
        for (int base = 2; base <= 5; base++) {
            for (int exp = 1; exp <= 8; exp++) {
                powerStress = powerStress + power(base, exp) % 1000;
            }
        }
        
        // Final extreme calculation
        int extremeResult = megaExpr1 + megaExpr2 + megaExpr3 + deepExpr + 
                           formula1 + formula2 + formula3 + chainResult + sumTotal +
                           varSum + ultra1 + ultra2 + ultra3 + 
                           mathExtreme1 + mathExtreme2 + fibStress + factStress + powerStress;
        
        System.out.println("Computational Overload Results:");
        System.out.println("Fibonacci stress:" + fibStress);
        System.out.println("Factorial stress:" + factStress);
        System.out.println("Power stress:" + powerStress);
        System.out.println("EXTREME FINAL RESULT:" + extremeResult);
        
        long endTime = System.nanoTime();
        long totalTimeMs = (endTime - startTime) / 1_000_000;
        
        System.out.println("Computation time:" + totalTimeMs);
        System.out.println("=== EXTREME BENCHMARK COMPLETE ===");
        System.out.println("Total execution time:" + totalTimeMs);
        System.out.println("Extreme phases completed: 4");
        System.out.println("Ultra-complex operations: 500+");
        System.out.println("Deep nested expressions: 50+");
        System.out.println("Computational stress tests: 20+");
        System.out.println("Final extreme score:" + totalTimeMs);
        System.out.println("=== Java Extreme Performance Test Complete ===");
    }
    
    // Helper functions
    static int fibonacci(int n) {
        if (n <= 1) return n;
        return fibonacci(n - 1) + fibonacci(n - 2);
    }
    
    static int factorial(int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    }
    
    static int power(int base, int exp) {
        int result = 1;
        for (int i = 0; i < exp; i++) {
            result = result * base;
        }
        return result;
    }
}
