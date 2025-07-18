// Simple Loop Optimization Benchmark - Java Version with JIT Optimizations

public class SimpleLoopBenchmarkOptimized {
    public static void main(String[] args) {
        System.out.println("=== Simple Loop Optimization Benchmark ===");
        
        // Test 1: Small loop manually unrolled for JIT optimization
        System.out.println("Test 1: Small loop unrolling");
        for (int outer = 1; outer <= 1000; outer++) {
            // Manually unroll small loop (1,2,3,4) for JIT performance
            int x = 1 * 2; int y = x + 1; int z = y * 3;
            x = 2 * 2; y = x + 1; z = y * 3;
            x = 3 * 2; y = x + 1; z = y * 3;
            x = 4 * 2; y = x + 1; z = y * 3;
        }
        System.out.println("Small loop test completed");

        // Test 2: Medium loop kept normal (too large to unroll)
        System.out.println("Test 2: Medium loop (not unrolled)");
        for (int outer = 1; outer <= 1000; outer++) {
            for (int i = 1; i <= 15; i++) {
                int x2 = i * 2;
                int y2 = x2 + 1;
                int z2 = y2 * 3;
            }
        }
        System.out.println("Medium loop test completed");

        // Test 3: Single iteration loop optimized
        System.out.println("Test 3: Single iteration loop");
        for (int outer = 1; outer <= 1000; outer++) {
            // Single iteration - fully inlined
            int x3 = 5 * 2;
            int y3 = x3 + 1;
            int z3 = y3 * 3;
        }
        System.out.println("Single iteration test completed");

        // Test 4: Step loop manually unrolled (0,2,4)
        System.out.println("Test 4: Step loop");
        for (int outer = 1; outer <= 1000; outer++) {
            // Manually unroll step loop for JIT performance
            int x4 = 0 * 2; int y4 = x4 + 1; int z4 = y4 * 3;
            x4 = 2 * 2; y4 = x4 + 1; z4 = y4 * 3;
            x4 = 4 * 2; y4 = x4 + 1; z4 = y4 * 3;
        }
        System.out.println("Step loop test completed");

        // Test 5: Two iteration loop manually unrolled (1,2,3)
        System.out.println("Test 5: Two iteration loop");
        for (int outer = 1; outer <= 1000; outer++) {
            // Manually unroll two iteration loop
            int x5 = 1 * 2; int y5 = x5 + 1; int z5 = y5 * 3;
            x5 = 2 * 2; y5 = x5 + 1; z5 = y5 * 3;
            x5 = 3 * 2; y5 = x5 + 1; z5 = y5 * 3;
        }
        System.out.println("Two iteration test completed");

        System.out.println("=== Simple Loop Benchmark Complete ===");
    }
}