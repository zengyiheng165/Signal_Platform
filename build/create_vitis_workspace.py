#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
create_vitis_workspace.py
=========================

为「软件定义信号测试与验证平台」的 Zynq-7000 PS 端应用，按工程内自带的输入文件，
用 Vitis 2026.1 重新生成一个可以直接打开、可以直接编译的 Vitis Unified IDE 工作区。

设计目标
--------
1. 可移植：整个工程目录可以被搬到任意路径（甚至换一台电脑、换一个盘符），
   只要运行本脚本，就会用"当前的真实路径"重新建立工作区，不会残留旧路径。
2. 不污染工程：不把原来的 Vitis_WorkSpace 拷进工程。工作区是生成物，
   固定生成在 <工程>/board/Vitis_WorkSpace。
3. 输入集中：建立工程所需的全部文件都放在 <工程>/src/ps
       *.c / *.h              应用源码（自动扫描，新增文件无需改脚本）
       system_wrapper.xsa     硬件描述（Vivado 导出的 XSA）
       vitis_project.json     工程清单（平台、应用、编译选项）
       ide/launch.json        （可选）调试配置模板
4. 幂等：重复运行不会重复建工程；只有源码/清单/XSA/工程路径变化时才重新生成。

常用命令
--------
    build\\create_vitis_workspace.bat                  # 双击即可：自动挑 Python / 用 Vitis 自带 Python

    python build/create_vitis_workspace.py             # 需要时重建，然后打开 Vitis IDE
    python build/create_vitis_workspace.py --check     # 只做环境体检，不生成
    python build/create_vitis_workspace.py --sync      # 源码改过：只同步源码并重新编译
    python build/create_vitis_workspace.py --pull      # 把工作区里改过的源码同步回 src/ps
    python build/create_vitis_workspace.py --fix-sources  # 清掉源码里硬编码的绝对路径 #include
    python build/create_vitis_workspace.py --clean     # 强制删除工作区并重新生成
    python build/create_vitis_workspace.py --no-open   # 只生成，不打开 IDE
    python build/create_vitis_workspace.py --vitis "D:\\2026.1\\Vitis"

    也可以用 Vitis 自带的 Python 直接跑（不需要系统 Python）：
    vitis -s build/create_vitis_workspace.py

运行环境
--------
* Windows / Linux，Python 3.8 及以上。
* Vitis 2026.1（Unified IDE）。脚本会自动在常见安装位置查找，
  也可以用 --vitis 指定安装根目录或 vitis.bat / vitis 的完整路径。
"""

import argparse
import datetime
import glob
import hashlib
import inspect
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import traceback

# --------------------------------------------------------------------------- #
# 常量
# --------------------------------------------------------------------------- #

SCRIPT_PATH = os.path.abspath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_PATH)
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

MARKER_NAME = ".vitis_workspace.json"
RESULT_NAME = ".vitis_result.json"
DEFAULT_MANIFEST = os.path.join("src", "ps", "vitis_project.json")
DEFAULT_WORKSPACE = os.path.join("board", "Vitis_WorkSpace")

SRC_EXTENSIONS = (".c", ".h")

# Windows 上 Vitis 的 BSP 编译对路径长度非常敏感（CMake/ninja 的对象文件路径会超过
# 260 字符上限，表现为 "Invalid project location"）。工作区路径超过这个长度时，
# 脚本会自动在盘符根目录建一个短路径联接点，让 Vitis 通过短路径工作。
MAX_DIRECT_PATH = 70

# Vitis 从 2024.1 起才有 Unified IDE（vitis-comp.json / vitis -s）
# 版本号统一按「两位年份.小版本」比较，例如 2026.1 记为 (26, 1)
MIN_UNIFIED_VERSION = (24, 1)

_VERSION_RE = re.compile(r"^(?:20)?(\d{2})\.(\d)$")

# 源码里可能残留的「硬编码绝对路径 #include」，例如
#   #include "F:\FPGA_Vivado_26.1\...\drivers\fft_init_d_v1_0\src\fft_init_d.h"
# 换机器后就找不到文件了，生成工作区时会自动改成 #include "fft_init_d.h"，
# 并把对应的驱动头文件目录加进 USER_INCLUDE_DIRECTORIES。
# 直接按字节处理，避免中文注释的编码问题。
_ABS_INCLUDE_RE = re.compile(
    br'(^[ \t]*#[ \t]*include[ \t]*")([A-Za-z]:[\\/][^"\r\n]*[\\/])([^"\\/\r\n]+)(")',
    re.MULTILINE)

# Vitis 内部有些路径会以命令参数形式传递，遇到空格/中文等字符会失败
# （典型现象：create_platform_component 报 "'standalone' is an invalid os. Valid OSes are '[]'"）。
# 这类路径会先复制到纯 ASCII、无空格的暂存目录再用。
_SIMPLE_PATH_RE = re.compile(r"^[A-Za-z0-9_.\-\\/:]+$")


def _path_is_simple(path):
    return bool(_SIMPLE_PATH_RE.match(str(path)))


# --------------------------------------------------------------------------- #
# 日志
# --------------------------------------------------------------------------- #

class Logger(object):
    """同时输出到控制台和日志文件。"""

    def __init__(self, path=None):
        self.path = path
        self._fh = None
        self._open()

    def _open(self):
        if not self.path:
            return
        try:
            parent = os.path.dirname(self.path)
            if parent and not os.path.isdir(parent):
                os.makedirs(parent)
            self._fh = open(self.path, "a", encoding="utf-8")
        except Exception:
            self._fh = None

    def close_file(self):
        if self._fh:
            try:
                self._fh.close()
            except Exception:
                pass
            self._fh = None

    def _write(self, text):
        try:
            print(text)
        except UnicodeEncodeError:
            try:
                print(text.encode("ascii", "replace").decode("ascii"))
            except Exception:
                pass
        except Exception:
            pass
        if self._fh:
            try:
                self._fh.write(text + "\n")
                self._fh.flush()
            except Exception:
                pass

    def raw(self, text=""):
        self._write(text)

    def info(self, text):
        self._write(text)

    def step(self, index, total, text):
        self._write("[%d/%d] %s" % (index, total, text))

    def ok(self, text):
        self._write("      [OK] " + text)

    def warn(self, text):
        self._write("      [注意] " + text)

    def error(self, text):
        self._write("      [错误] " + text)

    def info_detail(self, text):
        self._write("      [info] " + text)


LOG = Logger()


def log_init(path):
    global LOG
    old = LOG
    LOG = Logger(path)
    old.close_file()
    return LOG


def _stamp():
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


# --------------------------------------------------------------------------- #
# 小工具
# --------------------------------------------------------------------------- #

def _exists(path):
    try:
        return os.path.exists(path)
    except Exception:
        return False


def _read_json(path):
    with open(path, "r", encoding="utf-8-sig") as fh:
        return json.load(fh)


def _write_json(path, data):
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    os.replace(tmp, path)


def _sha256(path, block=1 << 20):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(block)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _file_signature(path):
    st = os.stat(path)
    return {"size": st.st_size,
            "mtime": int(getattr(st, "st_mtime_ns", int(st.st_mtime * 1e9)))}


def _human_size(num):
    value = float(num)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024.0 or unit == "GB":
            if unit == "B":
                return "%d B" % int(value)
            return "%.2f %s" % (value, unit)
        value /= 1024.0
    return "%d B" % int(num)


def _rel_or_abs(root, path):
    """尽量用相对路径，规避 Vitis 启动器对含空格参数处理不当的问题。"""
    path = os.path.abspath(path)
    try:
        rel = os.path.relpath(path, root)
    except ValueError:
        return path
    if rel.startswith("..") or " " in rel:
        return path
    return rel


def _rmtree(path, logger=None):
    """删除目录；失败时用 robocopy 清空，处理 Windows 超长路径与只读文件。"""
    logger = logger or LOG
    if not _exists(path):
        return True

    def _onerror(func, target, exc_info):
        try:
            os.chmod(target, 0o666)
            if os.path.isdir(target):
                os.chmod(target, 0o777)
            func(target)
        except Exception:
            pass

    def _try_rmtree():
        for _ in range(2):
            try:
                shutil.rmtree(path, onerror=_onerror)
            except Exception as exc:
                logger.info_detail("普通删除失败：%s" % exc)
            if not _exists(path):
                return True
            if not os.path.isdir(path):
                return False
        return not _exists(path)

    if _try_rmtree():
        return True

    if os.name == "nt":
        # robocopy 支持超长路径：用一个空目录镜像过去，等于清空
        empty = os.path.join(os.environ.get("TEMP") or os.path.expanduser("~"), "vitis_empty_dir")
        try:
            if not os.path.isdir(empty):
                os.makedirs(empty)
            subprocess.call(["robocopy", empty, path, "/MIR", "/NFL", "/NDL", "/NJH",
                             "/NJS", "/NC", "/NS", "/NP", "/R:1", "/W:1"])
        except Exception as exc:
            logger.info_detail("robocopy 清理失败：%s" % exc)
        if _try_rmtree():
            return True
        try:
            subprocess.call(["cmd", "/c", "rmdir", "/s", "/q", path])
        except Exception:
            pass

    return not _exists(path)


def _enable_console_errors():
    """控制台直连时用系统编码（Python 会走 Unicode 控制台 API），
    被重定向/管道捕获时用 UTF-8，避免中文变成乱码。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            if stream.isatty():
                stream.reconfigure(errors="replace")
            else:
                stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def _version_tuple(text):
    match = _VERSION_RE.match(str(text).strip())
    if not match:
        return (0, 0)
    return (int(match.group(1)), int(match.group(2)))


def _version_from_path(path):
    parts = re.split(r"[\\/]+", os.path.abspath(path))
    for part in reversed(parts):
        if _VERSION_RE.match(part):
            return part
    return ""


# --------------------------------------------------------------------------- #
# 长路径规避：短路径联接点
# --------------------------------------------------------------------------- #

def _is_reparse_point(path):
    """目录是否是联接点/符号链接（Windows）。"""
    if os.name != "nt":
        return os.path.islink(path)
    try:
        import ctypes
        attrs = ctypes.windll.kernel32.GetFileAttributesW(path)
        return attrs not in (-1, 0) and bool(attrs & 0x400)  # REPARSE_POINT
    except Exception:
        return False


def _link_target(path):
    """返回联接点真实指向的目标；不是联接点时返回空串。"""
    try:
        if not _is_reparse_point(path):
            return ""
        return os.path.normcase(os.path.realpath(path))
    except Exception:
        return ""


