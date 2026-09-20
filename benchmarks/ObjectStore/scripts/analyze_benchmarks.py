#!/usr/bin/env python3

#============================================================================
#
# Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
#
# This file is part of the extora which can be found at
# https://github.com/IvanPinezhaninov/extora/.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
# THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

import json
import sys


OBJECT_SIZES = ("4KiB", "64KiB", "1MiB", "64MiB", "256MiB", "1GiB")
EXTENT_SIZES = (
    "256KiB",
    "512KiB",
    "1MiB",
    "2MiB",
    "4MiB",
    "8MiB",
    "16MiB",
    "32MiB",
    "64MiB",
    "128MiB",
)
CONCURRENT_OBJECT_SIZES = (
    ("4KiB", 4 * 1024),
    ("64KiB", 64 * 1024),
    ("1MiB", 1024 * 1024),
    ("64MiB", 64 * 1024 * 1024),
)
THREAD_COUNTS = (1, 2, 4, 8, 16)
TIME_UNIT_SECONDS = {
    "ns": 1e-9,
    "us": 1e-6,
    "ms": 1e-3,
    "s": 1.0,
}


def load_json(path):
    with open(path, "r", encoding="utf-8") as file:
        return json.load(file)


def logical_name(benchmark):
    run_name = benchmark.get("run_name", benchmark.get("name", ""))
    parts = []
    for part in run_name.split("/"):
        if part.startswith(("min_time:", "max_time:", "iterations:", "repetitions:", "threads:")):
            break
        if part == "real_time":
            break
        parts.append(part)
    return "/".join(parts)


def result_priority(benchmark):
    if benchmark.get("run_type") == "aggregate":
        return 2 if benchmark.get("aggregate_name") == "median" else 0
    return 1 if benchmark.get("run_type") == "iteration" else 0


def collect_results(document):
    results = {}
    priorities = {}

    for benchmark in document.get("benchmarks", []):
        priority = result_priority(benchmark)
        if priority == 0:
            continue

        name = logical_name(benchmark)
        threads = int(benchmark.get("threads", 1))
        key = (name, threads)
        if priorities.get(key, -1) > priority:
            continue

        unit = benchmark.get("time_unit", "ns")
        results[key] = {
            "time": float(benchmark.get("real_time", 0.0)) * TIME_UNIT_SECONDS.get(unit, 1.0),
            "bytes_per_second": float(benchmark.get("bytes_per_second", 0.0)),
            "items_per_second": float(benchmark.get("items_per_second", 0.0)),
        }
        priorities[key] = priority

    return results


def get_result(results, name, threads=1):
    return results.get((name, threads))


def format_duration(seconds):
    if seconds >= 1.0:
        return f"{seconds:.3f} s"
    if seconds >= 1e-3:
        return f"{seconds * 1e3:.3f} ms"
    if seconds >= 1e-6:
        return f"{seconds * 1e6:.3f} us"
    return f"{seconds * 1e9:.3f} ns"


def format_bytes_per_second(value):
    if value == 0.0:
        return "-"

    gib = 1024.0 ** 3
    mib = 1024.0 ** 2
    kib = 1024.0
    if value >= gib:
        return f"{value / gib:.2f} GiB/s"
    if value >= mib:
        return f"{value / mib:.2f} MiB/s"
    if value >= kib:
        return f"{value / kib:.2f} KiB/s"
    return f"{value:.0f} B/s"


def format_rate(value, unit="ops"):
    if value == 0.0:
        return "-"
    if value >= 1e6:
        return f"{value / 1e6:.2f} M {unit}/s"
    if value >= 1e3:
        return f"{value / 1e3:.2f} k {unit}/s"
    return f"{value:.1f} {unit}/s"


def format_change(value, baseline):
    if baseline == 0.0:
        return "-"
    return f"{(value / baseline - 1.0) * 100.0:+.1f}%"


def format_scaling(value, baseline):
    if baseline == 0.0:
        return "-"
    return f"{value / baseline:.2f}x"


def print_table(headers, rows):
    if not rows:
        return

    widths = [len(header) for header in headers]
    for row in rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))

    def print_row(row):
        values = [row[0].ljust(widths[0])]
        values.extend(row[index].rjust(widths[index]) for index in range(1, len(row)))
        print("  " + "  ".join(values))

    print_row(headers)
    print("  " + "  ".join("-" * width for width in widths))
    for row in rows:
        print_row(row)


def print_section(title, headers, rows):
    if not rows:
        return
    print(title)
    print_table(headers, rows)
    print()


