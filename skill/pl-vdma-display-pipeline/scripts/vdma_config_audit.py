#!/usr/bin/env python3
"""vdma_config_audit.py — PL 端 VDMA 显示通路"能否正常显示"审计（无第三方依赖）

范围：只回答一个问题 —— 这条 VDMA → 视频输出 → 面板 的通路，能不能显示出一帧正确画面。
不做：应用层绘制/刷新策略、性能调优、PS 侧业务逻辑、RTL 修改。

用法:
    python vdma_config_audit.py --hwh system.hwh [--pclk-mhz <MHz>] [--panel-format rgb888|rgb565] [--json]

判定项:
    E1 VDMA 使能 MM2S（显示方向必须有数据源）
    E2 时序成立：有 v_tc、C_GENERATE_EN=1、HFRAME>HACTIVE、VFRAME>VACTIVE、非隔行
    E3 位宽链：TDATA = 分量x3；VDMA 流位宽 = vid_out S_AXIS；M_AXI >= TDATA
    E4 VTG 来源：vid_out 为 slave 时必须有 v_tc
    E5 帧同步：USE_FSYNC=1 时必须有 fsync 源
    E6 VDMA 有 AXI-Lite 基地址（PS 才能配置）
    E7 关键时钟使能/复位输入必须已连接 —— vtg_ce 未接 gen_clken 时 VTG 不推进, 画面全黑且参数看起来全对
    W1 单帧缓冲（能显示，但可能撕裂/闪）
    W2 像素时钟未取到
    I  带宽占比、连线归属等信息
退出码: 0 = 无 ERROR, 1 = 存在 ERROR
"""
import argparse
import json
import re
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def load(p):
    return open(p, encoding="utf-8", errors="replace").read()


def parse_ports(blk):
    """解析 <PORT ...> 或 <PORT .../>，返回 [{name,dir,signame,conns:[(inst,port)]}]"""
    out = []
    i = 0
    while True:
        j = blk.find("<PORT", i)
        if j < 0:
            break
        k = blk.find(">", j)
        if k < 0:
            break
        tag = blk[j:k + 1]
        inner = ""
        if tag.endswith("/>"):
            end = k + 1
        else:
            e = blk.find("</PORT>", k)
            end = (e + len("</PORT>")) if e > 0 else len(blk)
            inner = blk[k + 1:end]
        def attr(n):
            m = re.search(r'%s="([^"]*)"' % n, tag)
            return m.group(1) if m else ""
        conns = re.findall(r'<CONNECTION INSTANCE="([^"]+)" PORT="([^"]+)"', inner)
        out.append({"name": attr("NAME"), "dir": attr("DIR"),
                    "signame": attr("SIGNAME"), "conns": conns})
        i = end
    return out


def modules(t):
    out = {}
    tags = re.findall(r"<MODULE [^>]*>", t)
    blocks = re.findall(r"(?s)<MODULE [^>]*>(.*?)</MODULE>", t)
    for tag, blk in zip(tags, blocks):
        inst = re.search(r'INSTANCE="([^"]+)"', tag)
        typ = re.search(r'MODTYPE="([^"]+)"', tag)
        vlnv = re.search(r'VLNV="([^"]+)"', tag)
        if inst and typ:
            out[inst.group(1)] = {"type": typ.group(1), "blk": blk,
                                  "vlnv": vlnv.group(1) if vlnv else "",
                                  "params": dict(re.findall(r'<PARAMETER NAME="([^"]+)" VALUE="([^"]*)"', blk)),
                                  "ports": parse_ports(blk)}
    return out


def memranges(t):
    out = {}
    for s in re.findall(r"<MEMRANGE [^>]*>", t):
        inst = re.search(r'INSTANCE="([^"]+)"', s)
        base = re.search(r'BASEVALUE="([^"]+)"', s)
        if inst and base:
            out.setdefault(inst.group(1), base.group(1))
    return out


def gi(p, k, d=0):
    try:
        return int(p.get(k, d) or d)
    except ValueError:
        return d