def _ensure_junction(alias, target, logger):
    target_abs = os.path.abspath(target)
    if os.path.isdir(alias):
        if _link_target(alias) == os.path.normcase(os.path.realpath(target_abs)):
            return True
        if _is_reparse_point(alias):
            try:
                os.rmdir(alias)
                logger.info_detail("已移除失效联接点 %s" % alias)
            except Exception as exc:
                logger.warn("无法移除联接点 %s：%s" % (alias, exc))
                return False
        else:
            # 不是联接点的普通目录（常见于手工创建、或 Vitis 在联接点缺失时自己建的）
            try:
                if not os.listdir(alias):
                    os.rmdir(alias)
                    logger.info_detail("已移除同名空目录 %s" % alias)
                else:
                    logger.warn("%s 已存在且不是联接点，无法自动清理（里面还有文件）" % alias)
                    return False
            except Exception as exc:
                logger.warn("处理同名目录 %s 失败：%s" % (alias, exc))
                return False
    try:
        parent = os.path.dirname(alias)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
    except Exception:
        pass
    try:
        rc = subprocess.call(["cmd", "/c", "mklink", "/J", alias, target_abs])
    except Exception as exc:
        logger.warn("创建联接点失败（%s）：%s" % (alias, exc))
        return False
    if rc != 0 or not os.path.isdir(alias):
        logger.warn("创建联接点失败：%s -> %s" % (alias, target_abs))
        return False
    return True


def _remove_junction(path, logger):
    if not path or not os.path.isdir(path):
        return
    if not _is_reparse_point(path):
        return
    target = _link_target(path)
    if target and os.path.isdir(target):
        # 目标还在：可能是别的工程在用，不动它
        return
    try:
        os.rmdir(path)
        logger.info_detail("已清理失效联接点 %s" % path)
    except Exception as exc:
        logger.warn("清理旧联接点失败 %s：%s" % (path, exc))


def _short_alias_root(project_root, logger):
    """挑一个可写的短目录作为联接点根，例如 D:\\vws（必须是纯 ASCII、无空格）。"""
    candidates = []
    drive = os.path.splitdrive(os.path.abspath(project_root))[0]
    if os.name == "nt":
        if drive:
            candidates.append(drive + "\\vws")
    else:
        candidates.append("/tmp/vws")
    for var in ("LOCALAPPDATA", "TEMP", "TMP"):
        value = os.environ.get(var)
        if value:
            candidates.append(os.path.join(value, "vws"))
    for base in candidates:
        if not _path_is_simple(base):
            continue
        try:
            if not os.path.isdir(base):
                os.makedirs(base)
            if os.path.isdir(base):
                return base
        except Exception:
            continue
    return ""


def _alias_tag(project_root):
    name = re.sub(r"[^A-Za-z0-9]", "", os.path.basename(os.path.abspath(project_root)))[:12]
    digest = hashlib.sha1(
        os.path.normcase(os.path.abspath(project_root)).encode("utf-8")).hexdigest()[:6]
    return "%s_%s" % (name or "prj", digest)


def resolve_workspace(ctx, logger=None):
    """确定 Vitis 实际使用的工作区路径；必要时走短路径联接点。

    真实工作区仍然放在工程里（`board\\Vitis_WorkSpace`），
    当路径过长/含特殊字符时，在 `<盘>:\\vws\\<工程名>_<短哈希>` 建一个
    指向工程 `board` 目录的联接点，让 Vitis 通过短路径访问。

    如果路径确实过长、又建立不了联接点，会把原因记到 ctx.workspace_blocked 里——
    直接沿用长路径去建平台一定会编译失败，所以调用方应该拒绝继续。"""
    logger = logger or LOG
    requested = os.path.abspath(ctx.workspace)
    ctx.workspace_requested = requested
    ctx.xsa_effective = ctx.xsa
    ctx.workspace_blocked = ""
    ctx.workspace_link = ""

    need_alias = os.name == "nt" and (len(requested) > MAX_DIRECT_PATH
                                      or not _path_is_simple(requested))
    if not need_alias:
        ctx.workspace = requested
        ctx.workspace_alias = ""
        _stage_xsa(ctx, logger)
        return requested

    root = _short_alias_root(ctx.root, logger)
    if not root:
        logger.warn("工作区路径过长或含特殊字符，且找不到可用的短路径目录")
        ctx.workspace = requested
        ctx.workspace_alias = ""
        ctx.workspace_blocked = "找不到可用的短路径目录（如 <盘>:\\vws）"
        _stage_xsa(ctx, logger)
        return requested

    alias = os.path.join(root, _alias_tag(ctx.root))
    target = os.path.dirname(requested)
    try:
        if not os.path.isdir(target):
            os.makedirs(target)
    except Exception:
        pass
    if not _ensure_junction(alias, target, logger):
        ctx.workspace = requested
        ctx.workspace_alias = alias
        ctx.workspace_blocked = alias
        _stage_xsa(ctx, logger)
        return requested

    effective = os.path.join(alias, os.path.basename(requested))
    if len(effective) >= len(requested) and _path_is_simple(requested):
        ctx.workspace = requested
        ctx.workspace_alias = ""
    else:
        ctx.workspace = effective
        ctx.workspace_alias = alias
    _stage_xsa(ctx, logger)
    return ctx.workspace


def _stage_xsa(ctx, logger=None):
    """XSA 路径含空格/中文时，先复制到纯 ASCII 的暂存目录（Vitis 内部按命令行传递该路径）。"""
    logger = logger or LOG
    ctx.xsa_effective = ctx.xsa
    if _path_is_simple(ctx.xsa) or not os.path.isfile(ctx.xsa):
        return ctx.xsa

    if ctx.workspace_alias:
        root = os.path.dirname(ctx.workspace_alias)
    else:
        root = _short_alias_root(ctx.root, logger)
    if not root:
        logger.warn("XSA 路径含空格或中文，且找不到可用暂存目录，可能影响 Vitis 创建平台")
        return ctx.xsa

    stage = os.path.join(root, _alias_tag(ctx.root) + "_hw")
    if not _path_is_simple(stage):
        logger.warn("暂存目录路径不规范：%s" % stage)
        return ctx.xsa
    try:
        if not os.path.isdir(stage):
            os.makedirs(stage)
        dest = os.path.join(stage, os.path.basename(ctx.xsa))
        need_copy = True
        if os.path.isfile(dest) and os.path.getsize(dest) == os.path.getsize(ctx.xsa):
            need_copy = False
        if need_copy:
            shutil.copy2(ctx.xsa, dest)
        ctx.xsa_effective = dest
        logger.info_detail("XSA 路径含空格/中文，已暂存到 %s" % dest)
    except Exception as exc:
        logger.warn("暂存 XSA 失败：%s" % exc)
        return ctx.xsa
    return ctx.xsa_effective


# --------------------------------------------------------------------------- #
# 工程上下文（路径 + 清单）
# --------------------------------------------------------------------------- #

def _default_manifest():
    return {
        "schema": 1,
        "description": "Zynq-7000 软件定义信号测试与验证平台 · PS 端应用",
        "vitis_version": "2026.1",
        "workspace": DEFAULT_WORKSPACE,
        "xsa": "system_wrapper.xsa",
        "platform": {
            "name": "platform_v5_4",
            "os": "standalone",
            "cpu": "ps7_cortexa9_0",
            "domain": "standalone_ps7_cortexa9_0",
            "compiler": "gcc",
            "no_boot_bsp": False,
        },
        "app": {
            "name": "app",
            "template": "hello_world",
            "domain": "",
            "link_libraries": ["m"],
            "optimization": "-O2",
            "stack_size": "0x200000",
            "heap_size": "0x200000",
            "sources": [],
        },
        "debug_launch": "ide/launch.json",
    }


def _deep_merge(base, override):
    out = dict(base)
    for key, value in (override or {}).items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _deep_merge(out[key], value)
        else:
            out[key] = value
    return out


class Context(object):
    def __init__(self, project_root, manifest_path=None, workspace=None):
        self.root = os.path.abspath(project_root)
        self.src_dir = os.path.join(self.root, "src", "ps")
        # 脚本自己放在 build/，而所有“生成物”（工作区、日志、状态文件）统一放在 board/
        self.build_dir = os.path.join(self.root, "build")
        self.out_dir = os.path.join(self.root, "board")
        self.manifest_path = os.path.abspath(
            manifest_path or os.path.join(self.root, DEFAULT_MANIFEST))
        self.manifest = self._load_manifest()
        self.workspace = os.path.abspath(
            workspace or os.path.join(self.root, self.manifest.get("workspace") or DEFAULT_WORKSPACE))
        self.workspace_requested = self.workspace
        self.workspace_alias = ""
        self.workspace_link = ""
        self.workspace_blocked = ""
        self.marker_path = os.path.join(self.out_dir, MARKER_NAME)
        self.log_dir = os.path.join(self.out_dir, "logs")

        platform = self.manifest.get("platform") or {}
        app = self.manifest.get("app") or {}
        self.platform_name = platform.get("name") or "platform"
        self.app_name = app.get("name") or "app"
        self.domain = (app.get("domain") or platform.get("domain")
                       or ("standalone_" + str(platform.get("cpu") or "ps7_cortexa9_0")))
        self.xsa = os.path.join(self.src_dir, self.manifest.get("xsa") or "system_wrapper.xsa")
        self.xsa_effective = self.xsa
        self.target_version = str(self.manifest.get("vitis_version") or "2026.1")
        self.log_file_path = ""

    # -- 清单 ------------------------------------------------------------- #

    def _load_manifest(self):
        data = {}
        if _exists(self.manifest_path):
            data = _read_json(self.manifest_path)
        return _deep_merge(_default_manifest(), data)

    # -- 源文件 ----------------------------------------------------------- #

    def declared_sources(self):
        return (self.manifest.get("app") or {}).get("sources") or []

    def source_files(self):
        """要导入工作区的 .c/.h；清单为空时自动扫描 src/ps。"""
        declared = self.declared_sources()
        if not declared:
            names = []
            if os.path.isdir(self.src_dir):
                for name in sorted(os.listdir(self.src_dir)):
                    if name.lower().endswith(SRC_EXTENSIONS):
                        names.append(name)
            return names
        result = []
        for name in declared:
            result.append(str(name).replace("\\", "/"))
        return result

    def compile_sources(self):
        return [n for n in self.source_files() if n.lower().endswith(".c")]

    def unlisted_sources(self):
        """清单是显式列表时，src/ps 里没被列进去的源文件。"""
        if not self.declared_sources():
            return []
        listed = set(n.lower() for n in self.source_files())
        extra = []
        if os.path.isdir(self.src_dir):
            for name in sorted(os.listdir(self.src_dir)):
                if name.lower().endswith(SRC_EXTENSIONS) and name.lower() not in listed:
                    extra.append(name)
        return extra

    def inventory(self):
        data = {}
        for name in self.source_files():
            path = os.path.join(self.src_dir, name)
            if _exists(path):
                data[name] = _file_signature(path)
        return data

    def xsa_signature(self):
        if not _exists(self.xsa):
            return None
        return _file_signature(self.xsa)

    def manifest_sha(self):
        if not _exists(self.manifest_path):
            return ""
        return _sha256(self.manifest_path)

    def new_log_path(self):
        return os.path.join(self.log_dir, "create_workspace_%s.log" % _stamp())


