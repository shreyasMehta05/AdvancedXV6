import matplotlib.pyplot as plt
import pandas as pd

# Read the data from the file
with open('batterysaver.txt', 'r') as file:
    lines = file.readlines()

# Process the data
data = []
for line in lines:
    if line.startswith('ticks:'):
        parts = line.split()
        ticks = int(parts[1])
        pid = int(parts[3])
        priority = int(parts[5])
        data.append({'ticks': ticks, 'pid': pid, 'priority': priority})

# Create a DataFrame
df = pd.DataFrame(data)

# Create the plot
plt.figure(figsize=(12, 6))

# Plot each process
for pid in df['pid'].unique():
    process_data = df[df['pid'] == pid]
    plt.scatter(process_data['ticks'], process_data['priority'], label=f'P{pid}', s=10)
    plt.plot(process_data['ticks'], process_data['priority'], alpha=0.5)

plt.xlabel('Time (ticks)')
plt.ylabel('Queue ID (Priority)')
plt.title('Process Timeline Graph')
plt.legend()
plt.grid(True, which='both', linestyle='--', linewidth=0.5)

# Set y-axis ticks and labels
plt.yticks([0, 1, 2, 3], ['0', '1', '2', '3'])

# Show the plot
plt.tight_layout()
plt.savefig("mlfq_cpu1_batterysaver.png")