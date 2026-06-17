#!/usr/bin/env python3
import argparse
import bisect
import re
import subprocess
from collections import defaultdict


TRACE_RE = re.compile(r"(\w+)=([^ ]+)")


def parse_int(value):
    if value is None or value in ("", "(nil)", "NULL"):
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def load_symbols(nm, elf):
    output = subprocess.check_output([nm, "-n", elf], text=True, errors="replace")
    symbols = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            symbols.append((int(parts[0], 16), parts[2]))
    symbols.sort()
    return symbols


def nearest_symbol(symbols, addr):
    if addr is None or not symbols:
        return "?"
    values = [item[0] for item in symbols]
    index = bisect.bisect_right(values, addr) - 1
    if index < 0:
        return "?"
    sym_addr, name = symbols[index]
    return f"{name}+0x{addr - sym_addr:x}"


def normalize_addr(addr, load_base):
    if addr is None:
        return None
    if addr >= load_base:
        return addr - load_base
    return addr


def addr2line(addr2line_bin, elf, addr):
    if addr is None:
        return "?"
    text = subprocess.check_output(
        [addr2line_bin, "-f", "-C", "-e", elf, f"0x{addr:x}"], text=True, errors="replace"
    ).splitlines()
    if len(text) >= 2:
        return f"{text[0]} at {text[1]}"
    return "?"


def parse_log(path):
    events = []
    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "queue_trace" not in line:
                continue
            item = dict(TRACE_RE.findall(line))
            item["raw"] = line.rstrip()
            events.append(item)
    return events


def summarize_queue(queue, events, symbols, args):
    queue_addr = normalize_addr(parse_int(queue), args.load_base)
    created = [event for event in events if event.get("op") == "osCreateMesgQueue"]
    sends = [event for event in events if event.get("op") in ("osSendMesg", "osJamMesg")]
    recvs = [event for event in events if event.get("op") == "osRecvMesg"]
    waits = [event for event in events if "still waiting" in event["raw"]]

    print(f"\nq={queue}")
    print(f"  queue_symbol: {nearest_symbol(symbols, queue_addr)}")
    if created:
        event = created[-1]
        create_ra = normalize_addr(parse_int(event.get("current_ra")), args.load_base)
        print(
            "  created: "
            f"seq={event.get('create_seq')} msg={event.get('msg')} count={event.get('flag')} "
            f"thread={event.get('thread')} ra={event.get('current_ra')}"
        )
        print(f"           {nearest_symbol(symbols, create_ra)}")
        print(f"           {addr2line(args.addr2line, args.elf, create_ra)}")
    else:
        create_ra = None
        for event in events:
            create_ra = normalize_addr(parse_int(event.get("create_ra")), args.load_base)
            if create_ra is not None:
                break
        print("  created: not present in log")
        if create_ra is not None:
            print(f"           create_ra={event.get('create_ra')} {nearest_symbol(symbols, create_ra)}")
            print(f"           {addr2line(args.addr2line, args.elf, create_ra)}")

    print(f"  sends: {len(sends)}")
    for event in sends[-args.tail :]:
        current_ra = normalize_addr(parse_int(event.get("current_ra")), args.load_base)
        print(
            f"    {event.get('op')} seq={event.get('seq')} thread={event.get('thread')} "
            f"result={event.get('result')} blocks={event.get('blocks')} wakes={event.get('wakes')} "
            f"ra={event.get('current_ra')} {nearest_symbol(symbols, current_ra)}"
        )

    print(f"  receives: {len(recvs)}")
    for event in recvs[-args.tail :]:
        current_ra = normalize_addr(parse_int(event.get("current_ra")), args.load_base)
        print(
            f"    recv seq={event.get('seq')} thread={event.get('thread')} result={event.get('result')} "
            f"blocks={event.get('blocks')} wakes={event.get('wakes')} ra={event.get('current_ra')} "
            f"{nearest_symbol(symbols, current_ra)}"
        )

    print(f"  still_waiting: {len(waits)}")
    for event in waits[-args.tail :]:
        current_ra = normalize_addr(parse_int(event.get("current_ra")), args.load_base)
        print(
            f"    wait seq={event.get('seq')} op={event.get('op')} thread={event.get('thread')} "
            f"waited_ms={event.get('waited_ms')} ra={event.get('current_ra')} {nearest_symbol(symbols, current_ra)}"
        )


def main():
    parser = argparse.ArgumentParser(description="Resolve PSP queue_trace return addresses from sf64_psp.log")
    parser.add_argument("--log", default="sf64_psp.log")
    parser.add_argument("--elf", default="build/psp/starfox64.psp.elf")
    parser.add_argument("--queue", action="append", default=[])
    parser.add_argument("--load-base", default=0x08804000, type=lambda value: int(value, 0))
    parser.add_argument("--addr2line", default="psp-addr2line")
    parser.add_argument("--nm", default="psp-nm")
    parser.add_argument("--tail", default=8, type=int)
    args = parser.parse_args()

    events = parse_log(args.log)
    symbols = load_symbols(args.nm, args.elf)
    by_queue = defaultdict(list)
    for event in events:
        queue = event.get("q")
        if queue:
            by_queue[queue.lower()].append(event)

    queues = [queue.lower() for queue in args.queue]
    if not queues:
        queues = sorted(by_queue)

    print(f"resolved {len(events)} queue_trace events from {args.log}")
    print(f"elf={args.elf} load_base=0x{args.load_base:x}")
    for queue in queues:
        summarize_queue(queue, by_queue.get(queue, []), symbols, args)


if __name__ == "__main__":
    main()
