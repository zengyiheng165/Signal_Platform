#!/usr/bin/env python3
"""irq_config_check.py — 多中断配置静态检查（无第三方依赖）

用法:
    python irq_config_check.py --src <目录或文件...> [--json]

检查项:
    1) 每个 XScuGic_Connect 的 ID 是否有对应的 XScuGic_Enable
    2) 每个 XScuGic_Enable 的 ID 是否有对应的 Connect
    3) 每个 Connect 的 ID 是否设置了 SetPriorityTriggerType（漏设 ⇒ 警告）
    4) 是否存在 Xil_ExceptionRegisterHandler 与 Xil_ExceptionEnable
    5) 同一个 XAxiDma_IntrDisable 参数是否重复出现（提示级）

退出码: 0 = 无 ERROR, 1 = 存在 ERROR
"""
import argparse
import json
import os
import re
import sys

SRC_EXT = {".c", ".h"}
CONNECT_RE = re.compile(r"XScuGic_Connect\s*\(\s*\w+\s*,\s*([\w]+)")
ENABLE_RE = re.compile(r"XScuGic_Enable\s*\(\s*\w+\s*,\s*([\w]+)")
PRIO_RE = re.compile(r"SetPriorityTriggerType\s*\(\s*\w+\s*,\s*([\w]+)")
DMA_DIS_RE = re.compile(r"XAxiDma_IntrDisable\s*\(([^;]*)\)")


def iter_files(paths):
    for p in paths:
        if os.path.isfile(p):
            if os.path.splitext(p)[1].lower() in SRC_EXT:
                yield p
        elif os.path.isdir(p):
            for root, _d, files in os.walk(p):
                for f in files:
                    if os.path.splitext(f)[1].lower() in SRC_EXT:
                        yield os.path.join(root, f)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", nargs="+", required=True)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    files = list(iter_files(a.src))
    if not files:
        print("没有找到 .c/.h 文件: %s" % a.src)
        return 1

    connects, enables, prios = {}, {}, {}
    has_exc_reg = has_exc_en = False
    errors, warns, infos = [], [], []

    for path in files:
        text = read(path)
        for m in CONNECT_RE.finditer(text):
            connects.setdefault(m.group(1), []).append(path)
        for m in ENABLE_RE.finditer(text):
            enables.setdefault(m.group(1), []).append(path)
        for m in PRIO_RE.finditer(text):
            prios.setdefault(m.group(1), []).append(path)
        if "Xil_ExceptionRegisterHandler" in text:
            has_exc_reg = True
        if "Xil_ExceptionEnable" in text:
            has_exc_en = True
        dis = [re.sub(r"\s+", "", m.group(1)) for m in DMA_DIS_RE.finditer(text)]
        for arg in set(dis):
            if dis.count(arg) > 1:
                infos.append("重复的 XAxiDma_IntrDisable(%s) 出现 %d 次: %s" % (arg, dis.count(arg), path))

    for irq, where in connects.items():
        if irq not in enables:
            errors.append("已 Connect 但从未 XScuGic_Enable: %s  (%s)" % (irq, ", ".join(where)))
        if irq not in prios:
            warns.append("已 Connect 但未设置 SetPriorityTriggerType: %s  (%s)" % (irq, ", ".join(where)))
    for irq, where in enables.items():
        if irq not in connects:
            errors.append("已 XScuGic_Enable 但从未 Connect: %s  (%s)" % (irq, ", ".join(where)))
    if connects and not has_exc_reg:
        errors.append("找不到 Xil_ExceptionRegisterHandler: 中断无法进入 CPU")
    if connects and not has_exc_en:
        errors.append("找不到 Xil_ExceptionEnable: 中断无法进入 CPU")

    result = {
        "files_scanned": len(files),
        "connected_irqs": sorted(connects),
        "enabled_irqs": sorted(enables),
        "priority_set_irqs": sorted(prios),
        "errors": errors,
        "warnings": warns,
        "infos": infos,
        "verdict": "FAIL" if errors else ("WARN" if warns else "PASS"),
    }

    if a.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
    else:
        print("扫描文件: %d" % result["files_scanned"])
        print("Connect : %s" % ", ".join(result["connected_irqs"]))
        print("Enable  : %s" % ", ".join(result["enabled_irqs"]))
        print("已设触发: %s" % ", ".join(result["priority_set_irqs"]))
        for e in errors:
            print("  [ERROR] %s" % e)
        for w in warns:
            print("  [WARN ] %s" % w)
        for i in infos:
            print("  [INFO ] %s" % i)
        print("结论: %s" % result["verdict"])

    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())