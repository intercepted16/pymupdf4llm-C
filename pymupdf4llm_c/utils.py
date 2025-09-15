"""Utility functions and classes for performance profiling and output suppression."""

# ruff: noqa: T201 -- allow print statements for profiling output

import time
from collections import defaultdict
from contextlib import contextmanager
from typing import Any, Callable, Dict, List


@contextmanager
def suppress_output():
    """Context manager to suppress stdout and stderr."""
    yield


# Performance profiler
class PerformanceProfiler:
    """Class to profile performance of functions and code blocks."""

    def __init__(self) -> None:
        """Initialize the profiler."""
        self.timings: Dict[str, List[float]] = defaultdict(list)
        self.start_times: Dict[str, float] = {}

    def start_timer(self, name: str) -> None:
        """Start timing a named event."""
        self.start_times[name] = time.perf_counter()

    def end_timer(self, name: str) -> float:
        """End timing a named event and record the elapsed time."""
        if name in self.start_times:
            elapsed = time.perf_counter() - self.start_times[name]
            self.timings[name].append(elapsed)
            del self.start_times[name]
            return elapsed
        return 0.0

    def get_stats(self) -> Dict[str, Dict[str, float]]:
        """Get aggregated statistics for all timed events."""
        stats: Dict[str, Dict[str, float]] = {}
        for name, times in self.timings.items():
            stats[name] = {
                "total": sum(times),
                "average": sum(times) / len(times),
                "count": len(times),
                "min": min(times),
                "max": max(times),
            }
        return stats

    def print_report(self) -> None:
        """Print a performance report to the console."""
        print("\n" + "=" * 60)
        print("PERFORMANCE REPORT")
        print("=" * 60)
        stats = self.get_stats()

        # Sort by total time descending
        sorted_stats = sorted(stats.items(), key=lambda x: x[1]["total"], reverse=True)

        print(
            f"{'Function':<25} {'Total (s)':<10} {'Avg (s)':<10} {'Count':<8} {'Min (s)':<10} {'Max (s)':<10}"
        )
        print("-" * 75)

        for name, data in sorted_stats:
            print(
                f"{name:<25} {data['total']:<10.4f} {data['average']:<10.4f} {data['count']:<8} {data['min']:<10.4f} {data['max']:<10.4f}"
            )

        print("=" * 60)

    # convenience context manager for timing blocks
    from contextlib import contextmanager

    @contextmanager
    def time_block(self, name: str):
        """Context manager to time a block and automatically record it."""
        try:
            self.start_timer(name)
            yield
        finally:
            elapsed = self.end_timer(name)
            print(f"[perf] {name}: {elapsed:.6f}s")

    def timeit(self, name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
        """Decorator to time a function and record timings under `name`."""

        def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
            def wrapper(*a: Any, **k: Any) -> Any:
                self.start_timer(name)
                try:
                    return fn(*a, **k)
                finally:
                    elapsed = self.end_timer(name)
                    # short console hint
                    print(f"[perf] {name} -> {fn.__name__}: {elapsed:.6f}s")

            return wrapper

        return decorator


# Global profiler instance
profiler = PerformanceProfiler()
