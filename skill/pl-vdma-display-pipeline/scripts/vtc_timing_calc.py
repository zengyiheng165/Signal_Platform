#!/usr/bin/env python3
"""vtc_timing_calc.py — 由面板时序推导 v_tc 参数 / 从 .hwh 反推 / 两者比对

范围：只回答"v_tc 的 8 个参数是否按这块屏幕的时序配的"。

面板手册参数 -> v_tc 参数（C_GEN_*）:
    HACTIVE        = HA
    HFRAME         = HA + HFP + HSW + HBP
    HSYNC_START    = HA + HFP
    HSYNC_END      = HA + HFP + HSW
    VACTIVE        = VA
    VFRAME         = VA + VFP + VSW + VBP      (逐行)
    VSYNC_VSTART   = VA + VFP
    VSYNC_VEND     = VA + VFP + VSW
    pclk           = HFRAME x VFRAME x fps

用法:
    # 1) 从 hwh 反推（看现在配的是什么）
    python vtc_timing_calc.py --hwh system.hwh
    # 2) 由面板参数正向计算（该填什么）
    python vtc_timing_calc.py --hact <HA> --vact <VA> --hfp <HFP> --hsw <HSW> --hbp <HBP> \
                              --vfp <VFP> --vsw <VSW> --vbp <VBP> --fps <fps>
    # 3) 计算并与 hwh 比对（逐项差异）
    python vtc_timing_calc.py --hwh system.hwh --hact <HA> --vact <VA> --hfp <HFP> \
                              --hsw <HSW> --hbp <HBP> --vfp <VFP> --vsw <VSW> --vbp <VBP> --fps <fps>
退出码: 0 = 自洽/一致, 1 = 有 ERROR
"""
import argparse
import json
import re
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def read_vtc(hwh):
    t = open(hwh, encoding="utf-8", errors="replace").read()
    tags = re.findall(r"<MODULE [^>]*>", t)
    blocks = re.findall(r"(?s)<MODULE [^>]*>(.*?)</MODULE>", t)
    for tag, blk in zip(tags, blocks):
        if 'MODTYPE="v_tc"' not in tag:
            continue
        inst = re.search(r'INSTANCE="([^"]+)"', tag).group(1)
        return inst, dict(re.findall(r'<PARAMETER NAME="([^"]+)" VALUE="([^"]*)"', blk))
    return None, {}


