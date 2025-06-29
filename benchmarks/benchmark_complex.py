import subprocess, time, statistics, json

ORUS_CMD = ['./orus', 'complex_expression.orus']
PYTHON_CMD = ['python3', '-c', "print((1+2+3+4+5)*(6+7+8+9+10)-(11+12+13+14+15)+16*17-18//3+19%5)"]


def bench(cmd, iterations=20):
    times = []
    for _ in range(iterations):
        t0 = time.perf_counter()
        subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        t1 = time.perf_counter()
        times.append(t1 - t0)
    return {
        'avg': statistics.mean(times),
        'min': min(times),
        'max': max(times)
    }

if __name__ == '__main__':
    iterations = 20
    result = {
        'expression': 'complex_arithmetic',
        'iterations': iterations,
        'orus': bench(ORUS_CMD, iterations),
        'python': bench(PYTHON_CMD, iterations)
    }
    print(json.dumps(result, indent=2))
