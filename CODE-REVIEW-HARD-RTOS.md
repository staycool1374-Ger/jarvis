Please evaluate the system based strictly on the following five core pillars of hard real-time computing:

1. Determinism (Predictability): How guaranteed is the Worst-Case Execution Time (WCET)?
2. Strict Deadline Adherence (Zero-Tolerance): Are there mechanisms to prevent catastrophic failure if a single deadline is missed?
3. Preemptive Priority-Based Scheduling: How does the scheduler handle high-priority task interruption and mitigate risks like priority inversion?
4. Minimal and Known Interrupt Latency Jitter: Is the delay between hardware signals and task execution tightly bounded and constant?
5. Deterministic Memory and Resource Management: Does the architecture completely avoid non-deterministic behaviors (e.g., dynamic heap allocation, standard garbage collection, virtual memory paging)?

Please provide your review in the following structure:
- Executive Summary: A high-level assessment of the hard real-time compliance.
- Pillar-by-Pillar Analysis: A detailed critique of each of the 5 points listed above, highlighting potential bottlenecks or architectural flaws.
- Recommendations: Concrete technical solutions, protocols, or design patterns (e.g., Priority Inheritance, static memory pools) to ensure absolute hard real-time behavior.