# --------------------------------------------------------------------------- #
# 查找 Vitis
# --------------------------------------------------------------------------- #

class VitisInstall(object):
    def __init__(self, launcher, root, version):
        self.launcher = launcher or ""
        self.root = root or ""
        self.version = version or ""
        self.version_tuple = _version_tuple(self.version)

    def __str__(self):
        label = self.version or "未知版本"
        return "%s（%s）" % (self.launcher or "当前环境", label)


def _launcher_in(directory):
    if not directory or not os.path.isdir(directory):
        return None
    names = ("vitis.bat", "vitis.cmd", "vitis.exe") if os.name == "nt" else ("vitis",)
    for name in names:
        path = os.path.join(directory, name)
        if os.path.isfile(path):
            return path
    return None


def _install_from_launcher(launcher):
    launcher = os.path.abspath(launcher)
    root = os.path.dirname(os.path.dirname(launcher))
    version = _version_from_path(root) or _version_from_path(launcher)
    return VitisInstall(launcher, root, version)


def _drives():
    if os.name != "nt":
        return ["/"]
    roots = []
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        mask = kernel32.GetLogicalDrives()
        for index in range(26):
            if not (mask & (1 << index)):
                continue
            root = chr(ord("A") + index) + ":\\"
            # 2=可移动 3=固定 6=RAM 盘；跳过网络盘，避免卡顿
            if kernel32.GetDriveTypeW(root) in (2, 3, 6):
                roots.append(root)
    except Exception:
        roots = [c + ":\\" for c in "CDEFGH"]
    return roots or ["C:\\"]


def _scan_install_root(base):
    """base 可能本身是安装目录，也可能下面还有「版本号」目录。"""
    found = []
    if not base or not os.path.isdir(base):
        return found

    launcher = _launcher_in(os.path.join(base, "bin"))
    if launcher:
        found.append(_install_from_launcher(launcher))
        return found

    try:
        entries = sorted(os.listdir(base))
    except Exception:
        return found

    for entry in entries:
        child = os.path.join(base, entry)
        if not os.path.isdir(child):
            continue
        for candidate in (os.path.join(child, "bin"),
                          os.path.join(child, "Vitis", "bin")):
            launcher = _launcher_in(candidate)
            if launcher:
                found.append(_install_from_launcher(launcher))
                break
    return found


def _scan_drive_root(drive):
    """<盘符>:\\<版本号>\\Vitis\\bin\\vitis.bat，例如 D:\\2026.1\\Vitis。"""
    found = []
    try:
        entries = sorted(os.listdir(drive))
    except Exception:
        return found
    for entry in entries:
        if not _VERSION_RE.match(entry):
            continue
        base = os.path.join(drive, entry)
        if not os.path.isdir(base):
            continue
        for candidate in (os.path.join(base, "Vitis", "bin"), os.path.join(base, "bin")):
            launcher = _launcher_in(candidate)
            if launcher:
                found.append(_install_from_launcher(launcher))
                break
    return found


def _layouts_under(base):
    """在 base 及它的一级子目录下尝试常见布局，返回可能的启动器。"""
    launchers = []
    try:
        children = sorted(os.listdir(base))
    except Exception:
        children = []
    candidates = [base, os.path.join(base, "Vitis")]
    for child in children:
        path = os.path.join(base, child)
        if os.path.isdir(path):
            candidates.append(path)
            candidates.append(os.path.join(path, "Vitis"))
    seen = set()
    for candidate in candidates:
        key = os.path.normcase(candidate)
        if key in seen:
            continue
        seen.add(key)
        launcher = _launcher_in(os.path.join(candidate, "bin"))
        if launcher:
            launchers.append(launcher)
    return launchers


def _scan_drive_tree(drive):
    """逐层扫描：覆盖 X:\\<目录>\\<版本>\\Vitis、X:\\<目录>\\Vitis 这类自定义安装路径。"""
    found = []
    skip = set()
    windir = os.environ.get("WINDIR") or os.environ.get("SystemRoot")
    for name in (windir, os.environ.get("USERPROFILE"), os.environ.get("LOCALAPPDATA"),
                 os.environ.get("APPDATA"), os.environ.get("ProgramData")):
        if name:
            skip.add(os.path.normcase(os.path.abspath(name)))
    for name in ("$Recycle.Bin", "System Volume Information", "Recovery", "Windows",
                 "Users", "PerfLogs"):
        skip.add(os.path.normcase(os.path.join(drive, name)))

    for launcher in _layouts_under(drive):
        found.append(_install_from_launcher(launcher))

    try:
        level1 = sorted(os.listdir(drive))
    except Exception:
        return found
    for entry in level1:
        path = os.path.join(drive, entry)
        if os.path.normcase(path) in skip or not os.path.isdir(path):
            continue
        for launcher in _layouts_under(path):
            found.append(_install_from_launcher(launcher))
    return found


def _reg_value(key, name):
    try:
        import winreg
        value, _kind = winreg.QueryValueEx(key, name)
        return str(value or "")
    except Exception:
        return ""


def _registry_layouts(location, version, icon):
    bases = []
    if location:
        bases.append(location)
    if icon:
        bases.append(os.path.dirname(os.path.dirname(icon)))
    out = []
    for base in bases:
        base = base.strip().strip('"')
        if not base:
            continue
        out.append(base)
        if version:
            out.append(os.path.join(base, version, "Vitis"))
            out.append(os.path.join(base, "Vitis", version))
            out.append(os.path.join(base, version))
        out.append(os.path.join(base, "Vitis"))
    return out