def gi(p, k, d=0):
    try:
        return int(p.get(k, d) or d)
    except ValueError:
        return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hwh")
    for k in ("hact", "vact", "hfp", "hsw", "hbp", "vfp", "vsw", "vbp"):
        ap.add_argument("--" + k, type=int, default=None)
    ap.add_argument("--fps", type=float, default=None)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    E, W, I = [], [], []
    out = {}

    # ---------- 正向：由面板参数算 v_tc ----------
    panel = {k: getattr(a, k) for k in ("hact", "vact", "hfp", "hsw", "hbp", "vfp", "vsw", "vbp")}
    calc = None
    if all(v is not None for v in panel.values()):
        ha, va = panel["hact"], panel["vact"]
        hframe = ha + panel["hfp"] + panel["hsw"] + panel["hbp"]
        vframe = va + panel["vfp"] + panel["vsw"] + panel["vbp"]
        calc = {
            "C_GEN_HACTIVE_SIZE": ha,
            "C_GEN_HFRAME_SIZE": hframe,
            "C_GEN_HSYNC_START": ha + panel["hfp"],
            "C_GEN_HSYNC_END": ha + panel["hfp"] + panel["hsw"],
            "C_GEN_VACTIVE_SIZE": va,
            "C_GEN_F0_VFRAME_SIZE": vframe,
            "C_GEN_F0_VSYNC_VSTART": va + panel["vfp"],
            "C_GEN_F0_VSYNC_VEND": va + panel["vfp"] + panel["vsw"],
        }
        out["calculated"] = calc
        if a.fps:
            pclk = hframe * vframe * a.fps / 1e6
            out["pclk_required_mhz"] = round(pclk, 3)
            I.append("按面板时序与 %.3f fps：像素时钟需 %.3f MHz" % (a.fps, pclk))
        if panel["hsw"] < 1 or panel["vsw"] < 1:
            E.append("面板同步脉宽必须 >= 1（HSW=%d, VSW=%d）" % (panel["hsw"], panel["vsw"]))
    elif any(v is not None for v in panel.values()):
        E.append("面板时序参数必须同时给全（HA/VA/HFP/HSW/HBP/VFP/VSW/VBP）")

    # ---------- 读取 hwh ----------
    if a.hwh:
        inst, vp = read_vtc(a.hwh)
        if not inst:
            E.append("hwh 里找不到 v_tc")
        else:
            hw = {k: gi(vp, k) for k in
                  ("C_GEN_HACTIVE_SIZE", "C_GEN_VACTIVE_SIZE", "C_GEN_HFRAME_SIZE",
                   "C_GEN_F0_VFRAME_SIZE", "C_GEN_HSYNC_START", "C_GEN_HSYNC_END",
                   "C_GEN_F0_VSYNC_VSTART", "C_GEN_F0_VSYNC_VEND")}
            pol = {k: gi(vp, k) for k in
                   ("C_GEN_HSYNC_POLARITY", "C_GEN_VSYNC_POLARITY",
                    "C_GEN_HBLANK_POLARITY", "C_GEN_VBLANK_POLARITY",
                    "C_GEN_AVIDEO_POLARITY", "C_GEN_INTERLACED")}
            ha, va = hw["C_GEN_HACTIVE_SIZE"], hw["C_GEN_VACTIVE_SIZE"]
            hf, vf = hw["C_GEN_HFRAME_SIZE"], hw["C_GEN_F0_VFRAME_SIZE"]
            hs, he = hw["C_GEN_HSYNC_START"], hw["C_GEN_HSYNC_END"]
            vs, ve = hw["C_GEN_F0_VSYNC_VSTART"], hw["C_GEN_F0_VSYNC_VEND"]
            derived = {"HFP": hs - ha, "HSW": he - hs, "HBP": hf - he,
                       "VFP": vs - va, "VSW": ve - vs, "VBP": vf - ve,
                       "HFRAME": hf, "VFRAME": vf}
            out["hwh_inst"] = inst
            out["hwh_values"] = hw
            out["hwh_derived"] = derived
            out["hwh_polarity"] = pol

            # 自洽检查
            if hs < ha:
                E.append("HSYNC_START(%d) < HACTIVE(%d)：同步脉冲落在有效区内" % (hs, ha))
            if he > hf:
                E.append("HSYNC_END(%d) > HFRAME(%d)：同步脉冲超出帧" % (he, hf))
            if vs < va:
                E.append("VSYNC_VSTART(%d) < VACTIVE(%d)：同步脉冲落在有效区内" % (vs, va))
            if ve > vf:
                E.append("VSYNC_VEND(%d) > VFRAME(%d)" % (ve, vf))
            if hs >= he:
                E.append("HSYNC_START >= HSYNC_END：脉宽为 0 或负")
            if vs >= ve:
                E.append("VSYNC_VSTART >= VSYNC_VEND：脉宽为 0 或负")
            if pol.get("C_GEN_INTERLACED") == 1:
                W.append("v_tc 配置为隔行（LCD 面板通常只接受逐行）")
            for k, nm in (("C_GEN_HBLANK_POLARITY", "HBLANK"), ("C_GEN_VBLANK_POLARITY", "VBLANK"),
                          ("C_GEN_HSYNC_POLARITY", "HSYNC"), ("C_GEN_VSYNC_POLARITY", "VSYNC")):
                I.append("%s 极性 = %s（必须与面板手册一致）" % (nm, pol.get(k)))
            I.append("由 hwh 反推：HFP=%d HSW=%d HBP=%d | VFP=%d VSW=%d VBP=%d"
                     % (derived["HFP"], derived["HSW"], derived["HBP"],
                        derived["VFP"], derived["VSW"], derived["VBP"]))

            # 与面板参数比对
            if calc:
                diff = []
                for k, v in calc.items():
                    if hw.get(k) != v:
                        diff.append("%s: hwh=%s, 面板要求=%s" % (k, hw.get(k), v))
                for k, v in (("HFP", panel["hfp"]), ("HSW", panel["hsw"]), ("HBP", panel["hbp"]),
                             ("VFP", panel["vfp"]), ("VSW", panel["vsw"]), ("VBP", panel["vbp"])):
                    if derived[k] != v:
                        diff.append("%s: 反推=%d, 面板=%d" % (k, derived[k], v))
                out["diff"] = diff
                if diff:
                    E.append("v_tc 参数与本面板时序不一致（见 diff）：必须按面板改 v_tc")
                else:
                    I.append("v_tc 的 8 个参数与面板时序完全一致")

    out.update({"errors": E, "warnings": W, "infos": I,
                "verdict": "FAIL" if E else ("WARN" if W else "PASS")})
    if a.json:
        print(json.dumps(out, ensure_ascii=False, indent=1))
    else:
        if calc:
            print("== 按面板参数应填的 v_tc 参数 ==")
            for k, v in calc.items():
                print("   %-26s = %d" % (k, v))
            if "pclk_required_mhz" in out:
                print("   %-26s = %.3f MHz" % ("pclk", out["pclk_required_mhz"]))
        if "hwh_derived" in out:
            d = out["hwh_derived"]
            print("== hwh 现有值反推 ==")
            hv = out["hwh_values"]
            print("   H: HACTIVE %d + HFP %d + HSW %d + HBP %d = HFRAME %d"
                  % (hv["C_GEN_HACTIVE_SIZE"], d["HFP"], d["HSW"], d["HBP"], d["HFRAME"]))
            print("   V: VACTIVE %d + VFP %d + VSW %d + VBP %d = VFRAME %d"
                  % (hv["C_GEN_VACTIVE_SIZE"], d["VFP"], d["VSW"], d["VBP"], d["VFRAME"]))
        if "diff" in out and out["diff"]:
            print("== 差异 ==")
            for x in out["diff"]:
                print("   ! " + x)
        for e in E:
            print("  [ERROR] " + e)
        for w in W:
            print("  [WARN ] " + w)
        for i in I:
            print("  [INFO ] " + i)
        print("结论: " + out["verdict"])
    return 0 if not E else 1


if __name__ == "__main__":
    sys.exit(main())