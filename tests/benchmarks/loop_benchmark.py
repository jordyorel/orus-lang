import time

start_time = time.time()
x = 0
for i in range(1000000):
    x += 1
end_time = time.time()

print(x)
print(f"Execution time: {end_time - start_time} seconds")
