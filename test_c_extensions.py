#!/usr/bin/env python3
"""
Test script for C extensions to verify they work correctly.
"""

import sys
import time
import traceback


def test_core_extension():
    """Test the core C extension."""
    print("Testing to_md_core extension...")

    try:
        import to_md_core

        print("✓ to_md_core imported successfully")

        # Test span processing
        test_spans = [
            {
                "text": "Hello World",
                "flags": 0,
                "char_flags": 0,
                "size": 12.0,
                "bbox": [10, 20, 100, 30],
            },
            {
                "text": "Bold Text",
                "flags": 16,  # Bold flag
                "char_flags": 0,
                "size": 12.0,
                "bbox": [110, 20, 180, 30],
            },
        ]

        start_time = time.time()
        result = to_md_core.process_spans_ultra_fast(test_spans, "## ", 0.0)
        end_time = time.time()

        print(f"✓ Span processing result: {repr(result)}")
        print(f"✓ Processing time: {(end_time - start_time) * 1000:.3f}ms")

        # Test rectangle intersection
        test_rects = []  # Would need actual rectangle objects
        result = to_md_core.rect_intersects_any(10, 10, 50, 50, test_rects)
        print(f"✓ Rectangle intersection test: {result}")

        return True

    except ImportError as e:
        print(f"❌ Failed to import to_md_core: {e}")
        return False
    except Exception as e:
        print(f"❌ Error testing to_md_core: {e}")
        traceback.print_exc()
        return False


def test_main_extension():
    """Test the main C extension."""
    print("Testing to_md_c extension...")

    try:
        import to_md_c

        print("✓ to_md_c imported successfully")

        # Test basic functionality
        print(f"✓ MAX_FONT_SIZE constant: {to_md_c.MAX_FONT_SIZE}")
        print(f"✓ GRAPHICS_TEXT constant: {repr(to_md_c.GRAPHICS_TEXT)}")

        return True

    except ImportError as e:
        print(f"❌ Failed to import to_md_c: {e}")
        return False
    except Exception as e:
        print(f"❌ Error testing to_md_c: {e}")
        traceback.print_exc()
        return False


def test_hybrid_module():
    """Test the hybrid Python/C module."""
    print("Testing hybrid module...")

    try:
        import to_md_hybrid

        print("✓ to_md_hybrid imported successfully")

        # Test performance info
        perf_info = to_md_hybrid.get_performance_info()
        print(f"✓ Performance info: {perf_info}")

        # Test font analyzer
        analyzer = to_md_hybrid.HybridFontAnalyzer()
        test_spans = [(12, 100), (14, 50), (16, 25)]
        analyzer.analyze_spans(test_spans)
        analyzer.build_header_mapping()
        print(f"✓ Font analysis completed, headers: {len(analyzer.header_mapping)}")

        return True

    except ImportError as e:
        print(f"❌ Failed to import to_md_hybrid: {e}")
        return False
    except Exception as e:
        print(f"❌ Error testing to_md_hybrid: {e}")
        traceback.print_exc()
        return False


def benchmark_performance():
    """Simple performance benchmark."""
    print("\n=== Performance Benchmark ===")

    try:
        import to_md_hybrid

        # Create test data
        large_spans = []
        for i in range(1000):
            large_spans.append(
                {
                    "text": f"Text span {i} with some content",
                    "flags": i % 32,
                    "char_flags": i % 8,
                    "size": 12.0 + (i % 6),
                    "bbox": [i * 10, 20, i * 10 + 100, 35],
                }
            )

        # Benchmark span processing
        print("Benchmarking span processing...")

        start_time = time.time()
        for _ in range(100):
            result = to_md_hybrid.process_spans_optimized(
                large_spans[:10], "", None, None
            )
        end_time = time.time()

        total_time = end_time - start_time
        per_iteration = total_time / 100

        print("✓ 100 iterations of 10-span processing:")
        print(f"  Total time: {total_time:.3f}s")
        print(f"  Per iteration: {per_iteration * 1000:.3f}ms")
        print(f"  Throughput: {1000 / per_iteration:.0f} span-sets/second")

        return True

    except Exception as e:
        print(f"❌ Benchmark failed: {e}")
        traceback.print_exc()
        return False


def main():
    """Main test function."""
    print("=== C Extension Test Suite ===")
    print(f"Python version: {sys.version}")
    print()

    results = []

    # Test individual extensions
    results.append(("Core Extension", test_core_extension()))
    results.append(("Main Extension", test_main_extension()))
    results.append(("Hybrid Module", test_hybrid_module()))
    results.append(("Performance Benchmark", benchmark_performance()))

    # Summary
    print("\n=== Test Results ===")
    passed = 0
    for name, result in results:
        status = "✓ PASS" if result else "❌ FAIL"
        print(f"{name}: {status}")
        if result:
            passed += 1

    print(f"\nOverall: {passed}/{len(results)} tests passed")

    if passed == len(results):
        print("🎉 All tests passed! C extensions are working correctly.")
        return 0
    else:
        print("⚠ Some tests failed. Check the output above for details.")
        return 1


if __name__ == "__main__":
    sys.exit(main())

