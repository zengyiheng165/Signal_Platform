## 本详细说明由AI生成，团队成员不保证准确性。如您需要简单版使用说明，请阅读`Software_Defined_Signal_Test_and_Validation_Platform`根目录下的`README.md`



# build —— 工程生成脚本（Vitis / Vivado）

本目录存放两种“从源码重建工程”的脚本，两者互不依赖：

| 脚本 | 作用 | 生成位置 |
| --- | --- | --- |
| `create_vitis_workspace.bat` / `.py` | 建立 PS 端 Vitis 工程工作区（BSP + 应用 + 编译） | `board\Vitis_WorkSpace` |
| `create_vivado_project.bat` / `.tcl` | 建立 PL 端 Vivado 工程（Block Design + RTL + 约束） | `board\prj` |

工程目录可以整体移动到任意位置（换盘符、换电脑），之后重新运行对应脚本即可用当前真实路径
重建工程，不会残留旧机器上的绝对路径。

## 快速开始

**最简单：直接双击 `build\create_vitis_workspace.bat`**（会自动挑一个能用的 Python，
没有就改用 Vitis 自带的 Python；结束后窗口会停住，方便看结果）。

命令行方式：

```bat
:: 1) 生成工作区并打开 Vitis IDE（需要时自动重建）
python build\create_vitis_workspace.py
:: 或（推荐，不依赖系统 Python）
build\create_vitis_workspace.bat

:: 2) 只看环境/状态，不生成
python build\create_vitis_workspace.py --check

:: 3) 改过 src\ps 里的源码 -> 只同步源码并重新编译（速度快）
python build\create_vitis_workspace.py --sync

:: 4) 在 Vitis IDE 里改过源码，想同步回 src\ps
python build\create_vitis_workspace.py --pull

:: 5) 源码里如果有从旧机器带过来的绝对路径 #include，一键改成可移植写法
python build\create_vitis_workspace.py --fix-sources
```

> **`python ...` 输入后“没反应”怎么办？**
> 在 cmd 里执行 `where python`，如果结果是
> `C:\Users\...\AppData\Local\Microsoft\WindowsApps\python.exe`，
> 那只是应用商店的占位程序（运行直接退出、不输出任何东西）。此时请改用
> `build\create_vitis_workspace.bat` 或 `vitis -s build\create_vitis_workspace.py`，
> 也可以先用 `--vitis` 指定安装路径，或把真正的 Python 加进 PATH。

没有可用的系统 Python 时，用 Vitis 自带的 Python 运行，效果完全一样：

```bat
vitis -s build\create_vitis_workspace.py
```

Vitis 不在默认安装位置时，显式指定（可指向安装目录或 `vitis.bat`）：

```bat
python build\create_vitis_workspace.py --vitis "D:\2026.1\Vitis"
```

### 一键清理工作区

想删掉生成的工作区、从头再来（或腾空间）时，**双击 `build\clean_vitis_workspace.bat` 即可，不再询问确认，直接执行**：

```bat
build\clean_vitis_workspace.bat              :: 直接清理
build\clean_vitis_workspace.bat --force      :: 即使 Vitis IDE 还占着也强删（多半会失败）
```

它删除：

- `board\Vitis_WorkSpace`（整个工作区）
- `board\.vitis_workspace.json`（状态记录）
- Vitis 用的短路径联接点（`<盘>:\vws\…`，空的 `vws` 目录一并删掉）
- `board\logs`（日志目录）
- `board\.vitis_create.lock`（单实例锁）

**不动** `src\ps`、`src\pl` 和任何脚本。清理后 `build` 里只剩脚本和说明；
想再要工程，运行 `create_vitis_workspace.bat` 重新生成即可。

> 为什么不让你自己删文件夹：BSP 中间文件路径超过 260 字符，资源管理器删不动；
> 脚本是通过短路径联接点去删的（等价的命令行是
> `python build\create_vitis_workspace.py --purge`）。

## 参数

