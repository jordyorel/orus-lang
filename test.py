import time

# Record start time
start_time = time.time()

# Compute the sum from 1 to 100
total = 0
for i in range(1, 101):
    total += i

# Record end time
end_time = time.time()

# Print results
print("Sum from 1 to 100:", total)
print("Time taken:", end_time - start_time, "seconds")