def _registry_candidates():
    """从 Windows 卸载注册表里找 AMD/Xilinx 安装的 Vitis（自定义安装路径也能找到）。"""
    found = []
    if os.name != "nt":
        return found
    try:
        import winreg
    except Exception:
        return found

    locations = [
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
        (winreg.HKEY_LOCAL_MACHINE,
         r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"),
        (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"),
    ]
    for hive, base_path in locations:
        try:
            root = winreg.OpenKey(hive, base_path)
        except Exception:
            continue
        try:
            count = winreg.QueryInfoKey(root)[0]
        except Exception:
            count = 0
        for index in range(count):
            sub = None
            try:
                sub = winreg.OpenKey(root, winreg.EnumKey(root, index))
                display = _reg_value(sub, "DisplayName")
                if "vitis" not in display.lower():
                    continue
                version = _reg_value(sub, "DisplayVersion")
                location = _reg_value(sub, "InstallLocation")
                icon = _reg_value(sub, "DisplayIcon")
                for candidate in _registry_layouts(location, version, icon):
                    launcher = _launcher_in(os.path.join(candidate, "bin"))
                    if launcher:
                        found.append(_install_from_launcher(launcher))
                        break
            except Exception:
                continue
            finally:
                if sub is not None:
                    try:
                        winreg.CloseKey(sub)
                    except Exception:
                        pass
    return found


def find_vitis(explicit=None, prefer=None, logger=None):
    """按优先级查找 Vitis，返回 (选中的 VitisInstall 或 None, 全部候选)."""
    logger = logger or LOG
    candidates = []

    def _add(install):
        if install and install.launcher:
            candidates.append(install)

    def _resolve_any(value):
        value = os.path.expandvars(os.path.expanduser(str(value).strip().strip('"')))
        if not value:
            return
        if os.path.isfile(value):
            _add(_install_from_launcher(value))
            return
        base = os.path.abspath(value)
        if not os.path.isdir(base):
            return
        launcher = _launcher_in(os.path.join(base, "bin")) or _launcher_in(base)
        if launcher:
            _add(_install_from_launcher(launcher))
            return
        for install in _scan_install_root(base):
            _add(install)

    if explicit:
        _resolve_any(explicit)

    for var in ("VITIS_ROOT", "XILINX_VITIS", "VITIS"):
        value = os.environ.get(var)
        if value:
            _resolve_any(value)

    for name in ("vitis", "vitis.bat"):
        which = shutil.which(name)
        if which:
            _add(_install_from_launcher(which))

    # 注册表里的安装记录最可靠，自定义安装路径也能覆盖
    for install in _registry_candidates():
        _add(install)

    if os.name == "nt":
        for drive in _drives():
            for sub in ("Xilinx\\Vitis", "AMD\\Vitis", "AMDDesignTools",
                        "Program Files\\AMD\\Vitis", "Program Files\\Xilinx\\Vitis",
                        "Xilinx\\Vitis_Embedded"):
                for install in _scan_install_root(os.path.join(drive, sub)):
                    _add(install)
            for install in _scan_drive_root(drive):
                _add(install)
        if not candidates:
            # 还是没找到，再逐层扫一遍（自定义安装目录、装在某一层子目录里的情况）
            for drive in _drives():
                for install in _scan_drive_tree(drive):
                    _add(install)
    else:
        for sub in ("/opt/Xilinx/Vitis", "/tools/Xilinx/Vitis", "/opt/AMD/Vitis"):
            for install in _scan_install_root(sub):
                _add(install)

    unique = {}
    for install in candidates:
        key = os.path.normcase(os.path.abspath(install.launcher))
        if key not in unique:
            unique[key] = install
    candidates = list(unique.values())
    candidates.sort(key=lambda item: item.version_tuple, reverse=True)

    chosen = None
    if candidates:
        chosen = candidates[0]
        if prefer:
            prefer_tuple = _version_tuple(prefer)
            for install in candidates:
                if install.version_tuple == prefer_tuple:
                    chosen = install
                    break
    if logger and candidates:
        pass
    return chosen, candidates


# --------------------------------------------------------------------------- #
# 工作区状态判定
# --------------------------------------------------------------------------- #

def workspace_components_ok(ctx):
    platform_json = os.path.join(ctx.workspace, ctx.platform_name, "vitis-comp.json")
    app_json = os.path.join(ctx.workspace, ctx.app_name, "vitis-comp.json")
    return os.path.isfile(platform_json) and os.path.isfile(app_json)


def read_marker(ctx):
    if not _exists(ctx.marker_path):
        return None
    try:
        return _read_json(ctx.marker_path)
    except Exception:
        return None


def decide_mode(ctx, force_clean=False, force_sync=False):
    """返回 (mode, 原因)；mode 取值 full / sync / none。"""
    if force_clean:
        return "full", "指定了 --clean，强制重新生成"

    marker = read_marker(ctx)
    if not workspace_components_ok(ctx):
        return "full", "工作区不存在或组件不完整"
    if not marker:
        return "full", "缺少工作区记录文件（可能不是本脚本生成的）"

    if os.path.normcase(marker.get("project_root", "")) != os.path.normcase(ctx.root):
        return "full", "工程目录已移动（旧位置 %s）" % marker.get("project_root")
    if (os.path.normcase(marker.get("workspace_requested") or marker.get("workspace") or "")
            != os.path.normcase(ctx.workspace_requested)):
        return "full", "工作区路径已改变（旧位置 %s）" % (marker.get("workspace_requested")
                                                        or marker.get("workspace"))
    if (os.path.normcase(marker.get("workspace_effective") or "")
            != os.path.normcase(ctx.workspace)):
        return "full", "Vitis 使用的工作区路径已改变（旧位置 %s）" % marker.get("workspace_effective")
    if marker.get("manifest_sha256") != ctx.manifest_sha():
        return "full", "工程清单 vitis_project.json 已修改"
    if marker.get("xsa") != ctx.xsa_signature():
        return "full", "XSA 硬件描述已更新"

    if force_sync:
        return "sync", "指定了 --sync，强制同步源码并重新编译"
    if marker.get("sources") != ctx.inventory():
        return "sync", "src/ps 中的源码有改动"
    return "none", "工作区已是最新"


# --------------------------------------------------------------------------- #
# 调度：交给 Vitis 自带的 Python
# --------------------------------------------------------------------------- #

def vitis_module_available():
    try:
        import vitis  # noqa: F401
        return True
    except Exception:
        return False


def _install_from_env():
    root = os.environ.get("XILINX_VITIS", "")
    launcher = _launcher_in(os.path.join(root, "bin")) if root else ""
    return VitisInstall(launcher, root, _version_from_path(root) if root else "")


def relay_to_vitis(install, ctx, mode, args):
    child_args = ["-s", _rel_or_abs(ctx.root, SCRIPT_PATH), "--in-vitis", "--mode", mode]
    if args.workspace:
        child_args += ["--workspace", _rel_or_abs(ctx.root, os.path.abspath(args.workspace))]
    if args.manifest:
        child_args += ["--manifest", _rel_or_abs(ctx.root, os.path.abspath(args.manifest))]
    if args.platform_only:
        child_args += ["--platform-only"]
    if args.no_app_build:
        child_args += ["--no-app-build"]
    if ctx.log_file_path:
        child_args += ["--log-file", _rel_or_abs(ctx.root, ctx.log_file_path)]

    cmd = [install.launcher] + child_args
    LOG.info("[调度] %s" % " ".join(cmd))
    LOG.info("")
    LOG.close_file()
    try:
        return subprocess.call(cmd, cwd=ctx.root, env=_child_env())
    except FileNotFoundError:
        LOG.error("找不到 Vitis 启动器：%s" % install.launcher)
        return 2
    except Exception as exc:
        LOG.error("调用 Vitis 失败：%s" % exc)
        return 2


# --------------------------------------------------------------------------- #
# Vitis 侧：真正建立工程
# --------------------------------------------------------------------------- #

def _signature_params(func):
    try:
        return inspect.signature(func).parameters
    except (TypeError, ValueError):
        return None


def _call_supported(func, kwargs, logger=None):
    """按目标版本的真实签名挑参数，兼容不同 Vitis 版本之间的差异。"""
    logger = logger or LOG
    params = _signature_params(func)
    if params is None or any(p.kind == inspect.Parameter.VAR_KEYWORD for p in params.values()):
        return func(**dict((k, v) for k, v in kwargs.items() if v is not None))

    if "hw_design" in kwargs and "hw_design" not in params and "hw" in params:
        kwargs["hw"] = kwargs.pop("hw_design")

    accepted = {}
    dropped = []
    for key, value in kwargs.items():
        if value is None:
            continue
        if key in params:
            accepted[key] = value
        else:
            dropped.append(key)
    if dropped and logger:
        logger.info_detail("当前 Vitis 不支持参数 %s，已忽略" % ", ".join(dropped))
    return func(**accepted)


def _get_component(client, name, logger):
    try:
        return client.get_component(name=name)
    except Exception as exc:
        logger.warn("get_component('%s') 失败：%s" % (name, exc))
        return None


def _augment_toolchain_path(logger):
    """把 Vitis 自带的 arm-none-eabi 等工具链加到 PATH（批量模式启动器不会加）。"""
    root = os.environ.get("XILINX_VITIS")
    if not root or not os.path.isdir(root):
        return
    extra = []
    for pattern in ("gnu/*/*/*/bin", "gnu/*/*/bin", "gnuwin/*/bin", "tps/win64/*/bin"):
        search = os.path.join(root, *pattern.split("/"))
        for path in glob.glob(search):
            if not os.path.isdir(path):
                continue
            try:
                names = "\n".join(os.listdir(path)).lower()
            except Exception:
                continue
            if "arm-none-eabi-gcc" in names or "aarch64-none-elf-gcc" in names:
                extra.append(path)
    if not extra:
        return
    parts = [p for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    for path in reversed(extra):
        if path not in parts:
            parts.insert(0, path)
    os.environ["PATH"] = os.pathsep.join(parts)
    logger.info_detail("已补充工具链路径：%s" % ", ".join(extra))


def _find_elf(ctx):
    app_dir = os.path.join(ctx.workspace, ctx.app_name)
    direct = os.path.join(app_dir, "build", ctx.app_name + ".elf")
    if os.path.isfile(direct):
        return direct
    matches = glob.glob(os.path.join(app_dir, "**", "*.elf"), recursive=True)
    return matches[0] if matches else None


def _copy_debug_assets(ctx, logger):
    """把平台导出的比特流、调试配置模板放进应用组件，便于直接下载调试。"""
    app_ide = os.path.join(ctx.workspace, ctx.app_name, "_ide")

    sdt_dir = os.path.join(ctx.workspace, ctx.platform_name, "export",
                           ctx.platform_name, "hw", "sdt")
    assets = glob.glob(os.path.join(sdt_dir, "*.bit")) + glob.glob(os.path.join(sdt_dir, "*.mmi"))
    if assets:
        dest = os.path.join(app_ide, "bitstream")
        try:
            if not os.path.isdir(dest):
                os.makedirs(dest)
            for path in assets:
                shutil.copy2(path, os.path.join(dest, os.path.basename(path)))
            logger.ok("已复制 FPGA 比特流到 %s" % dest)
        except Exception as exc:
            logger.warn("复制比特流失败：%s" % exc)

    # 调试时用于初始化 PS 的 ps7_init.tcl（由平台编译生成）
    ps7_init = os.path.join(sdt_dir, "ps7_init.tcl")
    if os.path.isfile(ps7_init):
        dest = os.path.join(app_ide, "psinit")
        try:
            if not os.path.isdir(dest):
                os.makedirs(dest)
            shutil.copy2(ps7_init, os.path.join(dest, "ps7_init.tcl"))
            logger.ok("已复制 PS 初始化脚本 %s" % os.path.join(dest, "ps7_init.tcl"))
        except Exception as exc:
            logger.warn("复制 ps7_init.tcl 失败：%s" % exc)

    template = os.path.join(ctx.src_dir, ctx.manifest.get("debug_launch") or "")
    if os.path.isfile(template):
        try:
            with open(template, "r", encoding="utf-8-sig") as fh:
                text = fh.read()
            text = text.replace("__PLATFORM__", ctx.platform_name)
            text = text.replace("__APP__", ctx.app_name)
            if not os.path.isdir(app_ide):
                os.makedirs(app_ide)
            target = os.path.join(app_ide, "launch.json")
            with open(target, "w", encoding="utf-8") as fh:
                fh.write(text)
            logger.ok("已写入调试配置 %s" % target)
        except Exception as exc:
            logger.warn("写入 launch.json 失败：%s" % exc)


def _import_sources(component, ctx, logger):
    files = [name for name in ctx.source_files()
             if os.path.isfile(os.path.join(ctx.src_dir, name))]
    if not files:
        raise RuntimeError("src/ps 下没有找到任何 .c/.h 源文件")
    _call_supported(component.import_files,
                    dict(from_loc=ctx.src_dir, files=files, dest_dir_in_cmp="src"), logger)
    logger.ok("已导入 %d 个源文件到组件 src/" % len(files))


def _prune_template_sources(ctx, logger):
    """删掉 Vitis 模板自带、但不在 src/ps 列表里的 .c/.h（例如模板的 helloworld.c）。"""
    src = os.path.join(ctx.workspace, ctx.app_name, "src")
    if not os.path.isdir(src):
        return
    keep = set(name.lower() for name in ctx.source_files())
    removed = []
    for name in sorted(os.listdir(src)):
        if not name.lower().endswith(SRC_EXTENSIONS):
            continue
        if name.lower() in keep:
            continue
        path = os.path.join(src, name)
        if not os.path.isfile(path):
            continue
        try:
            os.remove(path)
            removed.append(name)
        except Exception:
            pass
    if removed:
        logger.ok("已移除模板自带的多余源文件：%s" % ", ".join(removed))


def _apply_app_config(component, ctx, logger):
    app = ctx.manifest.get("app") or {}
    sources = ctx.compile_sources()
    if not sources:
        raise RuntimeError("src/ps 里没有任何 .c 源文件，无法编译")

    entries = [("USER_COMPILE_SOURCES", sources)]
    libraries = app.get("link_libraries") or []
    if libraries:
        entries.append(("USER_LINK_LIBRARIES", list(libraries)))
    optimization = app.get("optimization")
    if optimization:
        entries.append(("USER_COMPILE_OPTIMIZATION_LEVEL", [optimization]))

    include_dirs = _driver_include_dirs(ctx)
    if include_dirs:
        entries.append(("USER_INCLUDE_DIRECTORIES", include_dirs))

    for key, values in entries:
        try:
            component.set_app_config(key=key, values=values)
        except Exception as exc:
            logger.warn("设置 %s 失败：%s" % (key, exc))
    if include_dirs:
        logger.ok("已加入 %d 个自定义 IP 驱动头文件目录" % len(include_dirs))


def _driver_include_dirs(ctx):
    """平台导出的自定义 IP 驱动头文件目录（<export>/hw/sdt/drivers/<ip>/src）。"""
    base = os.path.join(ctx.workspace, ctx.platform_name, "export",
                        ctx.platform_name, "hw", "sdt", "drivers")
    dirs = [d for d in sorted(glob.glob(os.path.join(base, "*", "src"))) if os.path.isdir(d)]
    return dirs


def _sanitize_includes(directory, logger, label="源码"):
    """把 #include 里残留的绝对路径改成只保留文件名，返回被改动的文件列表。"""
    changed = []
    if not os.path.isdir(directory):
        return changed
    for name in sorted(os.listdir(directory)):
        if not name.lower().endswith(SRC_EXTENSIONS):
            continue
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "rb") as fh:
                data = fh.read()
        except Exception as exc:
            logger.warn("读取 %s 失败：%s" % (path, exc))
            continue
        new_data, count = _ABS_INCLUDE_RE.subn(br"\1\3\4", data)
        if count and new_data != data:
            try:
                with open(path, "wb") as fh:
                    fh.write(new_data)
                changed.append(name)
            except Exception as exc:
                logger.warn("改写 %s 失败：%s" % (path, exc))
    if changed:
        logger.ok("已把 %s 中 %d 个文件的绝对路径 #include 改为文件名引用：%s"
                  % (label, len(changed), ", ".join(changed[:10])
                     + (" ..." if len(changed) > 10 else "")))
    return changed


def _apply_ld_script(ctx, component, logger):
    app = ctx.manifest.get("app") or {}
    stack = app.get("stack_size")
    heap = app.get("heap_size")
    if not stack and not heap:
        return
    ld_path = os.path.join(ctx.workspace, ctx.app_name, "src", "lscript.ld")
    if not os.path.isfile(ld_path):
        logger.warn("没有找到 lscript.ld，跳过栈/堆大小设置")
        return
    try:
        lscript = component.get_ld_script(path=ld_path)
        if stack:
            lscript.set_stack_size(str(stack))
        if heap:
            lscript.set_heap_size(str(heap))
        logger.ok("链接脚本栈/堆大小：%s / %s" % (stack, heap))
    except Exception as exc:
        logger.warn("设置链接脚本失败：%s" % exc)


def _build(component, label, logger):
    logger.info("      正在编译 %s（可能需要几分钟）..." % label)
    try:
        status = component.build()
    except Exception as exc:
        raise RuntimeError("%s 编译失败：%s" % (label, exc))
    if isinstance(status, int) and status != 0:
        raise RuntimeError("%s 编译失败（Vitis 返回码 %d）" % (label, status))


def _bsp_build_dirs(ctx):
    """找到平台里各 domain 的 BSP 构建目录（.../bsp/libsrc/build_configs/gen_bsp）。"""
    platform_dir = os.path.join(ctx.workspace, ctx.platform_name)
    found = []
    if not os.path.isdir(platform_dir):
        return found
    try:
        domains = glob.glob(os.path.join(platform_dir, "*", "*"))
    except Exception:
        return found
    for domain_dir in domains:
        gen = os.path.join(domain_dir, "bsp", "libsrc", "build_configs", "gen_bsp")
        if os.path.isdir(gen):
            found.append(gen)
    return found


def _reset_bsp_build(ctx, logger):
    """删掉 BSP 的 ninja 构建缓存，让它下次重新 configure。

    Vitis 2026.1 的 BSP 构建用 `cmake --build .`，一旦 build.ninja 里引用的
    CMakeFiles/rules.ninja 丢了，ninja 会先报“系统找不到指定的文件。”（GBK），
    Vitis 的 helper 又用严格 UTF-8 解码这段输出，于是抛 UnicodeDecodeError，
    最终只显示一句 `Error in generating platform "xxx".`。
    这种情况下把 gen_bsp 清掉，下一次构建会重新跑 cmake configure。"""
    removed = False
    for gen in _bsp_build_dirs(ctx):
        if _rmtree(gen, logger):
            logger.ok("已清理 BSP 构建缓存：%s" % gen)
            removed = True
    return removed


def _resolve_component(client, name, created, logger):
    component = _get_component(client, name, logger)
    if component is None:
        component = created
    if component is None or not hasattr(component, "build"):
        raise RuntimeError("无法获取组件对象：%s" % name)
    return component


def _patch_server_start(logger):
    """Vitis 2026.1 的 _server.Server.startServer 用严格 UTF-8 解码服务端输出。
    服务端启动时只要先打印一行本地编码（GBK）的提示（例如“系统找不到指定的文件。”），
    create_client 就会以 UnicodeDecodeError 失败。这里用同样逻辑、但容错解码的版本替换它，
    只在运行时生效，不改动安装目录。"""
    try:
        import platform as _platform
        from vitis import _server as vitis_server
        from vitis import _utils as vitis_utils

        if getattr(vitis_server.Server, "_dsh_start_patch", False):
            return True

        def start_server(self):
            root = os.getenv("XILINX_VITIS")
            binary = (root + "/bin/vitis-server") if root else ""
            if _platform.system() == "Windows":
                binary += ".bat"
            if not os.path.exists(binary):
                raise vitis_utils.exception(
                    msg="Unable to start Vitis Server, cannot find '%s'" % binary)
            cmd = os.path.abspath(binary)
            if self.serverPort is not None:
                cmd += " -p " + str(self.serverPort)
            kwargs = dict(shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if _platform.system() == "Windows":
                kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                kwargs["start_new_session"] = True
            self.cp = subprocess.Popen(cmd, **kwargs)
            if self.serverPort is None:
                for raw in iter(self.cp.stdout.readline, b""):
                    line = raw.decode("utf-8", "replace")
                    if "serverPort" in line:
                        try:
                            payload = re.split(r"\{(.*?)\}", line)[1]
                            self.serverPort = int(json.loads("{" + payload + "}")["serverPort"])
                        except Exception:
                            continue
                        break
            return True

        vitis_server.Server.startServer = start_server
        vitis_server.Server._dsh_start_patch = True
        logger.info_detail("已启用 Vitis 服务端兼容补丁（非 UTF-8 输出不再导致失败）")
        return True
    except Exception as exc:
        logger.warn("应用服务端兼容补丁失败：%s" % exc)
        return False


def _clear_client_singleton():
    try:
        from vitis import cli_client
        cli_client._Singleton._instances.clear()
    except Exception:
        pass


def _create_client(vitis, logger, attempts=3):
    """创建 Vitis 客户端。服务端启动偶发失败（超时/编码问题）时自动重试并打兼容补丁。"""
    os.environ.setdefault("VITIS_SERVER_START_TIMEOUT", "180")
    last = None
    for attempt in range(attempts):
        try:
            return vitis.create_client()
        except Exception as exc:
            last = exc
            text = str(exc).strip() or exc.__class__.__name__
            text = text.splitlines()[0]
            logger.warn("连接 Vitis 服务失败（第 %d/%d 次）：%s" % (attempt + 1, attempts, text))
            lowered = text.lower()
            if ("codec" in lowered or "decode" in lowered or "utf-8" in lowered
                    or "timeout" in lowered):
                _patch_server_start(logger)
            try:
                vitis.dispose()
            except Exception:
                pass
            _clear_client_singleton()
            if attempt + 1 < attempts:
                time.sleep(3)
    raise last


def phase_vitis(ctx, install, mode, args):
    """在 Vitis 自带 Python 环境中执行：建平台、建应用、编译。"""
    try:
        import vitis
    except Exception as exc:
        LOG.error("无法导入 vitis 模块：%s" % exc)
        return 2

    platform = ctx.manifest.get("platform") or {}
    app = ctx.manifest.get("app") or {}

    if mode == "full":
        steps = ["创建平台组件", "编译平台（BSP / FSBL / xpfm）", "创建应用组件"]
    else:
        steps = ["打开现有工作区"]
    steps += ["导入 src/ps 源码", "写入编译配置", "编译应用"]
    total = len(steps)
    index = [0]

    def step(text):
        index[0] += 1
        LOG.step(index[0], total, text)

    LOG.raw("")
    LOG.raw("-" * 60)
    LOG.raw("Vitis 环境：%s" % (os.environ.get("XILINX_VITIS") or install.root or "未知"))
    LOG.raw("Python    ：%s" % sys.version.split()[0])
    LOG.raw("-" * 60)

    _augment_toolchain_path(LOG)

    client = None
    try:
        if not os.path.isdir(ctx.workspace):
            os.makedirs(ctx.workspace)

        try:
            client = _create_client(vitis, LOG)
        except Exception as exc:
            detail = str(exc).strip().splitlines()
            LOG.error("无法连接 Vitis 服务：%s" % (detail[0] if detail else "未知错误"))
            LOG.info("请先关闭所有 Vitis IDE 窗口后重试；也可以先手动打开一次 Vitis IDE，再运行本脚本。")
            raise

        if mode == "full":
            # 等服务端确认可用后再删旧工作区：Vitis 起不来时不会白白丢掉现有工作区
            LOG.info("清理旧工作区：%s" % ctx.workspace)
            if not _rmtree(ctx.workspace, LOG):
                LOG.error("无法删除旧工作区，可能正被 Vitis IDE 占用，请先关闭它再重试。")
                return 1
            if not os.path.isdir(ctx.workspace):
                os.makedirs(ctx.workspace)

        client.set_workspace(path=ctx.workspace)
        if mode != "full":
            LOG.info("已连接工作区：%s" % ctx.workspace)

        if mode == "full":
            step("创建平台组件 %s（XSA：%s）" % (ctx.platform_name, os.path.basename(ctx.xsa)))
            created = _call_supported(client.create_platform_component, dict(
                name=ctx.platform_name,
                hw_design=ctx.xsa_effective,
                desc=platform.get("description") or "",
                os=platform.get("os") or "standalone",
                cpu=platform.get("cpu") or "ps7_cortexa9_0",
                domain_name=platform.get("domain") or ctx.domain,
                compiler=platform.get("compiler") or "gcc",
                no_boot_bsp=bool(platform.get("no_boot_bsp")),
            ), LOG)
            LOG.ok("平台组件已创建")

            step("编译平台（BSP / FSBL / xpfm）")
            platform_obj = _resolve_component(client, ctx.platform_name, created, LOG)
            try:
                _build(platform_obj, "平台 " + ctx.platform_name, LOG)
            except RuntimeError as exc:
                LOG.warn("平台编译失败：%s" % exc)
                LOG.info("这通常是 BSP 的 ninja 构建缓存坏了（CMakeFiles/rules.ninja 丢失），")
                LOG.info("Vitis 又会把 ninja 的中文报错按 UTF-8 解码，最后只剩一句 "
                         "Error in generating platform。")
                if _reset_bsp_build(ctx, LOG):
                    LOG.info("已清理 BSP 构建缓存，重试一次 ...")
                    _build(platform_obj, "平台 " + ctx.platform_name, LOG)
                else:
                    LOG.error("没找到可清理的 BSP 构建缓存，请用 --clean 重新生成工作区。")
                    return 1
            xpfm = os.path.join(ctx.workspace, ctx.platform_name, "export",
                                ctx.platform_name, ctx.platform_name + ".xpfm")
            if not os.path.isfile(xpfm):
                LOG.error("平台没有导出 xpfm，请查看 %s 下的编译日志"
                          % os.path.join(ctx.workspace, ctx.platform_name, "logs"))
                return 1
            LOG.ok("平台导出文件：%s" % xpfm)

            if args.platform_only:
                LOG.raw("")
                LOG.info("按 --platform-only 要求，已停在平台阶段。")
                return 0

            step("创建应用组件 %s（模板 %s）"
                 % (ctx.app_name, app.get("template") or "empty_application"))
            created = _call_supported(client.create_app_component, dict(
                name=ctx.app_name,
                platform=xpfm,
                domain=ctx.domain,
                template=app.get("template") or "empty_application",
            ), LOG)
            component = _resolve_component(client, ctx.app_name, created, LOG)
            LOG.ok("应用组件已创建")
        else:
            step("打开现有工作区 %s" % ctx.workspace)
            component = _get_component(client, ctx.app_name, LOG)
            if component is None or not hasattr(component, "build"):
                LOG.error("工作区里没有应用组件 %s，请用 --clean 重新生成" % ctx.app_name)
                return 1
            LOG.ok("已获取应用组件")

        step("导入 src/ps 源码（%d 个文件）" % len(ctx.source_files()))
        _import_sources(component, ctx, LOG)
        _prune_template_sources(ctx, LOG)
        _sanitize_includes(os.path.join(ctx.workspace, ctx.app_name, "src"), LOG, "工作区源码")

        step("写入编译配置（源文件列表、库、优化级别、栈/堆）")
        _apply_app_config(component, ctx, LOG)
        _apply_ld_script(ctx, component, LOG)

        if args.no_app_build:
            LOG.step(total, total, "跳过应用编译（--no-app-build）")
            elf = None
        else:
            step("编译应用 %s" % ctx.app_name)
            component = _resolve_component(client, ctx.app_name, component, LOG)
            _build(component, "应用 " + ctx.app_name, LOG)
            elf = _find_elf(ctx)
            if not elf:
                LOG.error("编译结束但没有找到 %s.elf，请查看 %s 下的 build.log"
                          % (ctx.app_name, os.path.join(ctx.workspace, ctx.app_name, "build")))
                return 1
            LOG.ok("生成 ELF：%s（%s）" % (elf, _human_size(os.path.getsize(elf))))

        _copy_debug_assets(ctx, LOG)

        marker = {
            "schema": 1,
            "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
            "project_root": ctx.root,
            "workspace": ctx.workspace_requested,
            "workspace_requested": ctx.workspace_requested,
            "workspace_effective": ctx.workspace,
            "alias": ctx.workspace_alias,
            "vitis_version": (install.version
                              or os.environ.get("XILINX_VERSION_VITIS", "")),
            "vitis_launcher": install.launcher,
            "manifest_sha256": ctx.manifest_sha(),
            "xsa": ctx.xsa_signature(),
            "sources": ctx.inventory(),
            "platform": ctx.platform_name,
            "app": ctx.app_name,
            "elf": elf or "",
        }
        _write_json(ctx.marker_path, marker)
        LOG.ok("已记录工作区信息 %s" % ctx.marker_path)

        LOG.raw("")
        LOG.info("工作区准备完成：%s" % ctx.workspace)
        return 0
    except Exception:
        LOG.raw("")
        LOG.error("生成工作区时出错：")
        LOG.raw(traceback.format_exc())
        return 1
    finally:
        server_pid = None
        try:
            if client is not None:
                proc = getattr(getattr(client, "_serverObj", None), "cp", None)
                server_pid = getattr(proc, "pid", None)
        except Exception:
            server_pid = None
        try:
            if client is not None:
                vitis.dispose()
        except Exception:
            pass
        # Vitis 服务端 dispose 后还会挂约 60 秒，期间占着工作区锁；
        # 不结束它，紧接着打开 IDE 就会弹 "already in use"。
        _stop_server_process(server_pid, LOG)


# --------------------------------------------------------------------------- #
# 把工作区里的源码同步回 src/ps
# --------------------------------------------------------------------------- #

def fix_bsp(ctx):
    """只清理 BSP 的 ninja 构建缓存，不动源码和工程。

    适用于：IDE 里点“编译平台”只报 `Error in generating platform 'xxx'.`
    （真正原因是 ninja 找不到 CMakeFiles/rules.ninja，而这段中文报错又被
    Vitis 按 UTF-8 解码，抛了 UnicodeDecodeError）。"""
    dirs = _bsp_build_dirs(ctx)
    if not dirs:
        LOG.error("没找到 BSP 构建目录，工作区可能还没生成：%s" % ctx.workspace)
        return 1
    for gen in dirs:
        if _rmtree(gen, LOG):
            LOG.ok("已清理 BSP 构建缓存：%s" % gen)
    LOG.raw("")
    LOG.info("请重新编译：运行 build\\create_vitis_workspace.bat --sync，")
    LOG.info("或在 Vitis IDE 里重新点一次“编译平台”。")
    return 0


def fix_sources(ctx):
    """把 src/ps 里硬编码绝对路径的 #include 改成文件名引用（原地修改）。"""
    changed = _sanitize_includes(ctx.src_dir, LOG, "src/ps")
    LOG.raw("")
    if changed:
        LOG.ok("已修正 %d 个文件，现在工程可以整体移动了" % len(changed))
    else:
        LOG.ok("src/ps 里没有绝对路径的 #include，无需修改")
    return 0


def _dir_size(path):
    total = 0
    try:
        for root, _dirs, files in os.walk(path):
            for name in files:
                try:
                    total += os.path.getsize(os.path.join(root, name))
                except Exception:
                    pass
    except Exception:
        pass
    return total


def _drop_alias(alias, logger):
    """无条件删除短路径联接点（只删链接本身，不动目标）。"""
    if not alias or not os.path.isdir(alias):
        return
    if not _is_reparse_point(alias):
        return
    try:
        os.rmdir(alias)
        logger.ok("已删除短路径联接点：%s" % alias)
    except Exception as exc:
        logger.warn("删除联接点失败 %s：%s" % (alias, exc))
        return
    parent = os.path.dirname(alias)
    try:
        if parent and os.path.isdir(parent) and not os.listdir(parent):
            os.rmdir(parent)
            logger.info_detail("已删除空的 %s" % parent)
    except Exception:
        pass


def purge_workspace(ctx, force=False):
    """一键清理：删除 Vitis 工作区、状态记录文件、短路径联接点。

    只清生成物，不动 src/ps 和 src/pl。"""
    lock = _workspace_lock_path(ctx.workspace)
    if _vitis_ide_running() and os.path.exists(lock) and not force:
        LOG.error("Vitis IDE 正在使用这个工作区，请先关闭 IDE 再清理。")
        LOG.info("如果确定要强制清理，加 --force（可能失败）。")
        return 2

    LOG.raw("")
    LOG.info("工作区：%s" % ctx.workspace_requested)
    if ctx.workspace_alias:
        LOG.info("短路径：%s" % ctx.workspace)

    # 兼容曾经出现过的“真实工作区在短路径、工程里是联接点”的布局：
    # 先摘掉工程里的联接点（只删链接），再删它指向的真实目录
    if os.path.isdir(ctx.workspace) and _is_reparse_point(ctx.workspace):
        target = _link_target(ctx.workspace)
        try:
            os.rmdir(ctx.workspace)
            LOG.ok("已删除工程里的工作区联接点：%s" % ctx.workspace_link or ctx.workspace)
        except Exception as exc:
            LOG.warn("删除联接点失败：%s" % exc)
        if target and os.path.isdir(target):
            _rmtree(target, LOG)

    freed = 0
    if os.path.isdir(ctx.workspace):
        freed = _dir_size(ctx.workspace)
        LOG.info("正在删除工作区（%s）..." % _human_size(freed))
        if not _rmtree(ctx.workspace, LOG):
            LOG.error("删除失败：文件仍被占用。")
            LOG.info("请关闭 Vitis IDE / 程序 / 资源管理器预览窗格后重试，")
            LOG.info("或直接删 board 目录下的 Vitis_WorkSpace（必要时用 --force）。")
            return 1
        LOG.ok("工作区已删除，释放约 %s" % _human_size(freed))
    else:
        LOG.ok("工作区不存在，无需删除")

    if os.path.isfile(ctx.marker_path):
        try:
            os.remove(ctx.marker_path)
            LOG.ok("已删除状态记录：%s" % os.path.basename(ctx.marker_path))
        except Exception as exc:
            LOG.warn("删除状态记录失败：%s" % exc)

    # 短路径联接点（以及空的 <盘>:\vws）
    if ctx.workspace_alias:
        if _is_reparse_point(ctx.workspace_alias):
            _drop_alias(ctx.workspace_alias, LOG)
        elif os.path.isdir(ctx.workspace_alias):
            _rmtree(ctx.workspace_alias, LOG)
            LOG.ok("已删除短路径目录：%s" % ctx.workspace_alias)
            parent = os.path.dirname(ctx.workspace_alias)
            try:
                if parent and os.path.isdir(parent) and not os.listdir(parent):
                    os.rmdir(parent)
                    LOG.info_detail("已删除空的 %s" % parent)
            except Exception:
                pass

    # 日志目录（每次生成都会重建，属于可丢弃的残留）
    # 注意：先把脚本自己的日志文件句柄关掉，否则 Windows 下删不掉这个目录
    logs_dir = os.path.join(ctx.out_dir, "logs")
    if os.path.isdir(logs_dir):
        LOG.close_file()
        if _rmtree(logs_dir, LOG):
            LOG.ok("已删除日志目录：board\\logs")
        else:
            LOG.warn("日志目录删除失败：%s" % logs_dir)

    lock_file = os.path.join(ctx.out_dir, ".vitis_create.lock")
    if os.path.isfile(lock_file):
        try:
            os.remove(lock_file)
            LOG.ok("已删除实例锁文件：.vitis_create.lock")
        except Exception as exc:
            LOG.warn("删除实例锁文件失败：%s" % exc)

    LOG.raw("")
    LOG.info("清理完成。下次运行 create_vitis_workspace.bat 会重新生成工作区。")
    return 0


def pull_sources(ctx):
    app_src = os.path.join(ctx.workspace, ctx.app_name, "src")
    if not os.path.isdir(app_src):
        LOG.error("工作区里找不到应用源码目录：%s" % app_src)
        LOG.info("请先运行：python build/create_vitis_workspace.py")
        return 1

    updated, added = [], []
    for name in sorted(os.listdir(app_src)):
        if not name.lower().endswith(SRC_EXTENSIONS):
            continue
        source = os.path.join(app_src, name)
        if not os.path.isfile(source):
            continue
        target = os.path.join(ctx.src_dir, name)
        if not _exists(target):
            shutil.copy2(source, target)
            added.append(name)
        elif _sha256(source) != _sha256(target):
            shutil.copy2(source, target)
            updated.append(name)

    LOG.raw("")
    if updated:
        LOG.ok("已更新：%s" % ", ".join(updated))
    if added:
        LOG.ok("新增  ：%s" % ", ".join(added))
    if not updated and not added:
        LOG.ok("src/ps 与工作区源码一致，无需同步")
    LOG.info("如需让工作区重新编译，请运行：python build/create_vitis_workspace.py --sync")
    return 0


# --------------------------------------------------------------------------- #
# 体检报告
# --------------------------------------------------------------------------- #

def report(ctx, install, candidates, mode, reason):
    LOG.raw("")
    LOG.raw("=" * 60)
    LOG.raw(" Vitis 工作区体检")
    LOG.raw("=" * 60)
    LOG.info("工程根目录：%s" % ctx.root)
    LOG.info("源码目录  ：%s" % ctx.src_dir)

    sources = ctx.source_files()
    c_count = len([n for n in sources if n.lower().endswith(".c")])
    h_count = len([n for n in sources if n.lower().endswith(".h")])
    LOG.info("源文件    ：%d 个（.c %d / .h %d）" % (len(sources), c_count, h_count))
    missing = [n for n in sources if not _exists(os.path.join(ctx.src_dir, n))]
    if missing:
        LOG.warn("清单里有 %d 个文件不存在：%s" % (len(missing), ", ".join(missing[:8])))
    extra = ctx.unlisted_sources()
    if extra:
        LOG.warn("src/ps 里有 %d 个文件未写入清单，不会参与编译：%s"
                 % (len(extra), ", ".join(extra[:8])))

    if _exists(ctx.xsa):
        LOG.info("XSA       ：%s（%s）" % (ctx.xsa, _human_size(os.path.getsize(ctx.xsa))))
    else:
        LOG.error("XSA 不存在：%s" % ctx.xsa)

    LOG.info("工程清单  ：%s" % ctx.manifest_path)
    LOG.info("目标版本  ：Vitis %s" % ctx.target_version)
    LOG.info("工作区    ：%s" % ctx.workspace_requested)
    if ctx.workspace_alias:
        LOG.info("Vitis 路径：%s（短路径联接点）" % ctx.workspace)
        LOG.info("联接点    ：%s -> %s" % (ctx.workspace_alias,
                                          os.path.dirname(ctx.workspace_requested)))
    state = {"full": "需要重新生成", "sync": "需要同步源码", "none": "已就绪"}.get(mode, mode)
    LOG.info("状态      ：%s（%s）" % (state, reason))

    LOG.raw("")
    if install:
        LOG.info("使用 Vitis：%s" % install)
        if install.version_tuple and install.version_tuple < MIN_UNIFIED_VERSION:
            LOG.warn("该版本低于 Vitis 2024.1，可能不支持 Unified IDE 工程")
        elif ctx.target_version and install.version_tuple != _version_tuple(ctx.target_version):
            LOG.warn("工程面向 %s，当前找到的是 %s；如需指定请用 --vitis"
                     % (ctx.target_version, install.version or "未知"))
    else:
        LOG.error("没有找到 Vitis")
        LOG.info("已找过：VITIS_ROOT / XILINX_VITIS、PATH、注册表安装记录、各磁盘常见目录。")
        LOG.info("请用 --vitis 指定安装目录（目录下应有 bin\\vitis.bat），例如：")
        LOG.info("    --vitis \"D:\\2026.1\\Vitis\"")
        LOG.info("或先设置环境变量：set VITIS_ROOT=D:\\2026.1\\Vitis")

    if candidates:
        LOG.raw("")
        LOG.info("检测到的 Vitis 安装：")
        for item in candidates[:8]:
            LOG.info("      %-8s -> %s" % (item.version or "?", item.launcher))
    LOG.raw("=" * 60)
    return 0 if install else 2


# --------------------------------------------------------------------------- #
# 入口
# --------------------------------------------------------------------------- #

class InstanceLock(object):
    """同一时间只允许一个脚本实例在跑（双击两次会互相破坏工作区）。
    用系统文件锁实现，进程退出后自动释放，不会留下死锁。"""

    def __init__(self, path):
        self.path = path
        self._fh = None

    def acquire(self):
        try:
            self._fh = open(self.path, "a+b")
        except Exception:
            return True  # 打不开锁文件就不阻塞用户
        try:
            self._fh.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self._fh.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self._fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False
        except Exception:
            return True

    def release(self):
        if self._fh is None:
            return
        try:
            self._fh.seek(0)
            if os.name == "nt":
                import msvcrt
                try:
                    msvcrt.locking(self._fh.fileno(), msvcrt.LK_UNLCK, 1)
                except Exception:
                    pass
            else:
                import fcntl
                try:
                    fcntl.flock(self._fh.fileno(), fcntl.LOCK_UN)
                except Exception:
                    pass
            self._fh.close()
        except Exception:
            pass
        self._fh = None


def build_parser():
    parser = argparse.ArgumentParser(
        prog="create_vitis_workspace.py",
        description="按 src/ps 中的输入文件，用 Vitis 重新生成 board/Vitis_WorkSpace 工作区。",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument("--vitis", metavar="PATH",
                        help="Vitis 安装目录（如 D:\\2026.1\\Vitis），或 vitis.bat 的完整路径")
    parser.add_argument("--workspace", metavar="DIR",
                        help="工作区目录，默认 <工程>\\board\\Vitis_WorkSpace")
    parser.add_argument("--manifest", metavar="FILE",
                        help="工程清单，默认 <工程>\\src\\ps\\vitis_project.json")
    parser.add_argument("--check", action="store_true", help="只做体检，不生成工作区")
    parser.add_argument("--clean", action="store_true", help="删除已有工作区并重新生成")
    parser.add_argument("--sync", action="store_true", help="只同步源码并重新编译")
    parser.add_argument("--pull", action="store_true", help="把工作区里改过的源码同步回 src/ps")
    parser.add_argument("--fix-sources", action="store_true",
                        help="把 src/ps 里硬编码绝对路径的 #include 改成文件名引用")
    parser.add_argument("--fix-bsp", action="store_true",
                        help="只清理 BSP 的 ninja 构建缓存（编译平台报错时用）")
    parser.add_argument("--purge", action="store_true",
                        help="清理：删除生成的 Vitis 工作区、状态记录和短路径联接点（不动源码）")
    parser.add_argument("--force", action="store_true",
                        help="配合 --purge：即使检测到 Vitis IDE 在运行也强行清理")
    parser.add_argument("-y", "--yes", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--no-open", action="store_true", help="生成后不自动打开 Vitis IDE")
    parser.add_argument("--no-app-build", action="store_true", help="只建工程，不编译应用")
    parser.add_argument("--platform-only", action="store_true", help="只创建并编译平台")
    parser.add_argument("--in-vitis", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--mode", choices=("full", "sync", "none"), help=argparse.SUPPRESS)
    parser.add_argument("--log-file", help=argparse.SUPPRESS)
    return parser


def main(argv=None):
    _enable_console_errors()
    args = build_parser().parse_args(argv)

    ctx = Context(PROJECT_ROOT, args.manifest, args.workspace)
    ctx.log_file_path = args.log_file or ctx.new_log_path()
    log_init(ctx.log_file_path)

    lock = None
    try:
        if not args.in_vitis and not args.purge:
            lock = InstanceLock(os.path.join(ctx.out_dir, ".vitis_create.lock"))
            if not lock.acquire():
                LOG.raw("")
                LOG.error("已经有一个本脚本的实例在运行（或上次异常退出后残留了锁）。")
                LOG.info("请等它跑完再试；确认没有在跑的话，删除这个文件后重试：")
                LOG.info("    %s" % lock.path)
                return 3
        return _run(args, ctx)
    except KeyboardInterrupt:
        LOG.raw("")
        LOG.warn("用户中断")
        return 130
    finally:
        if lock is not None:
            lock.release()


def _result_path(ctx):
    return os.path.join(ctx.out_dir, RESULT_NAME)


def _run(args, ctx):
    # ---- 子进程（由 Vitis 启动）----
    if args.in_vitis:
        install = _install_from_env()
        resolve_workspace(ctx, LOG)
        mode = args.mode or decide_mode(ctx, args.clean, args.sync)[0]
        rc = phase_vitis(ctx, install, mode, args)
        try:
            _write_json(_result_path(ctx),
                        {"rc": rc,
                         "finished": datetime.datetime.now().isoformat(timespec="seconds")})
        except Exception:
            pass
        return rc

    inside_vitis = vitis_module_available()
    resolve_workspace(ctx, LOG)

    # 路径过长又没有短路径联接点时，直接沿用长路径去建平台一定会编译失败
    # （CMake 的编译器自检就过不去），所以必须先停下来并告诉用户怎么修。
    if ctx.workspace_blocked and not args.purge:
        LOG.raw("")
        LOG.error("工作区路径过长，但短路径联接点建立失败：%s" % ctx.workspace_blocked)
        LOG.info("这会导致编译平台时报错（CMake/编译器路径超过 260 字符）。")
        LOG.info("请先删掉这个残留目录，然后重新运行本脚本：")
        LOG.info("    rd /s /q \"%s\"" % ctx.workspace_blocked)
        LOG.info("（如果里面还有需要的文件，请先备份；删掉后脚本会自动重建联接点）")
        return 2

    mode, reason = decide_mode(ctx, force_clean=args.clean, force_sync=args.sync)

    if args.check:
        install, candidates = find_vitis(args.vitis, ctx.target_version, LOG)
        return report(ctx, install, candidates, mode, reason)

    if args.fix_sources:
        return fix_sources(ctx)

    if args.fix_bsp:
        return fix_bsp(ctx)

    if args.purge:
        return purge_workspace(ctx, force=args.force)

    if args.pull:
        return pull_sources(ctx)

    if inside_vitis:
        install = _install_from_env()
        candidates = [install] if install.launcher else []
    else:
        install, candidates = find_vitis(args.vitis, ctx.target_version, LOG)

    LOG.raw("")
    LOG.raw("=" * 60)
    LOG.raw(" Vitis 工作区生成器")
    LOG.raw("=" * 60)
    LOG.info("工程目录：%s" % ctx.root)
    LOG.info("源码目录：%s" % ctx.src_dir)
    if ctx.workspace_alias:
        LOG.info("工作区  ：%s" % ctx.workspace_requested)
        LOG.info("          （路径过长或含特殊字符，Vitis 通过短路径 %s 访问）" % ctx.workspace)
        LOG.info("           联接点 %s -> %s" % (ctx.workspace_alias,
                                              os.path.dirname(ctx.workspace_requested)))
    else:
        LOG.info("工作区  ：%s" % ctx.workspace)
    if install and install.launcher:
        LOG.info("Vitis   ：%s" % install)
    elif inside_vitis:
        LOG.info("Vitis   ：当前 Vitis 环境（%s）" % (os.environ.get("XILINX_VITIS") or "未知"))
    else:
        LOG.error("没有找到可用的 Vitis 安装")
        LOG.info("已经找过：环境变量 VITIS_ROOT / XILINX_VITIS、PATH、注册表安装记录、各磁盘常见目录。")
        LOG.info("请用 --vitis 指定（指向安装目录，或 bin\\vitis.bat 的完整路径都可以）：")
        LOG.info("    python build\\create_vitis_workspace.py --vitis \"D:\\2026.1\\Vitis\"")
        LOG.info("    build\\create_vitis_workspace.bat --vitis \"D:\\2026.1\\Vitis\"")
        LOG.info("也可以先设置环境变量再运行：set VITIS_ROOT=D:\\2026.1\\Vitis")
        LOG.info("判断方法：Vitis 安装目录里应该有 bin\\vitis.bat。")
        return 2
    LOG.info("日志文件：%s" % ctx.log_file_path)
    if (install and install.version and ctx.target_version
            and install.version_tuple != _version_tuple(ctx.target_version)):
        LOG.warn("工程面向 Vitis %s，当前使用 %s" % (ctx.target_version, install.version))
    LOG.info("状态    ：%s（%s）" % ({"full": "全新生成", "sync": "同步源码",
                                    "none": "无需重建"}.get(mode, mode), reason))

    if mode == "full":
        old_marker = read_marker(ctx)
        old_alias = (old_marker or {}).get("alias") or ""
        if old_alias and os.path.normcase(old_alias) != os.path.normcase(ctx.workspace_alias):
            # 旧版本留下的联接点/短目录，清掉
            if _is_reparse_point(old_alias):
                _remove_junction(old_alias, LOG)
            elif os.path.isdir(old_alias):
                _rmtree(old_alias, LOG)

    if mode in ("full", "sync"):
        if inside_vitis:
            rc = phase_vitis(ctx, install, mode, args)
        else:
            result = _result_path(ctx)
            if _exists(result):
                try:
                    os.remove(result)
                except Exception:
                    pass
            rc = relay_to_vitis(install, ctx, mode, args)
            log_init(ctx.log_file_path)
            if _exists(result):
                try:
                    rc = int(_read_json(result).get("rc", rc))
                except Exception:
                    pass
            elif rc == 0:
                LOG.warn("没有收到 Vitis 子进程的结果标记，按失败处理")
                rc = 1
        if rc != 0:
            LOG.raw("")
            LOG.error("工作区生成失败（返回码 %s），日志：%s" % (rc, ctx.log_file_path))
            LOG.info("常见原因：① 已经打开着 Vitis IDE，请先把它关掉再运行；")
            LOG.info("          ② Vitis 服务首次启动被安全软件拦截；③ 机器繁忙导致服务启动超时。")
            LOG.info("可以直接再运行一次；反复失败的话，先重启电脑再试。")
            return rc
    else:
        LOG.info("工作区已就绪，跳过生成步骤。")

    if not args.no_open:
        open_ide(install, ctx)

    LOG.raw("")
    if ctx.workspace_alias:
        LOG.info("完成。工作区：%s" % ctx.workspace_requested)
        LOG.info("      （Vitis 通过短路径 %s 访问，内容相同）" % ctx.workspace)
    else:
        LOG.info("完成。工作区：%s" % ctx.workspace)
    return 0


def _child_env():
    """启动 Vitis 时的环境变量：去掉会让 Electron 程序按 Node 运行的变量
    （从 VS Code/其它 Electron 终端里运行时会被继承，导致 IDE 起不来）。"""
    env = dict(os.environ)
    for key in ("ELECTRON_RUN_AS_NODE", "ELECTRON_NO_ATTACH_CONSOLE", "ELECTRON_ENABLE_LOGGING"):
        env.pop(key, None)
    return env


def _ide_executable(install):
    """Vitis Unified IDE 的可执行文件（直接启动它，比嵌套调用 vitis.bat 可靠）。"""
    root = (install.root if install and install.root else os.environ.get("XILINX_VITIS")) or ""
    if not root:
        return ""
    platform_dir = "win64" if os.name == "nt" else "lnx64"
    name = "vitis-ide.exe" if os.name == "nt" else "vitis-ide"
    exe = os.path.join(root, "ide", "electron-app", platform_dir, name)
    return exe if os.path.isfile(exe) else ""


def _windows_process_table():
    """用 Win32 API 枚举进程 (pid, ppid, 名称)
    （tasklist 在某些受限令牌下会 Access denied，不能用）。"""
    import ctypes
    from ctypes import wintypes

    class PROCESSENTRY32(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("cntUsage", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.c_void_p),
            ("th32ModuleID", wintypes.DWORD),
            ("cntThreads", wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", wintypes.DWORD),
            ("szExeFile", ctypes.c_char * 260),
        ]

    kernel32 = ctypes.windll.kernel32
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel32.Process32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
    kernel32.Process32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    snapshot = kernel32.CreateToolhelp32Snapshot(0x00000002, 0)  # TH32CS_SNAPPROCESS
    if not snapshot or int(snapshot) == -1:
        return []
    table = []
    try:
        entry = PROCESSENTRY32()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
        ok = kernel32.Process32First(snapshot, ctypes.byref(entry))
        while ok:
            table.append((int(entry.th32ProcessID), int(entry.th32ParentProcessID),
                          entry.szExeFile.decode("mbcs", "ignore").lower()))
            ok = kernel32.Process32Next(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return table


def _windows_process_names():
    return [name for _pid, _ppid, name in _windows_process_table()]


def _find_descendant_pids(root_pid):
    """按父子关系找出 root_pid 的所有子孙进程。"""
    if os.name != "nt":
        return []
    try:
        table = _windows_process_table()
    except Exception:
        return []
    children = {}
    for pid, ppid, name in table:
        children.setdefault(ppid, []).append((pid, name))
    result = []
    stack = [int(root_pid)]
    while stack:
        current = stack.pop()
        for pid, name in children.get(current, []):
            if pid in [p for p, _n in result]:
                continue
            result.append((pid, name))
            stack.append(pid)
    return result


def _vitis_ide_running():
    """是否有 Vitis IDE 正在运行（不同工作区都可能）。"""
    if os.name == "nt":
        try:
            names = _windows_process_names()
            if names:
                return any("vitis-ide" in n for n in names)
        except Exception:
            pass
        try:
            out = subprocess.check_output(
                ["tasklist", "/FI", "IMAGENAME eq vitis-ide.exe", "/NH"],
                stderr=subprocess.DEVNULL)
            return b"vitis-ide" in out.lower()
        except Exception:
            return False
    try:
        out = subprocess.check_output(["pgrep", "-f", "vitis-ide"], stderr=subprocess.DEVNULL)
        return bool(out.strip())
    except Exception:
        return False


def _workspace_lock_path(workspace):
    return os.path.join(workspace, "_ide", ".wsdata", ".lock")


def _pid_alive(pid):
    if os.name != "nt":
        try:
            os.kill(int(pid), 0)
            return True
        except OSError:
            return False
    import ctypes
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, int(pid))  # QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong(0)
    ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel32.CloseHandle(handle)
    return bool(ok) and code.value == 259  # STILL_ACTIVE


def _terminate_pid(pid):
    if os.name != "nt":
        try:
            os.kill(int(pid), signal.SIGTERM)
        except Exception:
            pass
        return
    import ctypes
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x0001, False, int(pid))  # PROCESS_TERMINATE
    if handle:
        kernel32.TerminateProcess(handle, 1)
        kernel32.CloseHandle(handle)


def _stop_server_process(pid, logger, wait=6.0):
    """dispose 之后服务端还会挂约 60 秒（它的空闲退出定时器），这期间工作区 `.lock`
    一直被占，IDE 一启动就会报 "already in use"。

    注意 `cp.pid` 是 `vitis-server.bat` 那个 cmd 的 PID，真正占锁的是它启动的
    java 子进程，所以要把这棵进程树一起收掉。"""
    if not pid:
        return
    try:
        pid = int(pid)
    except Exception:
        return
    deadline = time.time() + wait
    while time.time() < deadline and _pid_alive(pid):
        time.sleep(0.25)
    if _pid_alive(pid):
        _terminate_pid(pid)
        logger.info_detail("已结束 Vitis 服务端启动器进程（pid=%s）" % pid)
    for child_pid, child_name in _find_descendant_pids(pid):
        if child_name.startswith("java"):
            _terminate_pid(child_pid)
            logger.info_detail("已结束残留的 Vitis 服务进程（java pid=%s），释放工作区锁" % child_pid)


def _handle_workspace_lock(ctx, logger=None, timeout=30.0):
    """处理工作区占用锁（`<ws>/_ide/.wsdata/.lock`，Vitis 用独占方式打开它）。

    三种情况：
    - 没有锁文件            → 直接打开；
    - 有 IDE 在跑           → 不重复打开，提示切到已有窗口；
    - 锁被（刚退出的）服务端占着 → 等它释放（最多 timeout 秒）后删掉再打开。
      生成完工作区时服务端还没完全退出，这时立刻开 IDE 就会弹
      "The workspace ... is already in use"。

    返回 True 表示可以继续打开，False 表示应跳过。"""
    logger = logger or LOG
    lock = _workspace_lock_path(ctx.workspace)
    if not os.path.isfile(lock):
        return True
    if _vitis_ide_running():
        logger.warn("工作区正被一个已打开的 Vitis IDE 使用：%s" % ctx.workspace)
        logger.info("请直接切换到那个窗口；如果它是旧的/卡死的，先关掉它再运行本脚本。")
        return False

    deadline = time.time() + timeout
    first = True
    last_wait_msg = time.time()
    while os.path.isfile(lock):
        try:
            os.remove(lock)
            logger.ok("已清理工作区锁（Vitis 服务端已退出）")
            return True
        except Exception:
            if first:
                logger.info("Vitis 服务端还在释放工作区锁，等它退出后再打开 IDE ...")
                first = False
            if time.time() >= deadline:
                break
            if time.time() - last_wait_msg > 10:
                logger.info("      仍在等待（已等 %d 秒）..." % int(timeout - (deadline - time.time())))
                last_wait_msg = time.time()
            time.sleep(0.5)

    if not os.path.isfile(lock):
        return True
    logger.warn("工作区锁仍被占用；若 IDE 弹出 \"already in use\"，点 Open Anyway 即可。")
    return True


def open_ide(install, ctx):
    if not install:
        LOG.warn("没有可用的 Vitis 环境，无法自动打开 IDE")
        return
    env = _child_env()
    if install.root and not env.get("XILINX_VITIS"):
        env["XILINX_VITIS"] = install.root

    if not _handle_workspace_lock(ctx, LOG):
        return

    # 优先直接启动 IDE 本体：脚本本身可能就跑在 vitis.bat -s 里，
    # 再嵌套调用一次 vitis.bat 会被批处理串掉的环境变量搞失败。
    exe = _ide_executable(install)
    if exe:
        cmd = [exe, "--no-sandbox", "--log-level=debug", ctx.workspace]
    elif install.launcher:
        cmd = [install.launcher, "-w", ctx.workspace]
    else:
        LOG.warn("没有可用的 Vitis 启动器，无法自动打开 IDE")
        return

    LOG.raw("")
    LOG.info("启动 Vitis IDE：%s" % " ".join(cmd))
    try:
        kwargs = dict(cwd=ctx.root, env=env,
                      stdin=subprocess.DEVNULL,
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL)
        if os.name == "nt":
            kwargs["creationflags"] = 0x00000200  # CREATE_NEW_PROCESS_GROUP
        else:
            kwargs["start_new_session"] = True
        subprocess.Popen(cmd, **kwargs)
        LOG.ok("Vitis IDE 正在启动，工作区：%s" % ctx.workspace)
    except Exception as exc:
        LOG.warn("自动打开失败：%s" % exc)
        LOG.info("请手动执行：%s -w \"%s\"" % (install.launcher or "vitis", ctx.workspace))


if __name__ == "__main__":
    sys.exit(main())