def port(mod, name):
    for p in mod["ports"]:
        if p["name"] == name:
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hwh", required=True)
    ap.add_argument("--pclk-mhz", type=float, default=0.0)
    ap.add_argument("--panel-format", choices=["rgb888", "rgb565"], default=None,
                    help="面板像素格式：rgb888(24bit,8/8/8) 或 rgb565(16bit,5/6/5)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    t = load(a.hwh)
    mods, mem = modules(t), memranges(t)
    E, W, I = [], [], []

    vdma = next((k for k, v in mods.items() if v["type"] == "axi_vdma"), None)
    vtc = next((k for k, v in mods.items() if v["type"] == "v_tc"), None)
    vout = next((k for k, v in mods.items() if v["type"] == "v_axi4s_vid_out"), None)
    if not vdma:
        print("找不到 axi_vdma 实例：这条通路没有 VDMA")
        return 1
    vp = mods[vdma]["params"]

    hactive = vactive = hframe = vframe = 0
    interlaced = gen_en = None
    if vtc:
        tp = mods[vtc]["params"]
        hactive, vactive = gi(tp, "C_GEN_HACTIVE_SIZE"), gi(tp, "C_GEN_VACTIVE_SIZE")
        hframe, vframe = gi(tp, "C_GEN_HFRAME_SIZE"), gi(tp, "C_GEN_F0_VFRAME_SIZE")
        interlaced, gen_en = gi(tp, "C_GEN_INTERLACED"), gi(tp, "C_GENERATE_EN")

    axis_w = gi(vp, "C_M_AXIS_MM2S_TDATA_WIDTH")
    maxi_w = gi(vp, "C_M_AXI_MM2S_DATA_WIDTH")
    comp_w = gi(mods[vout]["params"], "C_S_AXIS_COMPONENT_WIDTH", 8) if vout else 8
    vout_w = gi(mods[vout]["params"], "C_S_AXIS_TDATA_WIDTH") if vout else 0
    vtg_ms = gi(mods[vout]["params"], "C_VTG_MASTER_SLAVE") if vout else None

    # E1
    if str(vp.get("C_INCLUDE_MM2S", "0")) != "1":
        E.append("E1 VDMA 未使能 MM2S：显示方向没有数据源，屏幕不会亮")
    # E2
    if not vtc:
        E.append("E2 找不到 v_tc：没有行/场时序")
    else:
        if gen_en == 0:
            E.append("E2 v_tc 的 C_GENERATE_EN=0：时序发生器未使能")
        if hframe <= hactive:
            E.append("E2 HFRAME(%d) <= HACTIVE(%d)：没有行消隐，显示器无法识别" % (hframe, hactive))
        if vframe <= vactive:
            E.append("E2 VFRAME(%d) <= VACTIVE(%d)：没有场消隐" % (vframe, vactive))
        if interlaced == 1:
            E.append("E2 v_tc 配置为隔行：多数 LCD 只接受逐行")
    # E3
    if axis_w and comp_w and axis_w != comp_w * 3:
        E.append("E3 stream 位宽(%d) != 分量位宽(%d)x3：颜色会错乱" % (axis_w, comp_w))
    if vout and axis_w and vout_w and axis_w != vout_w:
        E.append("E3 VDMA stream 位宽(%d) 与 vid_out S_AXIS(%d) 不一致：AXIS 无法对接" % (axis_w, vout_w))
    if maxi_w and axis_w and maxi_w < axis_w:
        E.append("E3 M_AXI(%d bit) < stream(%d bit)" % (maxi_w, axis_w))
    # E4
    if vout is None:
        E.append("E4 找不到 AXI4-Stream to Video Out：没有 AXIS→视频时序 的环节")
    elif vtg_ms == 0 and not vtc:
        E.append("E4 vid_out 为 VTG slave 但没有 v_tc 提供时序")
    # E5
    if str(vp.get("C_USE_FSYNC", "0")) == "1" and not vtc:
        E.append("E5 USE_FSYNC=1 但没有 fsync 源（应由 v_tc 的 SOF 提供）")
    # E6
    if not mem.get(vdma):
        E.append("E6 找不到 VDMA 的 AXI-Lite 基地址：PS 无法配置，通路不会被启动")

    # E8 像素格式与转换 IP 选型（v_axi4s_vid_out 只支持等宽分量，不支持 RGB565）
    custom = [k for k, v in mods.items() if v.get("vlnv") and not v["vlnv"].startswith("xilinx.com")]
    if a.panel_format == "rgb565":
        if vout is not None and comp_w != 5:
            if custom:
                I.append("E8 面板为 RGB565：v_axi4s_vid_out 只支持等宽分量(8/10/12/16)，无法直接出 565；"
                         "本设计在其后接了自定义 IP %s 做 24bit->565 转换，需人工核对该 IP 的时序与位宽"
                         % ", ".join(sorted(custom)))
            else:
                E.append("E8 面板为 RGB565 但链路里只有 v_axi4s_vid_out（等宽分量），没有任何 565 转换环节："
                         "565 分量宽度 5/6/5 不等，该 IP 不支持 -> 颜色与位宽都会错。"
                         "修法: 在后面加一个自定义转换 IP（24bit RGB -> 16bit 565），或改用支持 565 的方案。")
        elif custom:
            I.append("E8 面板为 RGB565，检测到自定义 IP %s：按其参数与手册核对转换关系" % ", ".join(sorted(custom)))
    elif a.panel_format == "rgb888":
        if vout is not None and comp_w != 8:
            W.append("E8 面板为 RGB888 但 v_axi4s_vid_out 的分量位宽为 %d（应为 8）" % comp_w)
        elif vout is not None:
            I.append("E8 面板为 RGB888 且 vid_out 分量为 8bit：IP 选型匹配")
    else:
        I.append("E8 未提供面板像素格式（--panel-format）：无法判断 'vid_out 是否够用'，"
                 "RGB565 面板必须另有转换环节")
    if custom:
        I.append("自定义 IP 清单（非 xilinx.com）: " + ", ".join(sorted(custom)))

    # ---- E7 关键输入连线检查 ----
    need = []
    if vout:
        need.append((vout, "vtg_ce", "E"))
        need.append((vout, "vid_io_out_ce", "I"))
        need.append((vout, "aclken", "I"))
        need.append((vout, "vid_io_out_reset", "I"))
    if vtc:
        need.append((vtc, "clken", "I"))
    for inst, pn, lvl in need:
        p = port(mods[inst], pn)
        if p is None:
            I.append("%s 无 %s 端口（IP 版本差异，可忽略）" % (inst, pn))
            continue
        connected = bool(p["signame"]) or bool(p["conns"])
        if not connected:
            if lvl == "E":
                E.append("E7 %s 的 %s 没有任何连接：若为悬空/常量, VTG 不推进 -> 行场时序不变 -> 画面全黑"
                         "（其它参数可能看起来全对）。修法: 把它接到 v_tc 的 gen_clken。" % (inst, pn))
            else:
                I.append("%s 的 %s 未连信号（hwh 用自闭合端口表示'接默认值'，通常由 IP 默认使能，属正常）"
                         % (inst, pn))
        elif pn == "vtg_ce":
            src = ", ".join("%s/%s" % (i2, q) for i2, q in p["conns"])
            if "gen_clken" in src:
                I.append("E7 vtg_ce 已连到 %s（正确：VTG 由 v_tc 的时钟使能推进）" % src)
            else:
                W.append("E7 vtg_ce 由 %s 驱动：确认它是帧率时钟使能，而不是普通数据或常量" % src)
    # 时钟
    pclk, src = a.pclk_mhz, None
    if pclk <= 0:
        for k, v in mods.items():
            if v["type"] == "clk_wiz":
                f = v["params"].get("CLKOUT1_REQUESTED_OUT_FREQ")
                if not f:
                    continue
                try:
                    fv = float(f)
                except ValueError:
                    continue
                if 20.0 <= fv <= 200.0:
                    pclk, src = fv, k
                    I.append("像素时钟取自 %s CLKOUT1 = %.3f MHz" % (k, fv))
                    break
    if pclk <= 0:
        W.append("W2 未取到像素时钟：无法核对帧率（用 --pclk-mhz 指定）")

    nf = gi(vp, "C_NUM_FSTORES")
    if nf <= 1:
        W.append("W1 C_NUM_FSTORES=1（单帧缓冲）：能显示，但可能出现撕裂/局部闪")
    bytes_per_px = (axis_w // 8) if axis_w else 3
    frame_bytes = hactive * vactive * bytes_per_px

    res = {"hwh": a.hwh, "vdma": vdma, "vtc": vtc, "vid_out": vout,
           "axi_lite_base": mem.get(vdma), "hactive": hactive, "vactive": vactive,
           "hframe": hframe, "vframe": vframe, "pclk_mhz": pclk, "pclk_from": src,
           "axis_width": axis_w, "maxi_width": maxi_w, "component_width": comp_w,
           "num_fstores": nf, "include_mm2s": vp.get("C_INCLUDE_MM2S"),
           "use_fsync": vp.get("C_USE_FSYNC"), "genlock_mode": vp.get("C_MM2S_GENLOCK_MODE"),
           "vtg_ce": (port(mods[vout], "vtg_ce") or {}).get("conns") if vout else None,
           "panel_format": a.panel_format, "custom_ips": custom,
           "frame_bytes": frame_bytes, "errors": E, "warnings": W, "infos": I}

    if hframe and vframe and pclk > 0:
        tf = hframe * vframe / (pclk * 1e6) * 1000.0
        res["frame_period_ms"] = round(tf, 3)
        res["fps"] = round(1000.0 / tf, 2)
        if frame_bytes:
            need_mb = frame_bytes / (tf / 1000.0) / 1e6
            res["need_MBps"] = round(need_mb, 1)
            if maxi_w:
                peak = maxi_w / 8 * 100e6 / 1e6
                res["peak_MBps_100MHz"] = round(peak, 1)
                res["util_pct"] = round(need_mb / peak * 100.0, 2)
                if res["util_pct"] > 70.0:
                    W.append("W1 带宽占比 %.1f%%：可能出现断帧/花屏" % res["util_pct"])
    res["verdict"] = "FAIL" if E else ("WARN" if W else "PASS")

    if a.json:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        print("VDMA          : %s   AXI-Lite %s" % (vdma, res["axi_lite_base"]))
        print("时序          : %d x %d  (H %d / V %d)" % (hactive, vactive, hframe, vframe))
        print("像素时钟      : %s" % (("%.3f MHz (%s)" % (pclk, src)) if pclk else "未取到"))
        if "fps" in res:
            print("帧周期/帧率   : %.3f ms / %.2f fps" % (res["frame_period_ms"], res["fps"]))
        print("位宽链        : M_AXI %d -> TDATA %d -> 分量 %d (帧 %d B)" % (maxi_w, axis_w, comp_w, frame_bytes))
        print("缓冲/同步     : N_FSTORES=%d  MM2S=%s  FSYNC=%s" % (nf, res["include_mm2s"], res["use_fsync"]))
        print("面板格式      : %s%s" % (a.panel_format or "未提供",
                                        ("  自定义IP: " + ", ".join(sorted(custom))) if custom else ""))
        for e in E:
            print("  [ERROR] %s" % e)
        for w in W:
            print("  [WARN ] %s" % w)
        for i in I:
            print("  [INFO ] %s" % i)
        print("结论: %s" % res["verdict"])
        if not E:
            print("下一步: PL 侧配置与连线自洽；能否显示还要看 PS 是否启动 VDMA（写地址/长度/帧数并置运行位）")
    return 0 if not E else 1


if __name__ == "__main__":
    sys.exit(main())