| 参数 | 说明 |
| --- | --- |
| `--vitis PATH` | Vitis 安装目录或 `vitis.bat` 路径；不指定时自动搜索 |
| `--workspace DIR` | 工作区目录，默认 `<工程>\board\Vitis_WorkSpace` |
| `--manifest FILE` | 工程清单，默认 `<工程>\src\ps\vitis_project.json` |
| `--check` | 只体检：列出源文件、XSA、找到的 Vitis、工作区状态 |
| `--clean` | 删除已有工作区，完整重新生成 |
| `--sync` | 只同步源码并重新编译应用 |
| `--pull` | 把工作区里改过的 `.c/.h` 同步回 `src\ps` |
| `--fix-sources` | 把 `src\ps` 里 `#include "X:\...\xxx.h"` 这类绝对路径改成 `#include "xxx.h"` |
| `--purge` | 删除生成的工作区、状态记录、短路径联接点（不动源码），见上面“一键清理” |
| `--force` | 配合 `--purge`：即使 Vitis IDE 正在用也强删 |
| `--no-open` | 生成后不自动打开 IDE |
| `--no-app-build` | 只建工程，不编译应用 |
| `--platform-only` | 只创建并编译平台（BSP/FSBL），不建应用 |

`build\create_vitis_workspace.bat` 会把上面的参数原样透传，例如：
`build\create_vitis_workspace.bat --clean`、`build\create_vitis_workspace.bat --vitis "D:\2026.1\Vitis"`。

## 判断逻辑

脚本会把「当前路径 + 清单指纹 + XSA 指纹 + 源码指纹」记录在
`board/.vitis_workspace.json`，据此决定动作：

| 情况 | 动作 |
| --- | --- |
| 工作区不存在 / 不完整 | 完整生成（平台 + 应用 + 编译） |
| 工程目录被移动、清单或 XSA 变了 | 完整生成 |
| 只有 `src\ps` 源码变了 | 只同步源码并重新编译 |
| 什么都没变 | 直接打开 IDE |

## 长路径 / 空格路径自动规避（重要）

Windows 下 Vitis 的 BSP 编译受路径长度限制（CMake/ninja 生成的对象文件路径不能超过 260 字符），
而且 Vitis 内部有些路径是当命令行参数传的，带**空格或中文**也会失败（典型报错
`'standalone' is an invalid os. Valid OSes are '[]'`）。本工程默认路径已经超过安全值
（`...\board\Vitis_WorkSpace\...` 约 105 字符），直接生成平台会报 `Invalid project location ...`。

因此当工作区路径超过 70 字符、或含空格/中文时，脚本会自动：

1. 在工程所在盘根目录建 `D:\vws\<工程名>_<短哈希>` 目录；
2. 建一个**目录联接点**指向工程的 `board` 目录；
3. 让 Vitis 通过 `D:\vws\xxxx\Vitis_WorkSpace`（约 35 字符，纯 ASCII）工作；
4. 如果 XSA 路径本身含空格/中文，先把它复制到 `D:\vws\xxxx_hw\` 再用。

工程里的 `board\Vitis_WorkSpace` 仍然是工作区**真实存放的位置**，联接点只是给 Vitis 用的短路径别名。
工程搬家后短哈希会变，脚本会自动使用新的联接点；若工程放到短路径（如 `D:\sds\`），
脚本会自动改用真实路径，不再建联接点。

> **注意**：Vitis 用“你打开工作区时用的那个路径”去编译，不会把联接点解析回真实路径。
> 所以请让脚本自动打开 IDE（它开的是 `D:\vws\...` 短路径）；如果手工打开
> `board\Vitis_WorkSpace`，BSP 的对象文件路径会超过 CMake 的 250 字符上限，
> 编译平台就会报 `Error in generating platform`。

> 因为 BSP 中间文件很深，用资源管理器手动删除/移动 `board\Vitis_WorkSpace` 可能报“路径太长”。
> 删除请用 `python build\create_vitis_workspace.py --clean`，移动整个工程时先剪切 `board` 之外的部分，
> 或先清理再移动。

## PL 侧：Vivado 工程（`board\prj`）

**只建工程，不综合、不布局布线**。生成后直接打开工程即可（工程里包含完整的 Block Design、
RTL、约束和 IP 配置）。

```bat
:: 双击或运行：创建（已存在则直接打开）Vivado 工程
build\create_vivado_project.bat

:: 改了 src\pl 里的代码 / BD / 约束后，删掉重建
build\create_vivado_project.bat -recreate