def read_rows(results):
    rows = []
    for size in OBJECT_SIZES:
        cached = get_result(results, f"Read/Object/Cached/{size}")
        uncached = get_result(results, f"Read/Object/Uncached/{size}")
        plain = get_result(results, f"Read/PlainFile/{size}")
        if cached is None:
            continue

        rows.append((
            size,
            format_duration(cached["time"]),
            format_bytes_per_second(cached["bytes_per_second"]),
            format_duration(uncached["time"]) if uncached else "-",
            format_duration(plain["time"]) if plain else "-",
            format_change(cached["time"], plain["time"]) if plain else "-",
        ))
    return rows


def range_read_rows(results):
    result = get_result(results, "Read/Object/Range4KiB/64MiB")
    if result is None:
        return []
    return [(
        "4KiB from 64MiB",
        format_duration(result["time"]),
        format_bytes_per_second(result["bytes_per_second"]),
    )]


def large_read_rows(results):
    cached = get_result(results, "Read/Object/Cached/1GiB")
    scenarios = (
        ("Cached", cached),
        ("Checksum verification", get_result(results, "Read/Object/Cached/VerifyChecksum/1GiB")),
        ("Evicted page cache", get_result(results, "Read/Object/Evicted/1GiB")),
        ("Plain file, evicted", get_result(results, "Read/PlainFile/Evicted/1GiB")),
    )
    rows = []
    for title, result in scenarios:
        if result is None:
            continue
        rows.append((
            title,
            format_duration(result["time"]),
            format_bytes_per_second(result["bytes_per_second"]),
            format_change(result["time"], cached["time"]) if cached else "-",
        ))
    return rows


def write_rows(results):
    modes = (
        ("Strict", "Write/PlainFile/Synced"),
        ("Balanced", "Write/PlainFile/Synced"),
        ("Relaxed", "Write/PlainFile/Unsynced"),
    )
    rows = []
    for size in OBJECT_SIZES:
        for mode, baseline_name in modes:
            result = get_result(results, f"Write/Object/{mode}/KnownLength/{size}")
            if result is None:
                continue

            baseline = get_result(results, f"{baseline_name}/{size}") if baseline_name else None
            rows.append((
                size,
                mode,
                format_duration(result["time"]),
                format_bytes_per_second(result["bytes_per_second"]),
                format_change(result["time"], baseline["time"]) if baseline else "-",
            ))
    return rows


def content_length_rows(results):
    rows = []
    for size in OBJECT_SIZES:
        changes = []
        found = False
        for mode in ("Strict", "Balanced", "Relaxed"):
            known = get_result(results, f"Write/Object/{mode}/KnownLength/{size}")
            unknown = get_result(results, f"Write/Object/{mode}/UnknownLength/{size}")
            if known is None or unknown is None:
                changes.append("-")
                continue
            found = True
            changes.append(format_change(unknown["time"], known["time"]))
        if found:
            rows.append((size, *changes))
    return rows


def extent_rows(results):
    rows = []
    for extent_size in EXTENT_SIZES:
        balanced_known = get_result(
            results,
            f"Write/Object/Balanced/KnownLength/Extent{extent_size}/64MiB",
        )
        balanced_unknown = get_result(
            results,
            f"Write/Object/Balanced/UnknownLength/Extent{extent_size}/64MiB",
        )
        relaxed_known = get_result(
            results,
            f"Write/Object/Relaxed/KnownLength/Extent{extent_size}/64MiB",
        )
        relaxed_unknown = get_result(
            results,
            f"Write/Object/Relaxed/UnknownLength/Extent{extent_size}/64MiB",
        )
        read = get_result(
            results,
            f"Read/Object/Cached/Extent{extent_size}/64MiB",
        )
        if all(result is None for result in (
            balanced_known,
            balanced_unknown,
            relaxed_known,
            relaxed_unknown,
            read,
        )):
            continue
        rows.append((
            extent_size,
            format_bytes_per_second(balanced_known["bytes_per_second"]) if balanced_known else "-",
            format_bytes_per_second(balanced_unknown["bytes_per_second"]) if balanced_unknown else "-",
            format_bytes_per_second(relaxed_known["bytes_per_second"]) if relaxed_known else "-",
            format_bytes_per_second(relaxed_unknown["bytes_per_second"]) if relaxed_unknown else "-",
            format_bytes_per_second(read["bytes_per_second"]) if read else "-",
        ))
    return rows


