## 项目名称
软件定义的信号测试与验证平台

## 项目简介
本项目面向便携式测试及通信等领域验证需求，基于ZYNQ-7020 SoC构建统一软件定义的多域信号测试与验证平台。采用软件定义的架构，单平台即可集成任意波形发生器、示波器、高精度宽频带实时频谱仪、频率特性分析仪与数字滤波器等核心功能。PL端负责高速ADC采集与并行信号处理：构建FFT、FIR、IIR实时算法流水线；通过FFT硬件加速卷积预算实现Chirp-Z变换，在关注频段内实现任意频率分辨率细化；基于Vitis HLS设计矩阵乘法与线性方程组求解硬件加速器，加速基于Levy最小二乘的传递函数拟合；构建由AXI控制、双端口BRAM存储波形表的DDS IP核，波形数据支持PS端动态改写，实现用户自定义任意波形发生；通过AXI VDMA驱动LCD显示，并利用AXI DMA实现PS-PL高速数据交互。PS端负责数据流调度、人机交互控制及部分数据分析，通过AXI4-Lite配置ADC采集、FFT等PL模块的工作状态与参数。信号发生器支持任意函数波形的发生。频谱仪与示波器覆盖DC至奈奎斯特频率，实现精细时域和频域分析。频率特性分析仪可测量未知系统的幅频、相频与冲激响应，并拟合传递函数。


## 文件夹目录结构

```text
Signal_Platform/
├── src/                       工程的源码
│   ├── ps/                    PS 端软件
│   │   ├── *.c / *.h          应用源码（入口为 main.c）
│   │   ├── system_wrapper.xsa Vivado 导出的硬件描述
│   │   ├── vitis_project.json 工程清单（平台/应用名、CPU、编译选项等）
│   │   └── ide/launch.json    调试配置模板
│   └── pl/                    PL 端硬件设计
│       ├── rtl/               Verilog 源码
│       ├── bd/system/         Block Design（system.bd 及其 IP）
│       ├── constr/pin.xdc     引脚约束
│       ├── ip/                独立 IP（fir_compiler_0）
│       ├── imports/           IP 使用的系数文件（.coe）
│       ├── ip_repo/           自定义 IP 仓库
│       └── utils/             其它参考文件（adc.dcp）
├── build/                     工程生成/清理脚本
│   ├── create_vitis_workspace.bat / .py   一键生成 Vitis 工作区
│   ├── clean_vitis_workspace.bat          一键清理 Vitis 工作区
│   ├── create_vivado_project.bat / .tcl   一键生成 Vivado 工程
│   ├── find_vivado.ps1                    自动定位 Vivado 安装路径
│   └── README.md                          脚本的详细用法与排错说明
├── board/                     脚本生成的工程（可随时删除后重新生成），初始状态为空
│   ├── Vitis_WorkSpace/       Vitis 工作区（平台 BSP + 应用工程 + ELF），真实目录就在这里
│   ├── prj/                   Vivado 工程（prj.xpr）
├── data/                      测试数据与报告
├── report/                    设计报告
│   └── report.docx
├── skill/                     技能包
└── README.md                  本说明
```

> 提示：`board/` 里的内容全部由脚本生成，初始状态为空，删掉后重新运行对应脚本即可恢复；



## 复现说明
本工程提供了一套可一键复现的脚本，读者可以查看、修改、优化 PS 端与 PL 端的工程设计。

复现本工程需要准备：一块 **ZYNQ7020** 开发板、**AD9238** 模块、**AD9767** 模块，以及一块 **800×480 RGB565 TFT LCD** 屏幕（触控使用 IIC 通信）。板卡连线需满足引脚约束文件 `src/pl/constr/pin.xdc` 的定义。

**路径要求**：工程所在路径尽量短（建议直接放在某个盘的根目录），且不要包含中文和空格。电脑上需已安装 **Vitis 26.1** 与 **Vivado 26.1**，然后按以下步骤操作。且项目文件夹名字必须是`Signal_Platform` 。

## 复现步骤
1. 依次打开 `Signal_Platform` 文件夹和 `build`文件夹，双击 `build/create_vitis_workspace.bat`这个 Windows 批处理文件。

2. 等待脚本自动完成：首次运行约需 1-5 分钟（要编译平台 BSP 和应用），它会在 `board` 文件夹内创建 Vitis 工作区（`board/Vitis_WorkSpace`），编译完成后自动打开 Vitis 工程，即可查看代码。之后再次运行时工作区已就绪，只需几秒就会打开工程。

   > **注意**：过程中可能出现“连接 Vitis 服务失败”的提示，脚本会自动启用兼容补丁并重试，耐心等待即可。

3. 连接板卡，点击 `Run` 即可下载程序、查看运行效果；也可以将程序固化到 Flash 中。

## 关于删除vitis工作区

需要删除工作区时，建议直接双击 `build/clean_vitis_workspace.bat` 清理，一键清除。

## 关于 Vivado 工程

本工程的复现只需要 Vitis，不依赖 Vivado。若读者想查看 PL 端工程，可以运行 `build/create_vivado_project.bat`，等待 2–3 分钟，Vivado 会自动打开工程；同时在 `board` 文件夹内创建存放工程的 `board/prj` 文件夹（工程文件为 `board/prj/prj.xpr`）。

更多参数与排查说明见 `build/README.md`。
