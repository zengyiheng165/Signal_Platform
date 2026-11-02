#!/usr/bin/env python3
"""dma_len_check.py — AXI DMA 长度/对齐/缓冲/超时预算自检（无第三方依赖）

用法:
    python dma_len_check.py --points <N> --bytes-per-point <B> --max-points <MAX>
    python dma_len_check.py --points <N> --bytes-per-point <B> --dma-len <LEN>
    python dma_len_check.py --points <N> --bytes-per-point <B> --fs <fs> --mhz <MHz> --json

判据:
    期望长度 = 点数 x 每点字节数
    --dma-len 与期望长度不等  =>  MISMATCH（S2MM 将永不完成，且没有错误码）
退出码: 0 通过, 1 不通过
"""
import argparse
import json
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--points", type=int, required=True, help="本次点数")
    ap.add_argument("--bytes-per-point", type=int, default=4, help="每点字节数，默认 4")
    ap.add_argument("--dma-len", type=int, default=None, help="代码中传给 SimpleTransfer 的长度")
    ap.add_argument("--max-points", type=int, default=None, help="缓冲可容纳的最大点数")
    ap.add_argument("--cache-line", type=int, default=32, help="缓存行字节数，默认 32")
    ap.add_argument("--fs", type=float, default=0.0, help="采样率 Hz（可选，用于算传输时间下限）")
    ap.add_argument("--mhz", type=float, default=0.0, help="CPU 主频 MHz（可选，用于估超时预算）")
    ap.add_argument("--channels", type=int, default=1, help="同时搬运的通道数，默认 1")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    a = ap.parse_args()

    expected = a.points * a.bytes_per_point
    aligned = ((expected + a.cache_line - 1) // a.cache_line) * a.cache_line
    problems = []

    if a.max_points is not None and a.points > a.max_points:
        problems.append("点数 %d 超过缓冲上限 %d" % (a.points, a.max_points))
    if expected == 0:
        problems.append("期望长度为 0")
    if a.dma_len is not None and a.dma_len != expected:
        problems.append("长度不匹配: 代码=%d, 期望=%d (%+d)" % (a.dma_len, expected, a.dma_len - expected))
    if a.dma_len is not None and a.dma_len % a.cache_line != 0:
            problems.append("传输长度 %d 不是缓存行(%d)的整数倍，按字节 Flush 的范围可能不足" % (a.dma_len, a.cache_line))

    transfer_ms = (a.points / a.fs * 1000.0) if a.fs > 0 else None
    out = {
        "points": a.points,
        "bytes_per_point": a.bytes_per_point,
        "expected_len": expected,
        "expected_len_aligned": aligned,
        "code_len": a.dma_len,
        "channels": a.channels,
        "total_buffers_bytes": aligned * a.channels,
        "transfer_min_ms": transfer_ms,
        "verdict": "PASS" if not problems else "FAIL",
        "problems": problems,
    }

    if a.mhz > 0 and transfer_ms is not None:
        cycles = transfer_ms / 1000.0 * a.mhz * 1e6
        out["transfer_cycles_est"] = int(cycles)
        out["poll_budget_suggestion"] = int(cycles * 4) if cycles > 0 else 0

    if a.json:
        print(json.dumps(out, ensure_ascii=False, indent=1))
    else:
        print("点数            : %d" % a.points)
        print("每点字节        : %d" % a.bytes_per_point)
        print("期望长度        : %d 字节 (对齐后 %d)" % (expected, aligned))
        print("代码里的长度    : %s" % (a.dma_len if a.dma_len is not None else "(未提供)"))
        print("通道数          : %d  缓冲合计 %d 字节" % (a.channels, aligned * a.channels))
        if transfer_ms is not None:
            print("传输时间下限    : %.3f ms" % transfer_ms)
        if "poll_budget_suggestion" in out:
            print("轮询预算建议    : %d 次" % out["poll_budget_suggestion"])
        print("结论            : %s" % out["verdict"])
        for p in problems:
            print("  - %s" % p)
        if problems:
            print("提示: 长度不匹配时 S2MM 永远到不了 IOC, done 标志不会置位, 也没有错误码。")

    sys.exit(0 if not problems else 1)


if __name__ == "__main__":
    main()