:: 无界面方式（脚本里调用）
build\create_vivado_project.bat --batch
```

| 参数 | 说明 |
| --- | --- |
| `-recreate` | 删除 `board\prj` 后完整重建 |
| `--batch` / `-batch` | 无界面（batch）运行，不弹 Vivado GUI |
| `-synth` | （可选）顺便跑综合 |
| `-bit` | （可选）顺便跑到比特流 |
| `-xsa <文件>` | （可选）跑完比特流后导出 XSA，供 Vitis 侧使用 |
| `-jobs N` | 并行数，默认 8 |
| `-root <路径>` | 手工指定工程根目录（默认按脚本位置推算） |

**Vivado 装在非常规位置也不用管**：`.bat` 会依次用环境变量 `XILINX_VIVADO` / `VIVADO_ROOT`、
PATH、`find_vivado.ps1`（查 Windows 卸载注册表 + 各磁盘常见目录），最后才用硬编码列表兜底。
AMD 统一安装器把产品登记成 “... Vitis Unified Software Platform <ver>”，其中也包含 Vivado，
所以注册表里按 `Vivado|Vitis` 都能找到。都不行时手动指定：

```bat
set VIVADO_ROOT=D:\Xilinx\Vivado\2026.1
build\create_vivado_project.bat
:: 或直接传参
build\create_vivado_project.bat -root "D:\...\工程根目录"
```

（判定标准：那个目录下有 `bin\vivado.bat`。）

`src\pl` 里放的就是“建工程所需的全部文件”：

```
src\pl\
├── rtl\              5 个 .v（accumulator_fir / signed_add / chaifen / wave_sel / fft_init）
├── bd\system\        Block Design：system.bd + ip\（IP 参数都在这里）+ ui\
├── constr\           pin.xdc
├── ip\               fir_compiler_0.xci（被 accumulator_fir.v 例化）
├── imports\          E7_9_fir.coe（fir_compiler 的系数文件）
├── ip_repo\          10 个自定义 IP 仓库（my_adc / my_dds / fft_init_d / Levy_2 / rgb2lcd …）
└── utils\            adc.dcp（原工程 utils 里的参考文件）
```

生成出来的工程：

```
board\prj\
├── prj.xpr
├── prj.srcs\         工程登记信息（RTL/约束/COE 以相对路径指回 ..\..\src\pl，未复制）
├── prj.gen\          BD 生成的 wrapper、IP 输出等
└── .Xil\ NA\ ...     Vivado 的临时文件（都留在工程目录里）
board\logs\           vivado.log / vivado.jou
```

> **工程根目录不会产生任何文件**：脚本把 Vivado 的工作目录切到 `board\prj` 再启动，
> 所以 `.Xil`、`NA`、`prj.gen`、`nlviewcommon.logfile`、`vivado_pid*.str` 这些都落在
> `board\prj` 里；日志写在 `board\logs`。`-recreate` 时先退出该目录再删除（Windows
> 不允许删除正在被某个进程当作工作目录的文件夹）。

几点说明：

* **RTL / 约束是“就地引用”**：`.xpr` 里存的是 `$PPRDIR/../../src/pl/rtl/xxx.v` 这种相对路径，
  所以 `src\pl` 始终是唯一源码，工程整体搬迁也不会断；改了代码用 `-recreate` 重建即可。
* **Block Design、独立 IP、系数文件是复制进工程**的：`system.bd` 本身不保存 IP 参数
  （参数在 `bd\system\ip\*.xci` 里），必须连 `ip\` 一起搬；`fir_compiler_0` 和
  `imports\...\E7_9_fir.coe` 也会复制到 `prj.srcs\sources_1\ip|imports` 下并保持原有相对布局，
  这样 IP 里的 `../../imports/...` 仍然成立（否则 Vivado 会把 IP 输出写到工程外面去）。
* 脚本会自动设置 `ip_repo_paths`（10 个自定义 IP 仓库）、生成 BD、`make_wrapper` 出
  `system_wrapper.v` 并把它设为顶层。
* `.tcl` 文件是**纯 ASCII 英文**的，这是故意的：Vivado 用系统码页（中文 Windows 是 GBK）
  读取 Tcl 文件，UTF-8 中文注释可能把后面的 `[` / `"` 吞进多字节字符里导致语法错误。

## 常见问题

**Q：脚本跑完了，但 Vitis IDE 没有弹出来**

脚本默认在生成完工作区后自动打开 IDE（加 `--no-open` 才跳过）。它是直接启动
`<Vitis 安装目录>\ide\electron-app\win64\vitis-ide.exe --no-sandbox --log-level=debug <工作区>`
来打开的——不再嵌套调用 `vitis.bat`，因为脚本本身往往就跑在 `vitis -s` 里，
再嵌一层批处理会被串掉的环境变量搞失败。若窗口迟迟不出现：先等 20~30 秒；
仍没有就检查是否被杀毒软件拦截，或手动执行
`"D:\2026.1\Vitis\bin\vitis.bat" -w <工作区路径>`。

**Q：打开时弹窗 “The workspace ... is already in use”**

Vitis 用一个独占的锁文件 `<工作区>\_ide\.wsdata\.lock` 表示“工作区正在被使用”，
弹窗只有三种来源：

1. **刚生成完就开 IDE**（最常见）：脚本生成时起的 Vitis 服务端是 `java ... RigelApp`，
   `dispose()` 之后它还会按自己的空闲定时器再活约 60 秒，这期间锁一直被它占着。
2. 这个工作区确实已经被另一个 Vitis IDE 实例打开着（IDE 还开着又跑了一次脚本、或重复双击）。
3. 上次 Vitis 异常退出（崩溃 / 任务管理器强杀）留下的死锁。

脚本现在会这样处理：

- 生成结束后，**顺着 `vitis-server.bat` 的进程树把残留的 `java` 服务端结束掉**，
  锁会立刻释放（日志里会看到 “已结束残留的 Vitis 服务进程（java pid=…），释放工作区锁”）；
- 打开 IDE 前再检查一次锁：**没有** IDE 在跑就删掉死锁再开；仍被占用则最多等 30 秒；
- **有** IDE 在跑，就不重复打开，只提示你切到已有窗口。

手动处理：确认任务管理器里没有 `vitis-ide.exe` 后，删掉
`board\Vitis_WorkSpace\_ide\.wsdata\.lock` 即可；或直接点弹窗里的 “Open Anyway”
（在确认没有进程占用时，点它是安全的）。

**Q：在 IDE 里点“编译平台”，只报 `Error in generating platform 'platform_v5_4'.`**

这句提示底下其实有**两种完全不同的原因**，看日志里的关键字区分：

**原因 A（最常见）：路径太长，CMake 拒绝编译**

日志里会出现：

```
The object file directory
  D:/.../board/Vitis_WorkSpace/platform_v5_4/.../gen_bsp/CMakeFiles/CMakeScratch/TryCompile-xxxx/
  has 252 characters.  The maximum full path to an object file is 250
-- Check for working C compiler: ... - broken
[ERROR]: Failed to configure CMake with updated BSP configurations
```

说明这次编译用的是**工程里的长路径**（`board\Vitis_WorkSpace`），BSP 的对象文件路径
到了 252 字符，超过 CMake 的 250 上限。**解决办法：用脚本打开 IDE**（脚本开的是
`D:\vws\<工程名>_<短哈希>\Vitis_WorkSpace` 这个短路径），不要手工打开
`board\Vitis_WorkSpace`。已经这样搞坏过的话，执行：

```bat
build\create_vitis_workspace.bat --clean
```

**原因 B：BSP 的 ninja 缓存坏了，又被 GBK 解码带偏**

日志里会出现 `UnicodeDecodeError: 'utf-8' codec can't decode byte 0xd5`：

1. BSP 构建目录里 `CMakeFiles/rules.ninja` 丢了（构建被中断、或同一个工作区
   被两种路径编译过），`build.ninja` 却还在；
2. 于是 `cmake --build .` 一启动，ninja 就先报
   `系统找不到指定的文件。`（中文，GBK 编码）；
3. Vitis 的 BSP 构建脚本用**严格 UTF-8** 解码这段输出，抛
   `UnicodeDecodeError: 'utf-8' codec can't decode byte 0xd5`，
   界面上就只剩那句 `Error in generating platform`。

处理办法（任选其一，从轻到重）：

```bat
:: 1) 只清 BSP 构建缓存，然后重新编译
python build\create_vitis_workspace.py --fix-bsp
build\create_vitis_workspace.bat --sync

:: 2) 直接重建整个工作区（最稳）
build\create_vitis_workspace.bat --clean
```

脚本在生成时已经会自动检测“平台编译失败”，清理 BSP 缓存后重试一次，
所以正常情况下重新跑一遍 `create_vitis_workspace.bat` 就能恢复。

**Q：换电脑后提示“没有找到可用的 Vitis 安装”**

脚本找 Vitis 的顺序是：`--vitis` 参数 → 环境变量 `VITIS_ROOT` / `XILINX_VITIS` → PATH →
注册表安装记录 → 各磁盘常见目录（还找不到会逐层扫一遍）。
`XILINX_VITIS` 只在 Vitis 命令行窗口里有值，普通 cmd 里没有，所以如果 Vitis 装在非常规位置，
就可能漏掉。解决方式任选其一：

```bat
:: 1) 直接告诉脚本 Vitis 在哪（目录下有 bin\vitis.bat 就对）
build\create_vitis_workspace.bat --vitis "D:\Xilinx\Vitis\2026.1"

:: 2) 先设环境变量，再照常运行
set VITIS_ROOT=D:\Xilinx\Vitis\2026.1
build\create_vitis_workspace.bat

:: 3) 在 Vitis 命令行窗口里运行（这时 XILINX_VITIS 已就绪）
vitis -s build\create_vitis_workspace.py
```

如果那台电脑根本没装 Vitis，或者只装了 Vivado，就需要先安装
「AMD Vitis Unified Software Platform」（安装目录里能看到 `bin\vitis.bat`）。

**Q：第一次能跑，后面再跑报 `Vitis Create Client failed, 'utf-8' codec can't decode byte ...`**

这是 Vitis 2026.1 客户端自带的问题：它用严格 UTF-8 解码 Vitis 服务端的启动输出，
服务端只要先打印一行本地编码（GBK）的中文提示就会抛这个异常。
脚本已经内置处理：自动重试、把服务端启动改成容错解码，并把服务启动超时放宽到 180 秒。
直接再运行一次即可；若反复出现，先关掉所有 Vitis 窗口（或重启电脑）再试。

**Q：提示“已经有一个本脚本的实例在运行”**

同一时间只允许一个实例（防止双击两次互相覆盖工作区）。等前一个跑完；
确认没有在跑的话，删掉 `board\.vitis_create.lock` 再试。

**Q：工作区没了 / 被删了**

直接双击 `build\create_vitis_workspace.bat` 重建即可；它只依赖 `src\ps` 里的输入，
不会因为工作区被删而丢东西。

**Q：改完代码怎么让工程重新编译**

`build\create_vitis_workspace.bat --sync`。

## 工程输入（全部放在 src\ps）

```
src\ps\
├── *.c / *.h                 应用源码（自动扫描，新增文件无需改脚本）
├── system_wrapper.xsa        Vivado 导出的硬件描述
├── vitis_project.json        工程清单：平台/应用名称、CPU、OS、优化级别、栈堆大小
└── ide\launch.json           (可选) 调试配置模板，会写入 app 的 _ide\launch.json
```

## 生成的工作区结构

```
board\Vitis_WorkSpace\        （真实目录；Vitis 通过 D:\vws\<工程名>_<短哈希> 联接点访问它）
├── platform_v5_4\            平台组件（BSP、FSBL、导出的 xpfm）
├── app\                      应用组件（src 为源码副本，build 下为 app.elf）
│   └── _ide\                 调试配置、比特流、ps7_init.tcl
└── _ide\                     Vitis 工作区元数据
```

> BSP 的 `gen_bsp` 目录很深，路径一长 CMake 就直接拒绝编译，所以生成/编译/打开 IDE
> 都走短路径联接点。删除请用 `clean_vitis_workspace.bat`，它会把工作区和联接点一起清掉。

> 说明：源码以 `src\ps` 为准，每次完整生成/同步都会复制到工作区组件里编译。
> 如果在 Vitis IDE 里直接改了代码，记得用 `--pull` 同步回 `src\ps`，否则下次
> `--clean` 会以 `src\ps` 的旧内容覆盖工作区。
>
> 自定义 IP（my_adc / my_dds / fft_init_d …）的驱动头文件目录由脚本自动加入
> `USER_INCLUDE_DIRECTORIES`，所以源码里只要写 `#include "my_adc.h"` 即可，
> 不要再写带盘符的绝对路径。
>
> 在 VS Code 等 Electron 终端里运行时，脚本会剔除 `ELECTRON_RUN_AS_NODE`，
> 否则 Vitis IDE 会被当成 Node.js 启动而失败。