def metadata_rows(results):
    scenarios = (
        ("HEAD cached", "Metadata/Head/Cached", "ops"),
        ("HEAD uncached", "Metadata/Head/Uncached", "ops"),
        ("List 256 objects", "Metadata/List/256Objects", "items"),
    )
    rows = []
    for title, name, unit in scenarios:
        result = get_result(results, name)
        if result is None:
            continue
        rows.append((
            title,
            format_duration(result["time"]),
            format_rate(result["items_per_second"], unit),
        ))
    return rows


def concurrent_metadata_table(results):
    scenarios = (
        ("Cached HEAD", "Concurrent/Metadata/Head/Cached"),
        ("Uncached HEAD", "Concurrent/Metadata/Head/Uncached"),
    )
    available = [
        scenario for scenario in scenarios
        if any(get_result(results, scenario[1], threads) is not None for threads in THREAD_COUNTS)
    ]

    headers = ["Threads"]
    for title, _ in available:
        headers.extend((title, f"{title} scaling"))

    rows = []
    for threads in THREAD_COUNTS:
        values = []
        found = False
        for _, name in available:
            result = get_result(results, name, threads)
            if result is None:
                values.extend(("-", "-"))
                continue

            found = True
            baseline = get_result(results, name, 1)
            values.extend((
                format_rate(result["items_per_second"]),
                format_scaling(
                    result["items_per_second"],
                    baseline["items_per_second"],
                ) if baseline else "-",
            ))

        if not found:
            continue
        rows.append((str(threads), *values))

    return tuple(headers), rows


def concurrent_object_rows(results, name, object_size):
    baseline = get_result(results, name, 1)
    rows = []
    for threads in THREAD_COUNTS:
        result = get_result(results, name, threads)
        if result is None:
            continue

        throughput = result["bytes_per_second"]
        rows.append((
            str(threads),
            format_rate(throughput / object_size, "objects"),
            format_bytes_per_second(throughput),
            format_scaling(throughput, baseline["bytes_per_second"]) if baseline else "-",
        ))
    return rows


def print_context(document):
    context = document.get("context", {})
    values = []
    if context.get("host_name"):
        values.append(f"host {context['host_name']}")
    if context.get("num_cpus"):
        values.append(f"{context['num_cpus']} CPUs")
    if context.get("library_build_type"):
        values.append(f"{context['library_build_type']} build")
    if values:
        print("  " + ", ".join(values))
        print()


def main():
    if len(sys.argv) != 2:
        print("usage: analyze_benchmarks.py <google-benchmark-json>", file=sys.stderr)
        return 1

    document = load_json(sys.argv[1])
    results = collect_results(document)

    print()
    print("Extora benchmark summary")
    print_context(document)

    print_section(
        "Sequential reads",
        ("Size", "Cached", "Throughput", "Uncached", "Plain file", "Latency vs plain"),
        read_rows(results),
    )
    print_section(
        "Range reads",
        ("Range", "Latency", "Throughput"),
        range_read_rows(results),
    )
    print_section(
        "1GiB read modes",
        ("Mode", "Latency", "Throughput", "Latency vs cached"),
        large_read_rows(results),
    )
    print_section(
        "Object writes with known content length",
        ("Size", "Durability", "Latency", "Throughput", "Latency vs plain"),
        write_rows(results),
    )
    print_section(
        "Unknown content length latency change",
        ("Size", "Strict", "Balanced", "Relaxed"),
        content_length_rows(results),
    )
    print_section(
        "64MiB throughput by extent size",
        ("Extent", "Balanced known", "Balanced unknown", "Relaxed known", "Relaxed unknown", "Cached read"),
        extent_rows(results),
    )
    print_section(
        "Metadata",
        ("Operation", "Latency", "Rate"),
        metadata_rows(results),
    )
    concurrent_headers, concurrent_results = concurrent_metadata_table(results)
    print_section("Concurrent metadata", concurrent_headers, concurrent_results)
    for size, object_size in CONCURRENT_OBJECT_SIZES:
        print_section(
            f"Concurrent cached reads ({size})",
            ("Threads", "Object rate", "Throughput", "Scaling"),
            concurrent_object_rows(
                results,
                f"Concurrent/Read/Object/Cached/{size}",
                object_size,
            ),
        )
    for size, object_size in CONCURRENT_OBJECT_SIZES:
        print_section(
            f"Concurrent balanced writes ({size})",
            ("Threads", "Object rate", "Throughput", "Scaling"),
            concurrent_object_rows(
                results,
                f"Concurrent/Write/Object/Balanced/{size}",
                object_size,
            ),
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
