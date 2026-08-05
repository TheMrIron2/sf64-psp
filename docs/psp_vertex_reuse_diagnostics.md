# Finding 3 vertex-reuse results

Finding 3 Stage A, direct construction in the PSPGL stream, is accepted. The
follow-up reuse work is concluded and its temporary diagnostics and A/B build
selectors have been removed.

## Reuse diagnostic

The profiling-only diagnostic compared exact final 24-byte vertex packets
within each renderer batch. Hash matches were verified with `memcmp`; every
invariant passed and neither capture overflowed its table.

| Scene | Direct duplicates | Hybrid U16 savings | Winning direct batches |
| --- | ---: | ---: | ---: |
| Title | 60.6% | 52.4% | 16,207 / 18,900 |
| Mid-Corneria | 31.8% | 24.1% | 14,395 / 22,492 |

A source-load identity then tested whether repeats could be detected before
packet construction:

| Scene | Direct identity hit rate | Duplicate coverage | Mismatches |
| --- | ---: | ---: | ---: |
| Title | 58.6% | 96.6% | 0 |
| Mid-Corneria | 29.4% | 83.7% | 0 |

The identity was sound, but byte savings alone did not justify indexed draws.
The July 2005 PSP graphics seminar shows materially higher transfer cost for
24-byte indexed geometry and recommends compact non-indexed data in eDRAM. This
port's earlier indexed implementation also regressed. Stage B therefore needed
a convincing matched counter win, not just reuse potential.

## Non-indexed follow-ups

Both follow-ups used layout-identical 300-frame title pairs with thread hardware
counters. Work differed by less than 0.003% in each pair.

The first cached converted UVs by source identity and exact UV state:

| Metric | Candidate change |
| --- | ---: |
| Submitted work | +0.0018% |
| Task elapsed time | +4.51% |
| Task CPU cycles | +4.57% |
| Batch elapsed time | +16.78% |
| Batch CPU cycles | +17.26% |
| Batch FPU instructions | -28.24% |
| Batch memory stalls | +29.22% |
| Batch D-cache misses | +88.08% |

The arithmetic saving was real, but the roughly 67 KiB cache cost more than it
saved in the highest-reuse scene.

The second hoisted invariant UV conversion constants once per triangle command:

| Metric | Candidate change |
| --- | ---: |
| Submitted work | -0.0023% |
| Task elapsed time | +1.97% |
| Task CPU cycles | +2.13% |
| Batch elapsed time | +6.99% |
| Batch CPU cycles | +7.67% |
| Batch FPU instructions | -7.85% |
| Batch memory stalls | +56.09% |
| Batch D-cache misses | +69.14% |
| Batch D-cache writebacks | +201.74% |

Six stored constants across 328,800 triangle commands predicted about 1.97
million extra cached stores; the measured increase was 1,970,705. Writebacks
and memory stalls outweighed the removed conversions.

## Conclusion

- Keep accepted Stage A direct streaming and its cached-staging fallback
- Reject the UV cache and per-command UV-constant hoist
- Retire the fused VFPU Stage B and do not pursue indexed reuse
- Prefer candidates that remove data traffic, cache misses, stalls or draw calls

No Finding 3 hardware validation remains outstanding.
