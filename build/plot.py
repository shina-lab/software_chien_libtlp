import matplotlib.pyplot as plt
import numpy as np
import typing

def get_median_times(filename):
    with open(filename, 'r') as f:
        medians = []
        stderrs = []
        for line in f:
            stripped = line.strip()
            if 'med:' in stripped:
                median = stripped.replace(" ", "").replace("med:", "").replace("seconds", "")
                medians.append(float(median) * 1000)  # convert seconds to milli-seconds

            if 'stderr' in stripped:
                stderr = stripped.replace(" ", "").replace("stderr:", "").replace("seconds", "")
                stderrs.append(float(stderr) * 1000)  # convert seconds to milli-seconds
        return medians, stderrs

medians_bare, errs_bare = get_median_times('bare_new.txt')
medians_shallow, errs_shallow = get_median_times('shallow_new.txt')
medians_full, errs_full = get_median_times('full_new.txt')

# programs = ["Prog{}".format(i+1) for i in range(len(medians_bare))]
programs = ["syscall-check", "file-check", "tty-drivers", "netfilter-hijack", "tcp-op", 
            "process-list", "priv-escalation", "module-list", "keyboard-sniffer", "vfs-hook"]

x = np.arange(len(programs)) * 1.5
width = 0.4

fig, ax = plt.subplots(figsize=(12, 6))

bars1 = ax.bar(x - width, medians_bare, width, label='Bare', color='#3498db', alpha=0.8, 
                      yerr=errs_bare, capsize=5, error_kw={'linewidth': 1})
bars2 = ax.bar(x, medians_shallow, width, label='Shallow', color='#e74c3c', alpha=0.8, 
                      yerr=errs_shallow, capsize=5, error_kw={'linewidth': 1})
bars3 = ax.bar(x + width, medians_full, width, label='Full', color='#2ecc71', alpha=0.8,
                      yerr=errs_full, capsize=5, error_kw={'linewidth': 1})

def add_value_labels(bars):
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, height, '%.1f' % height, ha='center', va='bottom', fontsize=8)

add_value_labels(bars1)
add_value_labels(bars2)
add_value_labels(bars3)

ax.set_xlabel('Programs', fontsize=12, fontweight='bold')
ax.set_ylabel('Median Time (ms)', fontsize=12, fontweight='bold')
ax.set_title('Comparison of Memory Introspection Turnaround Times', fontsize=14, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(programs)
ax.legend(loc='upper left', framealpha=0.9)

plt.show()