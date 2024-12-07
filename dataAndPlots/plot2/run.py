import matplotlib.pyplot as plt
import numpy as np

def parse_log_file(filename):
    data = []
    with open(filename, 'r') as f:
        for line in f:
            time, pid, queue = map(int, line.strip().split())
            data.append((time, pid, queue))
    return data

def create_mlfq_graph(data):
    processes = set(pid for _, pid, _ in data)
    colors = plt.cm.rainbow(np.linspace(0, 1, len(processes)))
    color_map = dict(zip(processes, colors))

    fig, ax = plt.subplots(figsize=(12, 6))

    for pid in processes:
        process_data = [(time, queue) for time, p, queue in data if p == pid]
        times, queues = zip(*process_data)
        ax.scatter(times, queues, c=[color_map[pid]], label=f'Process {pid}', s=20)
        ax.plot(times, queues, c=color_map[pid], alpha=0.5)

    ax.set_xlabel('Time')
    ax.set_ylabel('Queue ID')
    ax.set_yticks(range(4))  # Assuming 4 queues (0-3)
    ax.set_title('MLFQ Scheduler: Process Queue Timeline for 1 CPU')
    ax.legend(loc='center left', bbox_to_anchor=(1, 0.5))
    ax.grid(True, which='both', linestyle='--', alpha=0.7)

    plt.tight_layout()
    plt.savefig('mlfq_timeline_for_1_CPU_one_Cpu1.png')
    plt.close()

# Usage
log_file = 'priority_100.txt'
data = parse_log_file(log_file)
create_mlfq_graph(data)
print("Graph saved as 'mlfq_timeline_for_1_CPU_one_Cpu1.png'")