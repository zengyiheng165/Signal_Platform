#include <stdio.h>
#include <xaxidma_hw.h>
#include <xil_types.h>
#include <xparameters_ps.h>
#include <xstatus.h>
#include "platform.h"
#include "xil_printf.h"
#include "xaxidma.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xil_util.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
#include "xgpio.h"
#include "sleep.h"
#include <xuartps_hw.h>
#include "xuartps.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fft_init_d.h"
#include "my_adc.h"
#include "my_dds_bram_23.h"
#include "my_dds.h"
#include "my_fir_init.h"
#include "my_iir.h"

#include "xbram.h"

#include <complex.h>

#include "dds_wave.h"
#include "clk_wiz1.h"
#include "czt.h"
#include "plfft.h"
#include "levy_ps.h"            /* ★ PL 曲线拟合(你写的) */
#include "filter.h"             /* ★ 你写的下发层: FIR_COEFFICIENT_RELOAD / IIR_COEFFICIENT_SET */
#include "fir_filter_design.h"  /* ★ FIR 设计 + 量化 */
#include "iir_filter_design.h"  /* ★ IIR 设计 + 量化 */
#include "levy_ps.h"


#include "xaxivdma.h"
#include "axi_vdma_api.h"
#include "xil_cache.h"
#include "lcd_gfx.h"
#include "ui_screens.h"
#include "ui_awg.h"
#include "ui_app.h"
#include "touch.h"
#include "ui_touch.h"
#include "fft_windows.h"
#include "fra_core.h"          /* 频率特性分析仪数学核心(单点DFT/最小二乘拟合/逆FFT) */


#include"fir_filter_design.h"
#include"iir_filter_design.h"
#include"filter.h"


/* XXSUB: CZT 模式2【显示频率】的亚 bin 修正开关: 1=开, 0=关(关掉后与本改动之前完全一致)。
   只影响 app.spec.czt2_peak_hz(左上角读数 + 信息行 Z2峰); 曲线/峰值下标/幅度一律不变。 */


#define INTC_DEVICE_ID          XPAR_SCUGIC_SINGLE_DEVICE_ID //中断控制器的中断ID号

#define RESET_TIMEOUT_COUNTER	   10000           //复位超时的计数值
#define TEST_START_VALUE	      0xC           //测试数据的起始值


#define FFT_N            16384
#define MAX_PKT_LEN      (FFT_N * 4)         //发送数据包的长度(字节)

#define NUMBER_OF_TRANSFERS	10

#define XINTERRUPT_DEFAULT_PRIORITY     0xA0U 

#define POLL_TIMEOUT_COUNTER    10000U
#define NUMBER_OF_EVENTS	1

#define AXI_GPIO_INT_ID     XPS_FPGA0_INT_ID 

XGpio   AXI_Gpio;

#define UART_DEVICE_ID      0       //串口器件ID
#define UART_INT_IRQ_ID     XPAR_XUARTPS_1_INTR      //UART1中断ID号

static XUartPs Uart_Ps;     //UART器件的驱动实例


static XScuGic Intc;   //中断控制器的驱动实例


#define RX_INTR_ID0       XPS_FPGA1_INT_ID
#define RX_INTR_ID1       XPS_FPGA0_INT_ID 
#define TX_INTR_ID2       XPS_FPGA2_INT_ID
#define RX_INTR_ID2       XPS_FPGA3_INT_ID 


static XAxiDma AxiDma0;		//定义AXI DMA 的驱动实例
static XAxiDma AxiDma1;		//定义AXI DMA 的驱动实例
static XAxiDma AxiDma2;		//定义AXI DMA 的驱动实例

volatile u32 TxDone0;        //发送完成的标志
volatile u32 RxDone0;        //接收完成的标志
volatile u32 Error0;         //错误信号的标志

volatile u32 TxDone1;        //发送完成的标志
volatile u32 RxDone1;        //接收完成的标志
volatile u32 Error1;         //错误信号的标志

volatile u32 TxDone2;        //发送完成的标志
volatile u32 RxDone2;        //接收完成的标志
volatile u32 Error2;         //错误信号的标志

u8 *RxBufferPtr0;
u8 *RxBufferPtr1;
u8 *TxBufferPtr2;
u8 *RxBufferPtr2;
int *Rx0;
int *Rx1;
long long int *Tx2;
long long int *Rx2;

#define N FFT_N             // FFT点数(必须与 ADC 单次触发点数一致)
#define FS 8192000.0f       // 采样率 (Hz)
#define fre_N 1996      // 扫频点数
#define fre_first 1000  // 扫频起始频率
#define PI              3.141592653589793f




void dma_init(void);
int uart_init(XUartPs* uart_Ps);
static int intr_init(void);
static void disable_intr_system(XScuGic * int_ins_ptr, u16 rx1_intr_id,u16 rx2_intr_id);
static void RxIntrHandler0(void *Callback);
static void RxIntrHandler1(void *Callback);
void uart_intr_handler(void *CallbackRef);

float dmadata_2_realvoltage_vp(float data);


//#define  START_ADDR      0 //启始地址
//#define  BRAM_DATA_BYTE  4 //BRAM位宽

int dds_data[4096];




//LCD
XAxiVdma   vdma;

#define LCD_WIDTH  800
#define LCD_HEIGHT 480
#define LEN 480*800*3

#define FB_A   (u32)(XPAR_PS7_DDR_0_BASEADDRESS + 0x1000000)
#define FB_B   (u32)(FB_A + LEN)

u32 cur_fb = FB_A;   /* VDMA 正在读的缓冲 */
u32 nxt_fb = FB_B;   /* 空闲、可写入的缓冲 */
#define DISP_FB  FB_A 

static ui_app_t app;     /* 示波器 + 信号发生器 + 当前界面 */

#define SAMPLE_LEN   FFT_N           /* 每通道采样点数(必须与 MY_ADC 寄存器里写的点数一致) */
#define SAMPLE_RATE  8192000         /* 采样率 Hz = clk_wiz_0 给 ADC 的 8.192MHz(原来写 819200, 差 10 倍) */

/* 界面用的两路采样缓冲: 静态 + 32 字节对齐。
   不要拿 malloc 的 DMA 缓冲兼做界面数据(cache 对齐/生命周期都容易出问题) */
static s16 adc_buf0[SAMPLE_LEN] __attribute__((aligned(32)));
static s16 adc_buf1[SAMPLE_LEN] __attribute__((aligned(32)));

#define USE_SIM_ADC  0               /* 1=内部模拟波形自测; 0=接真实 ADC */

#define LIVE_FB   FB_A          /* 单缓冲: VDMA 一直读它, 之后永不换源 */

/* XXPPM: 采样率标定修正(ppm)。【以 DG2102 为基准的现场标定值】: 模式2 读数在 1MHz/2MHz/3MHz 处分别高 8/16/24Hz(纯比例 = 8ppm)。
   ★ 注意: 这 8ppm 也可能来自信号源本身(DG2102 时基约 +-25ppm), 二者无法用同一台源区分; 要确认需独立基准(频率计/GPSDO 测 8.192MHz)。
   只用于【计算频率】的地方(目前只有 CZT 模式2 的 fs2), 不影响显示值与写给 clk_wiz 的值。
   ★ 改成 0.0 即可完全关闭校正。 */
#ifndef ADC_FS_PPM_CORR
#define ADC_FS_PPM_CORR   (-8.0)
#endif
static double adc_fs_mhz = ADC_FS_SPEC_MHZ;
static u32    adc_fs_quiet;
static u8     adc_fs_screen = 0xFF;   /* 上一帧所在界面(0xFF = 还没进过主循环) */
static double adc_fs_prev_mhz;                 /* 变速前的采样率(校验不过要退回) */
static u8     adc_fs_is_spec;                  /* 当前采样率是不是频谱仪的小数频率 */
static u8     adc_fs_prev_is_spec;             /* 变速前那个值的性质(退回时照原样写回) */
static s64    adc_fs_prev_f_mhz;               /* 变速前测到的频率(单位 0.001Hz, 64 位同 meas) */
static u8     adc_fs_verify;                   /* 1 = 等下一次测量来校验这次变速 */
static u8     adc_fs_src_clk;                  /* 1 = 信号源与采样时钟同域, 不能自动变速 */

/* ---------------- 信号源(DAC 数据选择器) + ADC 使能: AXI GPIO ----------------
     AXI GPIO 4 位: bit[1:0] = ADC 使能(常态 0; 采集时脉冲拉高 1us),
                    bit[3:2] = 数据选择器: 00 = 普通 DDS(双路, 分别送 DAC1/DAC2),
                                          11 = BRAM DDS(单路, 只送 DAC1)。
     ★ 原来每帧的 ADC 触发脉冲直接写 0x03 / 0x00, 会把高两位一起清成 00 ——
       于是"切到 BRAM"永远被下一帧打回普通 DDS, BRAM 波形根本出不来。
       所有 GPIO 写入统一走下面几个函数(实现放在本文件最后)。 */
#define AWG_SRC_DDS_BITS    0x00u
#define AWG_SRC_BRAM_BITS   0x0Cu
#define AWG_ADC_BITS        0x03u

/* ★ DAC 源选择码 = wave_sel 的 sel 本身(必须一一对应, 06/04 容易搞反):
     00 = 普通 DDS(信号发生器) · 01 = FIR 直出 · 10 = IIR 直出 · 11 = BRAM-DDS */
#define AWG_SRC_DDS         0u
#define AWG_SRC_FIR         1u
#define AWG_SRC_IIR         2u
#define AWG_SRC_BRAM        3u

/* 通路验证: 置 1 = 开机就选 BRAM DDS, DAC1 应输出开机用 set_PM_wave()
   写进 BRAM 的那个 PM 波形。验证通过后改回 0(默认), 正式由界面按钮切换。 */
#define AWG_BOOT_SRC_BRAM   0

static void pl_gpio_apply(void);    /* 写常态值: 源位 + ADC 位为 0(只在变化时写) */
static void pl_adc_off(void);       /* ADC 使能位清零, 源位保留(强制写一次) */
static void pl_adc_pulse(void);     /* 采集触发: 拉高 1us 再回落(两个沿都真写) */
static void pl_awg_src_set(u8 src); /* 0 = 普通 DDS, 1 = BRAM DDS */

/* ---------------- 信号发生器常量与前向声明 ---------------- */

#define AWG_DDS_AMP_FS    128
#define AWG_BRAM_AMP_FS   1024
#define AWG_FULL_MVPP     UI_AWG_VPP_MAX_MV   /* ★ 满幅(唯一标定点在 ui_awg.h) */

/* BRAM 波形种类(与 dds_wave.c 的生成函数一一对应; p[] 含义看各自注释) */
/* ★ 波形编号直接沿用 ui_awg.h 那一套(界面与桥接同一个编号, 免得两处对不上) */
#define AWG_W_PULSE   UI_AWG_W_PULSE   /* 4  p[0] = 占空比 ×10 */
#define AWG_W_SINC    UI_AWG_W_SINC    /* 5  p[0] = wide */
#define AWG_W_HARM    UI_AWG_W_HARM    /* 6  用 awg_gen_harm_abs() 单独生成 */
#define AWG_W_AM      UI_AWG_W_AM      /* 7  p[0] = 调制比 ×10, p[1] = 载波比 fmfc */
#define AWG_W_DSB     UI_AWG_W_DSB     /* 8  p[0] = 载波比 fmfc */
#define AWG_W_FM      UI_AWG_W_FM      /* 9  p[0] = 载波比 fmfc, p[1] = Kf ×10 */
#define AWG_W_PM      UI_AWG_W_PM      /* 10 p[0] = 载波比 fmfc, p[1] = Kp ×10(周) */
#define AWG_W_ASK     UI_AWG_W_ASK     /* 11 p[0]=每码元周期数 p[1]=码元数 p[2]=码型 */
/* 谐波合成不用 p[]: 用 awg_gen_harm_abs(基波mV, 各次mV, 各次相位0.1度, 次数) 单独生成(n ≤ 8) */

/*通路验证: AWG_BOOT_SRC_BRAM=1 时, 开机用新桥接生成下面这种 BRAM 波形并施加到 DAC1 */
#define AWG_BOOT_BRAM_WAVE   AWG_W_AM
#define AWG_BOOT_BRAM_FREQ   2000          /* Hz */
#define AWG_BOOT_BRAM_MVPP   AWG_FULL_MVPP  /* mVpp (满幅) */
#define AWG_BOOT_USE_HARM    0             /* 1 = 验证块里额外跑一遍谐波合成入口 */


static u8   awg_dds_amp(s32 mv);
static u16  awg_bram_amp(s32 mv);
static u16  awg_ph12(s32 mdeg);
static void awg_apply_dds_ch(int ch, u8 on, u8 shape, s32 freq_hz, s32 vpp_mv, s32 phase_mdeg);
static void awg_bram_cfg(s32 freq_hz, s32 phase_mdeg, s32 vpp_mv, u8 on);
static void awg_gen_wave(u8 wave, const s32 *p);
static void awg_gen_harm_abs(int fund_mv, const s32 *h_mv, const s32 *h_pha_mdeg, int n);
static void awg_apply_from_ui(ui_awg_t *a);
static void filt_apply_from_ui(ui_filter_t *f);   /* ★ 数字滤波器: 设计+量化+下发 */
static int adc_fs_khz(double mhz)              /* MHz -> kHz, 串口只打整数 */
{
    return (int)(mhz * 1000.0 + 0.5);
}


static int  wait_vdma_frame(void);
void LCD_init(void);
static void adc_fs_apply(double mhz, int is_spec);
static void spec_fs_tab_init(void);
static u32 fft_log2(u32 n);

/* ★ Chirp-Z(频谱细化的 Z 变换)需要 8192 点: ADC 单次触发点数、FFT 点数(寄存器=log2)、
   频谱通路里所有按 N 计算的地方, 必须同时是 8192; CZT 关闭时全部回到 16384。
   ★ 切换只走 czt_pl_set() 一处, 避免"某个出口忘了还原"。
   ★ 宏 FFT_N / N / SAMPLE_LEN 一个字不改(缓冲区仍按 16384 分配, 8192 一定放得下)。 */
static u8   g_czt_on;              /* 1 = PL 按 8192 点工作(CZT 占用); 定义在文件后部 */
static u32  czt_nfft(void);        /* 当前 FFT 点数(8192 / 16384) */
static u32  czt_adc_len(void);     /* 当前 ADC 单次触发点数(与 FFT 点数一致) */
static void czt_pl_set(int on);    /* 唯一的点数模式切换点 */


/* ================= 频率特性分析仪 =================
   参照历史框架: 一次采集 → 打包进 FFT → DMA → 取第 k 个 bin 的实/虚 → 两路比值。
   新工程只有一路 FFT, 所以两路"分两次顺序进 FFT"(不能同时); 但两路 ADC 原始数据
   是同一帧同时抓到的(主循环每帧 pl_adc_pulse() 同时触发 my_adc_0/1), 所以
   两次 FFT 用的是同一采集时刻的数据 ⇒ 起始相位相同 ⇒ 比值里抵消 ⇒ 相位可用。
   k = f/(fs/N) = f/500 (fs=8.192MHz, N=16384), 扫频点取 500Hz 整数倍 ⇒ k 恒为整数。 */
#define FRA_DUT_IS_BUF0    1        /* ★ 被测是否用 adc_buf0: 上板若发现 |H| 是倒数(直连时显示约 -20dB), 改成 0 */

/* ★ PL FFT 输出的"实部/虚部"两个字哪个在高 32 位。
   实测现象: 相频反号(2 阶低通应为 0->-180, 实际画成 0->+180), 而幅频完全正常。
   这正是"两字对调"的特征: 对调后 Y/X 变成 conj(Y/X) -> 相位取负, 而 |Y/X| 不变。
   (当初用 fra_ps_dft 交叉校验时只比了幅度, 幅度对调换不敏感, 所以没查出来。)
   1 = 高32位是实部、低32位是虚部; 若改成 1 后相频反而变回 +180, 就改回 0。 */
#define FRA_FFT_RE_HIGH    0        /* ★ 低32位=实部(plfft.h 明确规定)。改成 1 等效于给 Y/X 取共轭 -> 相位整体反号! */
#define FRA_SETTLE_FRAMES  3        /* 每个频点先等几帧(等 DDS/滤波器稳定) */
#define FRA_AVG_FRAMES     4        /* 每个频点平均帧数(界面 avg 会覆盖它) */

static u8     fra_last_run = 0u;   /* 上一帧的 running: 检测"重新起扫" */
static u8     fra_phase = 0u;      /* 0=空闲 1=稳定中 2=测量中 3=完成 */
static u16    fra_pidx  = 0u;      /* 当前频点序号 */
static u8     fra_fcnt  = 0u;      /* 当前频点已测帧数 */
static double fra_hr    = 0.0;     /* 复数 H 累加 */
static u8     fra_fok;                 /* ★ 本频点"真正算进去"的帧数(fft 成功且参考够强) */
static u8     fra_retry;               /* ★ 本频点连续无有效帧的次数(防止卡死) */
static double fra_hi    = 0.0;

/* 用 PL FFT 对 src(16384 点 s16) 做一次不加窗 FFT, 取第 k 个 bin 的复数; 0 = 成功 */
static int fra_fft_bin(const s16 *src, int k, s32 *o_re, s32 *o_im);
/* 每帧调用一次: 扫频状态机(不阻塞, 只在 FRA 界面跑, 与频谱仪互斥) */
static void fra_sweep_step(void);



/* ★ 这一帧是否需要写屏(决定要不要等 LCD 帧起点)。
   FRA 这边现在每个点都要重画, 所以恒为 1 即可(保留此函数只为不动调用处结构)。
   那些帧不必等帧起点 —— 每帧等一次帧起点是扫频慢的最大一块。
   注意: 触摸处理也会写屏(按钮高亮), 但它走 ui_touch_poll, 与这里的帧同步无关,
   首帧/换屏/弹窗这些仍然照旧等帧起点。 */
static int ui_need_paint(void)
{
    /* ★ 单缓冲屏(VDMA 一直读同一块 framebuffer): 任何没对齐帧起点的写入都会
       按扫描位置撕裂成一条带(上/中/下 1/3 闪)。所以这里恒为 1 —— 每帧都等帧起点。
       提速的余地不在这里(不能破坏帧同步, 否则单缓冲屏会撕裂闪屏)。 */
    return 1;
}

int main()
{
    int Status;

    init_platform();

    setbuf(stdout, NULL);

    //UART初始化
    uart_init(&Uart_Ps);

    (void)LCD_init();

    //初始化dma
    dma_init();

    //中断初始化
    intr_init();

    
    //动态内存分配
	RxBufferPtr0 = malloc(MAX_PKT_LEN*sizeof(u8)) ;
    RxBufferPtr1 = malloc(MAX_PKT_LEN*sizeof(u8)) ;
    TxBufferPtr2 = malloc(MAX_PKT_LEN*sizeof(u8)); 
    RxBufferPtr2 = malloc(MAX_PKT_LEN*2*sizeof(u8)) ;
    

    //初始化axi_gpio
    XGpio_Initialize(&AXI_Gpio,XPAR_AXI_GPIO_0_BASEADDR);

  //  XGpio_DiscreteWrite(&AXI_Gpio, 1, 0x00);


    //判断当前AXI DMA是否为SG模式
	if (XAxiDma_HasSg(&AxiDma0)) {
		xil_printf("Device configured as SG mode \r\n");
		return XST_FAILURE;
      }
    if (XAxiDma_HasSg(&AxiDma1)) {
		xil_printf("Device configured as SG mode \r\n");
		return XST_FAILURE;
      }


    //传入DDS波形类型 _通道一   
    /* ★ 开机 DDS 配置: 直接调你写好的函数(Hz -> 频率字的 120MHz 标定在函数内部) */
    pl_dds1_config(2000, 0, 128, 1);      /* 正弦 / 2000Hz / 幅度128 / 相位0 (与原 85899@100MHz 等效) */
    

    double temp;
    for(int i=0;i<4096;i++)
    {
        temp=8191*cos((i/4096.00)*2*PI)+8191;
        dds_data[i]=(int)temp;
    }


    set_PM_wave(1,1,50,5.0);
    pl_bram_config(1000,0,1024); //BRAM DDS配置
    

    clk_wiz_cfg(XPAR_CLK_WIZ_0_BASEADDR,8.192);//采样时钟频率-->8.192MHz

        
    /* ★ 上电默认: CZT 关 -> ADC 单次触发点数 = SAMPLE_LEN = 16384, FFT 点数 = log2(16384) = 14。
       以前这里是三行分开写(ADC0/ADC1/FFT_INIT_D), 现在统一由 czt_pl_set(0) 成对写,
       这样"开 CZT 时改成 8192 / 关 CZT 时改回 16384"永远是一组, 不会再出现只改一半。 */
    czt_pl_set(0);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG10_OFFSET,0);


    XGpio_DiscreteWrite(&AXI_Gpio, 1, 0x0C);   /* 原开机脉冲(高两位=11), 保持原样 */
    /* 影子(在文件末尾)保持初值 0xFF = "未知", 下面 pl_awg_src_set() 一定会强制写一次 */
    pl_awg_src_set((AWG_BOOT_SRC_BRAM != 0) ? AWG_SRC_BRAM : AWG_SRC_DDS);   /* ★ 显式转换: 保持原行为 */


    

    u32 loops = 0;      /* 主循环计数: 只在前几帧打印采集状态 */
    while(1)
    {
        int adc_ok = 0;
        
        /* ★ 界面切换: 频谱仪必须用 8.192MHz(FFT 的 bin 间隔、频率轴、跨度都按它算) */
        if (app.screen != adc_fs_screen) {
            adc_fs_screen = app.screen;
            if (app.screen == UI_SCREEN_SPEC) {
                /* ★ 用设置窗第 2 项里选的那一档(默认 8.192MHz), 不再写死:
                   is_spec=1 允许小数档; 频率没变时 adc_fs_apply() 内部直接返回。 */
                double m = ui_spec_fs_mhz(&app.spec);
                adc_fs_apply((m > 0.0) ? m : ADC_FS_SPEC_MHZ, 1);
            } else {
                adc_fs_quiet = 3;                /* 回示波器/AWG: 换频后头几帧丢掉 */
                adc_fs_src_clk = 0u;             /* 换界面回来: 允许自动变速再试一次 */
                /* ★ 离开频谱仪 -> 立刻关 CZT:
                   示波器要 16384 点; FRA 的 FFT/逆变换都是 16384, 而且它的频率轴按 f/500 算,
                   都不能与 CZT 的 8192 共存。这里只清模式, 寄存器由下面"帧边界切换"统一改。 */
                app.spec.czt_mode  = 0u;
                app.spec.czt_popup = 0u;
            }
        }
        /* ★ 设置窗第 2 项"采样率": 用户按了上下箭头 -> 把新档写进采样时钟。
           只在帧边界这里动 MMCM(触摸处理里只置 fs_req, 不能在帧中间改时钟)。 */
        if (app.spec.fs_req != 0u) {
            double m = ui_spec_fs_mhz(&app.spec);

            app.spec.fs_req = 0u;
            if (m > 0.0) {
                adc_fs_apply(m, 1);                     /* 1 = 频谱仪, 允许小数档 */
                xil_printf("[fs] 频谱仪采样率 -> %d kHz\r\n", adc_fs_khz(m));
            }
        }
        /* ★ 滤波器页"采样率"按钮的请求: 与上面频谱仪那套完全并列 —— 触摸里只置 fs_req,
           这里在帧边界统一走 adc_fs_apply()(唯一改时钟入口, 内部才调 clk_wiz_cfg)。
           is_spec=0: 只允许整数档(5/6/8/16/32/48/64MHz), 正好是那张表里的整数频率。
           切完必须重新设计+下发滤波器系数(归一化截止 2*fc/fs 会变), 所以置 need_apply。 */
        if (app.flt.fs_req != 0u) {
            app.flt.fs_req = 0u;
            adc_fs_apply(ui_osc_fs_scope_at((int)app.flt.fs_idx), 0);
            app.flt.need_apply    = 1u;
            app.flt.redraw_params = 1u;
            app.flt.redraw_bar    = 1u;
            xil_printf("[fs] 滤波器页采样率 -> %d kHz\r\n", adc_fs_khz(adc_fs_mhz));
        }
        /* ★ 方案(a): 滤波器页显示的 fs 始终跟随【硬件真实值】—— 从频谱仪切过来时可能是
           6.144MHz 这类小数档, 这里把它对齐, 免得"显示 8.192 实际却是 6.144";
           并把 fs_idx 重算成最接近的整数档下标, 这样按钮的"下一档"不会从陈旧档位继续。
           只在真的不一致时才刷屏, 不产生额外开销。 */
        {
            s32 real_hz = (s32)((adc_fs_mhz * 1000000.0) + 0.5);

            if (app.flt.fs_hz != real_hz) {
                int nfs = ui_osc_fs_scope_count();
                int best = 0, bi;

                app.flt.fs_hz = real_hz;
                for (bi = 1; bi < nfs; bi++) {
                    double d1 = ui_osc_fs_scope_at(bi) - adc_fs_mhz;
                    double d0 = ui_osc_fs_scope_at(best) - adc_fs_mhz;

                    if (d1 < 0.0) { d1 = -d1; }
                    if (d0 < 0.0) { d0 = -d0; }
                    if (d1 < d0) { best = bi; }
                }
                app.flt.fs_idx = (u8)best;
                if (app.screen == UI_SCREEN_FLT) {
                    app.flt.redraw_params = 1u;
                    app.flt.redraw_bar    = 1u;
                }
            }
        }
        /* ★ Chirp-Z 的点数模式切换(帧边界):
           触摸处理里只改 app.spec.czt_mode, 这里才真正写三个寄存器 —— 与"采样率 fs_req"
           同一套规矩(触摸只置标志, 帧边界动手)。只在频谱仪界面允许打开; 切到别的界面时
           上面的切换分支已经把 czt_mode 清 0, 所以这里会自动切回 16384。 */
        {
            u8 want = (u8)(((app.screen == UI_SCREEN_SPEC) && (app.spec.czt_mode != 0u)) ? 1u : 0u);

            if (want != g_czt_on) {
                czt_pl_set((int)want);
                /* 点数变了: 让频谱下一帧整块重画, 否则逐列增量刷新会拿旧列高对比新 bin 数 */
                app.spec.prev_valid = 0u;
            }
        }
        /* ★ Chirp-Z 子窗口(save-under)与频谱的"增量刷新"冲突:
             频谱只重画"高度变了的那几列"(s->prev[x]), 而子窗口覆盖过的列被它认为
             "已经画对了" -> 关闭时贴回底图后仍会留下旧波形残迹。
           对策(本工程既有套路): 子窗口开着、或刚打开/刚关闭的那一帧, 强制频谱整块重画;
           子窗口在本帧最后画(ui_spec_draw_live 末尾), 所以不会被盖掉。 */
        {
            static u8 czt_pop_was = 0u;

            /* ★ 只在"开关状态发生变化"的那一帧整块重画。
               绝不能"开着就每帧整块重画": ui_spec_draw_plot() 开头会 lcd_fill_rect()
               清空绘图区, 把子窗口一起擦掉, 本帧末尾再重画 -> 每帧擦/画 = 屏幕一直闪。 */
            if (app.spec.czt_popup != czt_pop_was) {
                app.spec.prev_valid = 0u;
            }
            czt_pop_was = app.spec.czt_popup;
        }
        /* ★ 模式切换(关闭/模式1/模式2): 先把原来所有谱线整块擦掉, 再按新模式重画。
           prev_valid = 0 会让 ui_spec_draw_plot() 走整块重画(它开头 lcd_fill_rect 清绘图区,
           然后重新画每一列并重建 s->prev[]), 这正是"擦干净再画"。
           以前只在点数档(8192<->16384)变化时清屏, 模式1<->模式2 之间 want 不变 -> 不清屏
           -> 旧谱线残留。 */
        {
            static u8 czt_mode_was = 0u;

            if (app.spec.czt_mode != czt_mode_was) {
                ui_spec_czt_popup_invalidate();   /* ★ 底板作废: 按新模式重新存 */
                app.spec.prev_valid = 0u;
                czt_mode_was = app.spec.czt_mode;
            }
        }
        /* "自动"按钮提出的采样率变更请求(ui_osc_autoset 按测到的频率算好的) */
        /* ★ Chirp-Z mode1: throttled request to repaint ONLY the info row.
           value changed AND every 8 frames at most -> no flicker, tiny cost.
           info_req() repaints just that one row (see ui_spec_sync_chrome). */
        {
            static s32 last_shown = 0;
            static u32 disp_cnt   = 0;
            s32 now_fine = app.spec.czt_fine_hz;

            if ((g_czt_on == 0u) || (app.spec.czt_mode != 1u)) {
                last_shown = 0;                 /* off / other mode: reset so no stale text */
                disp_cnt   = 0u;
            } else if ((now_fine / 5) != (last_shown / 5)) {
                /* compare the value AS DISPLAYED (5Hz steps): a stable signal then
                   almost never repaints -> the info row stops blinking. */
                disp_cnt++;
                if (disp_cnt >= 16u) {
                    disp_cnt   = 0u;
                    last_shown = now_fine;
                    app.spec.info_req = 1u;     /* repaint info row only */
                }
            }
        }
        if (app.osc.fs_req_mhz1000 < 0) {               /* 按了"自动"但没测到频率 */
            app.osc.fs_req_mhz1000 = 0;
            xil_printf("[fs] 没测到频率(信号太小/没触发/通道关着), 采样率不变\r\n");
        }
        if (app.osc.fs_req_mhz1000 > 0) {
            double mhz = (double)app.osc.fs_req_mhz1000 / 1000.0;
            app.osc.fs_req_mhz1000 = 0;
            if (app.screen == UI_SCREEN_SCOPE) {
                /* ★ 用户按了"自动" = 明确要改采样率: 先清掉"停用自动变速"标志。
                   否则一旦被误判停用, 在示波器界面怎么按"自动"都不会再改采样率
                   (只有切界面才会清), 看起来就是"采样率锁死在 8.192MHz"。 */
                adc_fs_src_clk = 0u;
            }
            if ((app.screen == UI_SCREEN_SCOPE) && (adc_fs_src_clk == 0u)) {
                adc_fs_prev_mhz     = adc_fs_mhz;
                adc_fs_prev_is_spec = adc_fs_is_spec;
                adc_fs_prev_f_mhz   = app.osc.meas[app.osc.trig_src & 1u].freq_mhz;
                adc_fs_verify     = (adc_fs_prev_f_mhz > 0) ? 1u : 0u;
                adc_fs_apply(mhz, 0);          /* 0 = 示波器, 只允许表里的整数频率 */
                xil_printf("[fs] %d -> %d kHz, 改前测得 %d Hz\r\n",
                           adc_fs_khz(adc_fs_prev_mhz), adc_fs_khz(adc_fs_mhz),
                           (int)(adc_fs_prev_f_mhz / 1000));
            }
        }
        /* ★ 变速后校验: 信号频率本来不该跟着采样率变。
           变了 = 信号源工作在采样时钟域(板子 DAC 的 accumulator_fir/IIR 支路就是,
           它的 sys_clk 就是 clk_wiz_0 的输出), 再自动变速会形成正反馈:
           采样率↑ -> 信号频率↑ -> 下次算出的采样率更↑, 只能一路撞到上限才停。
           那就退回原采样率, 并且不再自动变速。 */
        /* ★ 自动变速校验只对示波器有意义: FRA 扫频时不做 */
        if ((adc_fs_verify != 0u) && (adc_fs_quiet == 0u) && (app.screen == UI_SCREEN_SCOPE)) {
            s64 f = app.osc.meas[app.osc.trig_src & 1u].freq_mhz;
            if (f > 0) {
                {   /* 测到的频率和"上次失败时的频率"差很多, 说明换了信号源: 允许再试 */
                    s64 df = (s64)f - (s64)adc_fs_prev_f_mhz;
                    if (df < 0) { df = -df; }
                    if ((adc_fs_src_clk != 0u) && ((df * 4) > (s64)adc_fs_prev_f_mhz)) {
                        adc_fs_src_clk = 0u;
                        xil_printf("[fs] 信号源好像换了, 允许自动变速再试一次\r\n");
                    }
                }
                s64 d = (s64)f - (s64)adc_fs_prev_f_mhz;
                if (d < 0) { d = -d; }
                if ((d * 4) > (s64)adc_fs_prev_f_mhz) {          /* 变化 > 25% */
                    /* ★ 只有"频率随采样率成比例变化"才真是同域信号源(板载 DAC 支路,
                       采样率↑ -> DDS 输出频率↑)。比例对不上多半是变速后那几帧测到的
                       是坏数据 —— 那种情况绝不能永久停用自动变速, 否则回到示波器后
                       按"自动"就再也不改采样率了(只有切界面才清标志)。 */
                    double rr = (double)f / (double)adc_fs_prev_f_mhz;
                    double gr = (adc_fs_prev_mhz > 0.0) ? (adc_fs_mhz / adc_fs_prev_mhz) : 1.0;

                    if ((rr > gr * 0.75) && (rr < gr * 1.25)) {
                    adc_fs_verify  = 0u;
                    adc_fs_src_clk = 1u;
                    xil_printf("[fs] 频率随采样率变了(%d -> %d Hz): 信号源与采样时钟同域,\r\n",
                               (int)(adc_fs_prev_f_mhz / 1000), (int)(f / 1000));
                    xil_printf("[fs] 退回 %d kHz 并停用自动变速\r\n", adc_fs_khz(adc_fs_prev_mhz));
                    adc_fs_apply(adc_fs_prev_mhz, adc_fs_prev_is_spec);
                    } else {
                        /* 比例不符: 数据不可信, 本次不回退也不停用, 让下次"自动"再试 */
                        adc_fs_verify = 0u;
                        xil_printf("[fs] 变速后测得 %d Hz 与 %d Hz 不成比例, 判为坏数据: 不回退也不停用\r\n",
                                   (int)(f / 1000), (int)(adc_fs_prev_f_mhz / 1000));
                    }
                } else {
                    adc_fs_verify = 0u;
                    xil_printf("[fs] 校验通过: 频率 %d Hz 不变\r\n", (int)(f / 1000));
                }
            }
        }
        if (adc_fs_quiet != 0u) {                /* MMCM 刚重锁, 这几帧数据不能要 */
            adc_fs_quiet--;
            (void)ui_touch_poll(LIVE_FB, &app);
            continue;
        }

        Rx0 = (int*)RxBufferPtr0;
        Rx1 = (int*)RxBufferPtr1;
        Tx2 = (long long int*)TxBufferPtr2;
        Rx2 = (long long int*)RxBufferPtr2;

        //DMA
        RxDone0 = 0;
        Error0 = 0;
        RxDone1 = 0;
        Error1 = 0;
        
        /* ★ 采集 DMA 长度必须与 ADC 单次触发点数一致: 点数 × 4 字节。
           原来写死 MAX_PKT_LEN(= 16384×4 = 65536), 恰好等于 16384 点的字节数, 所以一直没暴露;
           CZT 打开后 ADC 只触发 8192 点(= 32768 字节), DMA 却仍要 65536 -> 永远收不满
           -> RxDone0/1 不置位 -> adc_ok=0 -> 每帧被丢掉 -> 频谱那一帧根本不跑
           (表现就是: 一开 CZT 频谱就不动、信息栏步长还停在 500Hz)。 */
        {
            u32 alen = czt_adc_len() * 4u;

            Xil_DCacheFlushRange((UINTPTR)RxBufferPtr0, alen);
            Xil_DCacheFlushRange((UINTPTR)RxBufferPtr1, alen);

            XAxiDma_SimpleTransfer(&AxiDma0, (UINTPTR) RxBufferPtr0,
                                        alen, XAXIDMA_DEVICE_TO_DMA);
            XAxiDma_SimpleTransfer(&AxiDma1, (UINTPTR) RxBufferPtr1,
                                        alen, XAXIDMA_DEVICE_TO_DMA);
        }

        pl_adc_pulse();                        /* 原 0x03 / 1us / 0x00 三行: 现在会保留高两位(源) */


        Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone0);
        {
            Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone1);
            adc_ok = ((RxDone0 != 0u) && (RxDone1 != 0u)) ? 1 : 0;
        }
        loops++;


        /* ★ 两路 DMA 有一路没搬完(超时/出错)时, 缓冲里还是上一帧的旧数据。
           这种帧直接丢掉, 不要拿它去做触发/测量/显示 ——
           否则旧数据和新数据接在一起, 波形会"突然移了个相"地闪一下。 */
        if (adc_ok == 0) 
        {
            if ((loops < 40u) || ((loops % 100u) == 0u)) 
            {
                xil_printf("[adc] frame dropped (RxDone %u/%u)\r\n",
                           (unsigned)RxDone0, (unsigned)RxDone1);
            }
            (void)ui_touch_poll(LIVE_FB, &app);   /* 触摸还是要响应 */
            continue;
        }


        /* ★ 只拷贝本帧真正采到的点数(CZT 开 = 8192, 关 = 16384):
           多拷的部分是上一帧的旧数据, 混进来会让波形"莫名其妙移相"。 */
        for (u32 i = 0; i < czt_adc_len(); i++) {
            adc_buf0[i] = (s16)Rx1[i];
            adc_buf1[i] = (s16)Rx0[i];
        }





        if (app.screen == UI_SCREEN_SPEC)
        {
            const s16 *src = (app.spec.ch == 0u) ? adc_buf0 : adc_buf1;
            /* ★ 本帧的 FFT 点数: CZT 开 = 8192, 关 = 16384(= N)。
               下面所有"按点数计算"的地方都必须用 n, 不能再用宏 N:
               点数变了而公式没变, 幅度归一化会差 6dB、bin 数也会错。
               注意 static 数组 spec_mag 的尺寸仍写 N/2(常量, 取最大值 8192 个 bin),
               只有循环上界用 n/2 —— 8192 点时只用前 4096 个。 */
            u32 n = czt_nfft();
            s32 mean = 0;
            for (u32 i = 0; i < n; i++) mean += src[i];
            mean /= (s32)n;
            for (u32 i = 0; i < n; i++) {
                s32 v = (s32)src[i] - mean;              /* 不去直流的话 0Hz 会顶满 */
                /* ★ 这里必须先写成"32 位有符号"值: 窗函数是按 int 读数据的,
                   若按"低16位零扩展"写, -1000 会变成 +64536, 被窗函数里
                   "v > 2047 就限幅" 当成超量程 -> 负半周全被削成 +2047(半波整流),
                   送进 FFT 的波形是错的, 频谱自然全乱。窗后再压回打包格式即可。 */
                ((s32 *)TxBufferPtr2)[i] = (s32)(s16)v;
            }
            /* ★ CZT 也要去直流(频谱路径只把去均值写进了 TxBufferPtr2, 而 CZT 用的是原始 src):
               直流泄漏会直接落在 CZT 频段内(模式2 默认从 0Hz 起, 第一点就是 DC),
               并且不去直流会让 CZT 的幅度口径与频谱不一致。这里另存一份去均值的副本给 CZT 用,
               不加窗(保持 CZT 的窄带优势; 幅度口径与频谱的差异已在界面上通过"纵轴按参考电压"体现)。 */
            /* ★ 注意: 这两个变量必须声明在这里(而不是嵌套块里), 因为下面的 CZT 调用要用它们 */
            static s16 s_czt_in[16384];   /* 缓冲区保持 16384(只填前 8192 给 CZT 用), 留余量 */
            /* XXNOWIN: s_czt_win[16384] 已删除(不再对 CZT 输入加窗, 免得白占 64KB) */
            static float s_czt_wg = 1.0f;   /* XXWIN: 窗的相干增益补偿(否则加窗后幅度读数整体偏低) */
            u32 i5;

            for (i5 = 0; (i5 < n) && (i5 < 8192u); i5++) {   /* XXREV: 只填 8192(CZT 的 n=8192) */
                s_czt_in[i5] = (s16)((s32)src[i5] - mean);
            }
            /* ★ 按设置窗第 5 行选的窗调用 fft_windows.c 里【已有】的函数(不新写任何窗):
                 0=汉宁 hanning_windows()  1=汉明 hamming_windows()  2=布莱克曼 blackman_windows()
               三个函数的约定一致(输入 -2048..2047, 就地乘窗), 且必须在下面"压成 u16 打包格式"之前调用。 */
            switch (app.spec.win) {
            case 1u:  hamming_windows((int *)TxBufferPtr2, (int)n);  break;
            case 2u:  blackman_windows((int *)TxBufferPtr2, (int)n); break;
            case 3u:  break;      /* ★ 矩形窗 = 不加窗: 不调用任何窗函数(数据原样送去 FFT) */
            default:  hanning_windows((int *)TxBufferPtr2, (int)n);  break;
            }
            /* ★ 压回 FFT 输入要求的打包格式: 低16位=实部(16位补码), 高16位=虚部=0 */
            for (u32 i = 0; i < n; i++) {
                ((s32 *)TxBufferPtr2)[i] = (s32)((u16)(s16)((s32 *)TxBufferPtr2)[i]);
            }

            
            /* ★ FFT 的 DMA 传输全部交给 plfft 的 fft_pl_accelerate():
               它内部完成"长度寄存器写入 + 双缓冲 cache flush + 双向 SimpleTransfer
               + 等待完成 + 超时打印", 这里一行都不用再写。它按传入的 fft_len 设一切,
               所以 CZT 的 8192 / FRA 的 16384 都由各自的调用决定, 互不干扰。
               (TX: 每点 4 字节 低16位实部/高16位虚部; RX: 每点 8 字节 低32位实部/高32位虚部) */
            fft_pl_accelerate(TxBufferPtr2, RxBufferPtr2, (int)n, AxiDma2);
            

            if ((RxDone2 != 0u) && (Error2 == 0u)) 
            {
                static u16 spec_mag[N / 2];
                /* ★ 窗的相干增益补偿(否则换窗后频谱读数会整体偏高/偏低):
                   现在的幅度标定(4/n)是按【汉宁】的相干增益 0.5 定的,
                   汉明是 0.54 -> 读数会偏高 8%; 布莱克曼是 0.42 -> 偏低 16%。
                   这里按"0.5 / 该窗增益"换算, 让每种窗都用同一个电压标定:
                     汉宁 0.50 -> x1.0000   汉明 0.54 -> x0.9259   布莱克曼 0.42 -> x1.1905
                   (0.54 / 0.42 这两个常数就在 fft_windows.c 的窗公式里, 与窗函数同源) */
                {
                    float wg;

                    switch (app.spec.win) {
                    case 1u:  wg = 0.5f / 0.54f; break;      /* 汉明 */
                    case 2u:  wg = 0.5f / 0.42f; break;      /* 布莱克曼 */
                    case 3u:  wg = 0.5f / 1.00f; break;      /* 矩形(相干增益 1.0; 不补偿会大一倍) */
                    default:  wg = 1.0f;         break;      /* 汉宁 */
                    }
                    for (u32 i = 0; i < n / 2u; i++) {
                        s32 re = (s32)((u32)(Rx2[i] & 0xFFFFFFFFu));
                        s32 im = (s32)((u32)(Rx2[i] >> 32));
                        float m = sqrtf((float)re * (float)re + (float)im * (float)im);
                        float a = (4.0f * m / (float)n) * (3.0f / 3.07f);   /* XXCAL: 频谱显示幅值统一 x3/3.07(外部标定, 见下 3 处同源) */

                        a *= wg;                              /* ★ 归一到同一电压标定 */
                        if (a > 65535.0f) a = 65535.0f;
                        spec_mag[i] = (u16)(a + 0.5f);   /* ★ 四舍五入(原来是截断: 小信号系统性偏低, 100mVpp 时偏低 5%) */
                    }
                }
                {   /* this frame's real spectral peak, taken from spec_mag directly.
                       Why not use app.spec.peak_hz here: that one is refreshed on the DRAW
                       path (sp_find_peaks), so at this point in the frame it is still the old
                       value (measured: 1000 Hz) -> the CZT band was placed at 1 kHz and gave
                       garbage. Computing it here makes the band follow THIS frame's data. */
                    u32 kp = 0u, i2;
                    u16 mp = 0u;

                    for (i2 = 0u; i2 < n / 2u; i2++) {
                        if (spec_mag[i2] > mp) { mp = spec_mag[i2]; kp = i2; }
                    }
                    if (kp >= 1u) {
                        app.spec.peak_hz = (s32)((double)kp * adc_fs_mhz * 1000000.0 / (double)n + 0.5);
                    }
                }
                ui_spec_set_spectrum(&app.spec, spec_mag, n / 2u,
                                     (s32)(adc_fs_mhz * 1000000.0 + 0.5));

                /* ★ Chirp-Z 模式1(每帧精测): 在频谱找到的峰值附近做一次细化 Z 变换。
                   本步只把结果打到串口核对(不显示), 确认精测频率正确后再接界面。
                   ★★ 点数关系(硬约束, 写错会让 PL 的 FFT 档位来回切 -> DMA 全部超时):
                       czt.c 里 L = next_pow2(n + m - 1)  = CZT 内部 PL FFT 的长度
                       CZT 开时 PL 的 FFT 是 8192 档  =>  必须让 L 也等于 8192
                       => n + m - 1 <= 8192,  m = 201  =>  n <= 7992, 这里取 n = 7936
                          (7936 + 201 - 1 = 8136 -> L = 8192 ✓)
                       n 只是"用前多少个采样点", <= ADC 触发点数(8192) 即可。
                   频段 = peak ± bins·bin; czt_cached() 按 plan 自动复用啁啾缓存。 */
                /* ★ peak_hz must be refreshed AFTER ui_spec_set_spectrum():
                   that call internally runs sp_find_peaks() and OVERWRITES peak_hz
                   (measured: it sets 1000 Hz). So recompute the real peak from THIS
                   frame's spec_mag here, then the CZT band below follows the true peak. */
                {
                    u32 kp2 = 0u, i3;
                    u16 mp2 = 0u;

                    for (i3 = 0u; i3 < n / 2u; i3++) {
                        if (spec_mag[i3] > mp2) { mp2 = spec_mag[i3]; kp2 = i3; }
                    }
                    if (kp2 >= 1u) {
                        app.spec.peak_hz = (s32)((double)kp2 * adc_fs_mhz * 1000000.0 / (double)n + 0.5);
                    }
                }
                app.spec.czt_fine_hz = 0;      /* clear each frame; set below when valid */
                app.spec.czt2_n      = 0u;     /* 模式2: 每帧清零, 算出来才填 */
                if ((app.spec.czt_mode == 2u) && (g_czt_on != 0u)) {
                    /* XXWIN2: CZT 输入加窗 —— 只在这里(模式2 分支内)做!
                       原来它在这个 if 之外, 而 s_czt_in 是【模式1/2 共用】的 -> 模式1 也被加窗且没有补偿(读数减半/-6dB)。
                       窗与频谱仪普通模式的设置(app.spec.win)同步; 用你自己写好的 fft_windows.c 函数(约定 -2048..2047 就地乘),
                       因为 s_czt_in 是 s16 而它们要 int*, 所以经 int 临时缓冲再拷回。s_czt_wg = 1/相干增益(矩形口径无需补偿)。 */
            /* XXNOWIN: CZT 输入【不加窗】(加窗会改变主瓣形状/幅度口径, 实测把 8.192MHz 的图像弄错了 -> 回退)。
               s_czt_wg 保持 1.0 -> 幅度标定与原来相同。 */
            s_czt_wg = 1.0f;
                    /* ★ Chirp-Z 模式2(自定义频段): 用子窗口里填的 czt_f_start/f_end/m 做一次 CZT,
                       结果按【Vpp】存进 czt2_* (显示由 ui_spec 那边按 a 方案替换频谱曲线)。
                       硬约束: L = next_pow2(n+m-1) 必须 = 8192(与 PL 的 8192 档一致),
                   取 n = 8192、m <= 8193 ⇒ 8192+8193-1 = 16384 ⇒ L = 16384 ✓ */
                    static double czt2_abs[8193];   /* ★ 257->8193: 与新的 m 上限配套 */
        static double czt2_ema[8193];   /* XXEMA: 峰值比较用的滑动平均(时间平均降噪, 不插值) */
        static int    czt2_ema_ok = 0;  /* XXEMA: 是否已用真实数据初始化过 */
        static s32    czt2_ema_f0 = -1, czt2_ema_df = -1;   /* XXEMARST: 上一次的频段/栅格, 变了就复位平均谱 */
        static int    czt2_ema_m  = -1;
        static double czt2_ema_fs = -1.0;  /* XXFSRST: 上一次的采样率(Hz), 变了也要复位平均 */
        /* XXBOXAVG: 频率显示改成【普通等权平均】(环形缓冲 32 帧)。原来是 alpha=1/8 的滑动平均, 每帧跟新太快 -> 显示跳动快。
           CZT2_FAVG_N 改成 0 或 1 就等于【不平均】(直接显示 czt() 的返回值)。 */
#ifndef CZT2_FAVG_N
#define CZT2_FAVG_N   32u
#endif
        static double czt2_fbuf[CZT2_FAVG_N > 0u ? CZT2_FAVG_N : 1u];
        static u32    czt2_fn   = 0u;     /* 已填入的帧数(<=CZT2_FAVG_N) */
        static u32    czt2_fpos = 0u;     /* 环形写位置 */
        static double czt2_fsum = 0.0;   /* 当前有效帧之和 */
        static int    czt2_favg_ok = 0;   /* XXFAVG: 是否已初始化 */
                    czt_plan_t    cp2;
                    double        fs2 = adc_fs_mhz * 1000000.0 * (1.0 + ADC_FS_PPM_CORR / 1000000.0);   /* XXPPM: 频率刻度校正 */
                    int           m2  = (int)app.spec.czt_m;
                    int           i6;


                    if (m2 < 2)   { m2 = 2; }
                    if (m2 > 8192) { m2 = 8192; }   /* XXREV: n=8192 时 m 上限 8193, 取 8192 安全 */     /* ★ 257->8192: n=8192 时 m<=8193 (L<=16384) */
                    cp2.fs      = fs2;
                    cp2.f_start = (double)app.spec.czt_f_start;
                    cp2.f_end   = (double)app.spec.czt_f_end;
                    cp2.n       = 8192;    /* XXREV: 退回 8192 —— n=16384 会让 L=next_pow2(n+m-1) 超过 CZT_MAX_L(16384) 使 czt() 永远失败 */      /* ★ 用满 s_czt_in[8192]; L=next_pow2(8192+m-1) <= 16384 */
                    cp2.m       = m2;
                    if (cp2.f_start < 0.0) { cp2.f_start = 0.0; }
                    if (cp2.f_end > (fs2 / 2.0)) { cp2.f_end = fs2 / 2.0; }
                    if ((cp2.f_end - cp2.f_start) < 1.0) { cp2.f_end = cp2.f_start + 1000.0; }
                    /* XXCZTFREQ: 按你的要求, 模式2 的频率【直接取 czt() 的返回值】(*)
                       czt() 内部仍会把幅度写进第三个参数 czt2_abs, 所以曲线/幅度/下标一切照旧;
                       注意 czt() 每次调用都会重建旋转因子表(约 2.4 万次 sin/cos, 25~50ms), 这是按你的要求用的。 */
                    int czt2_freq = czt(cp2, (int16_t *)s_czt_in, czt2_abs, AxiDma2);
                    if (czt2_freq > 0) {                    /* 你的 czt() 出错时 return 0 */
                        double df2 = (cp2.f_end - cp2.f_start) / (double)(cp2.m - 1);

                        app.spec.czt2_dirty = 1u;   /* ★ 数据变了: 让曲线下一帧整块重画一次 */
                        app.spec.info_req   = 1u;   /* ★ 信息栏的 Z2峰 也要跟着更新(轻量刷一行) */
                        app.spec.czt2_n  = (u16)cp2.m;
                        app.spec.czt2_f0 = (s32)(cp2.f_start + 0.5);
                        app.spec.czt2_span = (s32)((cp2.f_end - cp2.f_start) + 0.5);   /* ★ 底部刻度直接用未取整的带宽(不再由 mHz x (m-1) 反推, 免得末端差几 Hz) */
                        {   /* ★ mHz 后要夹一下: m 很小(如 m=2)而带宽很大时 df2*1000 会超过 s32 上限(2.147e9) -> 变负数 -> 读数/刻度全乱 */
                            double dfm = df2 * 1000.0;

                            if (dfm > 2.1e9) { dfm = 2.1e9; }
                            app.spec.czt2_df = (s32)(dfm + 0.5);
                        }
                        for (i6 = 0; i6 < cp2.m; i6++) {
                            /* ★ 存【原始幅度】(与频谱的 spec_mag 同一口径): 电压换算只在显示处
                               做一次(sp_fmt_volt)。以前这里先乘 mv_fs/2048, 显示时又乘一次
                               -> 读数被放大 5.04 倍(实测 3Vpp 显示成 15.5Vpp)。 */
                            double v6 = czt2_abs[i6];

                            if (v6 < 0.0)     { v6 = 0.0; }
                            /* XXDBL: 这里原来把 v6 截断到 65535(u16 时代的上限)。czt2_mv 已改成 double, 这个截断不再需要, 去掉后模式2 的峰值幅度保留全部小数位(double ~15 位有效数字)。 */
                            app.spec.czt2_mv[i6] = v6 * (3.0 / 3.07) * (double)s_czt_wg;   /* XXDBL: 存 double, 不再压成 u16(去掉 0.033 码的量化) */   /* XXCAL + XXWIN: 同一把尺 x 窗的相干增益补偿 */
                        }
                        /* ★ 峰值必须在【全精度】的 czt2_abs[] 上求, 不能用取整后的 czt2_mv[]:
                           m=201、带宽 2000Hz 时每点是 10Hz, 主瓣顶部相邻几点的幅值差只有零点几,
                           取整后谁大谁小完全由舍入决定 -> 峰频在相邻 bin 之间乱跳
                           (用户实测: CZT 幅值要保留到小数点后 5 位, 峰位置才稳定)。 */
                        {
                            int kp2 = 0;

        /* XXEMA: 把本帧谱值做指数滑动平均(alpha=1/4), 再在平均谱上求峰。
           理由: m 大时 df 很细(1Hz 级), 主瓣顶几十个 bin 只差零点几个码,
           谁最大完全由 12bit 量化噪声决定 -> argmax 每帧乱跳(读数十分位/百分位狂跳)。
           平均后噪声降约 √4=2 倍, 峰位稳定; 读数仍取【真实 bin】= 不插值。 */
        /* XXEMARST: 频段/栅格/点数一变, 平均谱就作废(否则混着旧频段的形状 -> 读数滞后+错位)。 */
        if ((app.spec.czt2_f0 != czt2_ema_f0) || (app.spec.czt2_df != czt2_ema_df) || (m2 != czt2_ema_m) || (fs2 != czt2_ema_fs)) {
            czt2_ema_ok = 0;
            czt2_ema_f0 = app.spec.czt2_f0;
            czt2_ema_df = app.spec.czt2_df;
            czt2_ema_m  = m2;
            czt2_ema_fs = fs2;   /* XXFSRST: 记下本次采样率 */
            czt2_favg_ok = 0;   /* XXBOXAVG: 换频段/点数/采样率 -> 频率平均也作废 */
            czt2_fn = 0u; czt2_fpos = 0u; czt2_fsum = 0.0;
        }
        if (czt2_ema_ok == 0) {
            for (i6 = 0; i6 < cp2.m; i6++) { czt2_ema[i6] = czt2_abs[i6]; }
            czt2_ema_ok = 1;
        } else {
            for (i6 = 0; i6 < cp2.m; i6++) { czt2_ema[i6] = (czt2_ema[i6] * 15.0 + czt2_abs[i6]) * (1.0 / 16.0); }   /* XXEMA16: alpha 1/4->1/16 (等效平均约 31 帧): 主瓣顶 argmax 抖动再降约 2 倍(5Hz -> 1.2~2.5Hz)。代价: 改频率后读数约 0.3~0.6 秒才稳。 */
        }
        for (i6 = 1; i6 < cp2.m; i6++) { if (czt2_ema[i6] > czt2_ema[kp2]) { kp2 = i6; } }   /* XXEMA: 改用平均谱 */
        /* XXHYS-REMOVED: 死区/滞回已撤销 —— 它会把读数卡在旧的 bin 上(新峰必须比旧峰高 0.25 码才换位), 导致"读数跟不上真实频率"。仍然只用平均谱 + 直接 argmax(不插值)。 */
                            app.spec.czt2_peak_i  = (u16)kp2;
                            /* XXFAVG: czt() 返回的峰值频率【先做平均, 再显示】; 平均方式见下一段 XXBOXAVG。 */
                            /* XXBOXAVG: 普通等权平均(最近 CZT2_FAVG_N 帧的算术平均) —— 原来是指数滑动平均(alpha=1/8), 每帧跟新太快导致显示跳。 */
                            if (czt2_favg_ok == 0) { czt2_favg_ok = 1; czt2_fn = 0u; czt2_fpos = 0u; czt2_fsum = 0.0; }
                            if (czt2_fn < CZT2_FAVG_N) {
                                czt2_fbuf[czt2_fpos] = (double)czt2_freq;
                                czt2_fsum += (double)czt2_freq;
                                czt2_fn++;
                            } else {
                                czt2_fsum -= czt2_fbuf[czt2_fpos];
                                czt2_fbuf[czt2_fpos] = (double)czt2_freq;
                                czt2_fsum += (double)czt2_freq;
                            }
                            czt2_fpos = (u32)((czt2_fpos + 1u) % (CZT2_FAVG_N ? CZT2_FAVG_N : 1u));   /* % 0 是除零, 用三目保护(设 0 = 不平均时也安全) */

                            app.spec.czt2_peak_hz = (s32)((czt2_fsum / (double)czt2_fn) + 0.5);

                        }
                    }
                }
                if ((app.spec.czt_mode == 1u) && (g_czt_on != 0u) && (app.spec.peak_hz > 0)) {
                    static double  czt_abs[201];
                    czt_plan_t     cp;
                    double         fs_hz = adc_fs_mhz * 1000000.0;
                    double         dfbin = fs_hz / (double)n;        /* 当前 bin: CZT 开=1000Hz */
                    double         half  = (double)app.spec.czt_peak_bins * dfbin;

                    /* ★ czt_peak_bins 必须有下限: 它为 0 时 half=0 -> f_start=f_end
                       -> 频段宽度为 0 -> 201 个点同一个频率, 结果全是垃圾(表现为 k 恒=0)。
                       界面上的 +-N bin 未点过时它是 0, 所以这里兜底成 2。 */
                    if (app.spec.czt_peak_bins < 1u) { app.spec.czt_peak_bins = 2u; }

                    cp.fs      = fs_hz;
                    cp.f_start = (double)app.spec.peak_hz - half;
                    cp.f_end   = (double)app.spec.peak_hz + half;
                    cp.n       = 8192;       /* ★ 7936->8192: 与频谱的 16384 档一致, 避免每帧来回切档 */
                    cp.m       = 201;
                    if (cp.f_start < 0.0) { cp.f_start = 0.0; }

                    if (czt_cached(cp, (int16_t *)s_czt_in, czt_abs, AxiDma2) == 0) {     /* ★ 用去直流副本 */
                        double dfc = (cp.f_end - cp.f_start) / (double)(cp.m - 1);
                        int    kk  = czt_max_index(czt_abs, cp.m);

                        app.spec.czt_fine_hz = (s32)(cp.f_start + (double)kk * dfc + 0.5);
                        app.spec.czt_fine_db = ui_spec_czt_to_db10(&app.spec, (u32)((czt_abs[kk] + 0.5) * (3.0 / 3.07)));   /* XXCAL: 与 mag 同步缩放(否则 mag/dB 两个读数不一致) */
                        app.spec.czt_fine_mag = (u16)(czt_abs[kk] * (3.0 / 3.07) + 0.5);   /* XXCAL: 模式1 读数同尺 */

                    }
                }
            }
        }
        

        /* ★ 只在真的要写屏时才等帧起点: 扫频中不画图的帧直接跳过(提速的关键) */
        /* ★ 刚扫完: 显示"运行结束", 停 1 秒后自动关窗并恢复正常刷新 */
        if ((app.screen == UI_SCREEN_FRA) && (app.fra.run_win == 2u)) {
            /* ★ "运行结束"要真的看得见: 原来画完【同一圈】就置 redraw_pane=1, 紧接着就被
               fra_redraw_pane() 整块铺底覆盖, 用户几乎看不到。这里加一个保持计数器:
               先画"运行结束", 保持约 10 圈(一圈≈2ms ⇒ 20~30ms)再收窗并恢复刷新。
               保持期间必须清掉 redraw_curve, 否则每圈仍会重画曲线区把窗口擦掉。 */
            static u8 fra_end_hold = 0u;

            if (fra_end_hold == 0u) {
                /* ★ 这里【不能】把 run_win 置 3: 否则外层 if(run_win==2) 下一圈就不再成立,
                   保持计数器永远不会走完 -> "运行结束"永久留在屏上且刷新不恢复。 */
                wait_vdma_frame();
                ui_fra_run_win_draw(LIVE_FB, &app.fra, 1);
                ui_touch_draw_bar(LIVE_FB, &app, UI_BTN_NONE);   /* 按钮名恢复"运行" */
                fra_end_hold = 10u;
                app.fra.redraw_curve = 0u;   /* ★ 同一圈 fra_sweep_step 会置 redraw_curve, 若不清, 本帧 ui_app_draw_live 会用 800x367 铺底把刚画的"运行结束"擦掉 */
            } else {
                app.fra.redraw_curve = 0u;          /* 保持期间别把窗口擦掉 */
                fra_end_hold--;
                if (fra_end_hold == 0u) {
                    app.fra.run_win = 0u;
                    app.fra.redraw_pane = 1u;       /* 收窗后一次画全曲线 */
                    app.fra.redraw_curve = 0u;
                }
            }
        }
        /* ★ 扫频运行中: 冻结屏幕刷新; 起扫第一帧弹出"正在运行......"窗口。
           触摸仍由下面的 ui_touch_poll 处理 -> "运行"按钮可点来终止。 */
        if ((app.screen == UI_SCREEN_FRA) && (app.fra.running != 0u)) {
            if (app.fra.run_win == 1u) {
                app.fra.run_win = 3u;
                wait_vdma_frame();
                ui_fra_run_win_draw(LIVE_FB, &app.fra, 0);
            }
        } else if (ui_need_paint() != 0) {
            wait_vdma_frame();             /* 从帧起点附近开始写, 保证写在下一次扫描之前 */
        }

        if ((app.screen == UI_SCREEN_FRA) && (app.fra.running != 0u)) {
            /* ★ 扫频中【完全冻结屏幕】: 这里一个像素都不画(用户要求运行时只保留"停止"可用)。
               "正在运行"子窗口在上面的 run_win==1 分支里已经画过一次, 之后屏幕保持不动,
               扫完后由 run_win==2 的分支画"运行结束"再统一恢复刷新。 */
        } else if (ui_app_need_redraw()) {
            /* 切换界面: 在帧边界整屏重画, 写到一半不会被液晶扫到 */
            ui_app_draw(LIVE_FB, &app);
            ui_touch_draw_bar(LIVE_FB, &app, UI_BTN_NONE);
            lcd_flush(LIVE_FB);
            ui_app_clear_redraw();
        } else if (ui_app_win_need_redraw()) {
            /* 开/关弹窗("界面"菜单 或 频谱仪"设置"子窗口): 只动窗口那一块
               (存底板/贴底板/画窗口), 不整屏重画 -> 不闪。
               两个窗口共用这一套; 具体开哪个由 app 状态决定(见 ui_app_win_refresh)。 */
            ui_app_win_refresh(LIVE_FB, &app);
            ui_app_win_clear_redraw();
        } else if (ui_app_win_open(&app) == 0) {
            /* 有弹窗开着时先不刷新底下波形, 免得把窗口冲掉(也是底板能逐像素贴回的前提) */
            ui_app_draw_live(LIVE_FB, &app);   /* 擦旧迹+补网格+画新迹+更新测量值 */
            ui_app_flush_live(LIVE_FB, &app);
        }

        (void)ui_touch_poll(LIVE_FB, &app);

        /* ★ 信号发生器: 界面参数变了就下发到 PL(只在变化时, 绝不每帧写)。
           FRA 扫频期间跳过: 那时 CH2(DAC2) 归 FRA 控制, 不能被界面参数覆盖。 */
        /* ================= ★ 数字滤波器: 参数变了就重新设计+下发 ================= */
        if (app.flt.need_apply != 0u) {
            app.flt.need_apply    = 0u;
            filt_apply_from_ui(&app.flt);       /* 只调用你写的 filter.c / fir / iir 函数 */
            app.flt.redraw_params = 1u;         /* 把 coef_n/coef_max/q_shift/status 显示出来 */
        }
        /* ★ 只要滤波器输出开着, 每帧都钉住 DAC 源(与当前在哪个页面无关)
           -> 在信号发生器页也抢不走 DAC; 同时把信号发生器两路关掉。 */
        if (app.flt.out_on != 0u) {
            /* ★ 只关 ch[0](DAC1 那一路) —— 不能连 ch[1] 一起关:
               BD 已核实 wave_sel 的 sel 只门控 DAC1(my_dds_0/dac_data_2 是直连 DAC2 的),
               而 DAC2 正是 FRA 扫频的激励源。以前两路都关 -> 滤波器输出开着时跑 FRA,
               DAC2 被静默掐掉, 扫出来的幅频/相频全是垃圾且没有任何提示。
               ch[1] 是独立的一路输出, 与"滤波器占用 DAC1"无关, 保持用户设置即可。 */
            /* ★ 这里【不要】去清 app.awg.ch[0].on —— 那是销毁式的: 一清就会让
               awg_apply_from_ui() 把 CH1 的幅度写成 0, 于是"滤波器输出关掉之后
               信号发生器也出不来声了"(必须重新按一次"输出", 而且选中的是 CH2 时
               按下去只会开 CH2 -> CH1 永远不响)。
               正确做法: 只把 DAC1 的源位钉到滤波器(下面那行), AWG 自己的参数原样保留;
               这样 out_on 一变 0, 下一帧就自动恢复成信号发生器的输出, 无需任何额外操作。 */
            /* ★ 互斥规则: 滤波器输出开着 => 禁用信号发生器【对 DAC1 的输出】(只关 CH1)。
               ★ 界面同步必须【沿触发】: 只在 out_on 或 AWG 开关状态发生变化的那一帧置脏标志。
                 以前这里每帧无条件置位 -> 站在 AWG 页/滤波器页就会每帧全量重画
                 (预览 800x264 / 参数区 800x368) + 整行 cache flush, 正是闪屏的根源。 */
            {
                static u8 s_flt_prev_on  = 0u;
                static u8 s_flt_prev_awg = 0u;
                u8 awg_any = (u8)(((app.awg.ch[0].on != 0u) || (app.awg.ch[1].on != 0u)) ? 1u : 0u);

                if ((s_flt_prev_on != app.flt.out_on) || (s_flt_prev_awg != awg_any)) {
                    app.awg.redraw_preview = 1u;
                    app.flt.redraw_params  = 1u;
                }
                s_flt_prev_on  = app.flt.out_on;
                s_flt_prev_awg = awg_any;
            }
            pl_awg_src_set(ui_filter_sel(&app.flt));
        } else {
            /* ★ 输出关: 把 DAC1 交回信号发生器。
               ★ 这里必须【每帧无条件】写一次, 不能再加 if (awg.need_apply == 0u) 那种守卫:
                 守卫 + 下面 awa_apply 的 fra.running 门控叠加时, 会出现
                 "need_apply==1 且正在扫频 -> 本帧谁都不写 GPIO[3:2]" 的窗口,
                 DAC 会冻在上一次的 FIR/IIR 上(屏幕却显示两边都关)。
               值取自信号发生器自己的选择: BRAM 波形 -> 11, 其它 -> 00。
               注: wave_sel 的 sel 只门控 DAC1(BD 已核实: my_dds_0/dac_data_2 直连 DAC2),
                   所以 FRA 靠 DAC2 的激励不受这里影响。 */
            pl_awg_src_set((ui_awg_is_bram(app.awg.ch[0].wave) != 0u) ? AWG_SRC_BRAM : AWG_SRC_DDS);
        }

        if ((app.awg.need_apply != 0u) && (app.fra.running == 0u)) {
            awg_apply_from_ui(&app.awg);
        }

        /* ★ 频率特性分析仪: 扫频一步(走 PL FFT, 每帧一步, 不阻塞) */
        fra_sweep_step();
    }   
    

    RxBufferPtr0=NULL;
    RxBufferPtr1=NULL;
    free(RxBufferPtr0);
    free(RxBufferPtr1);

    disable_intr_system(&Intc, RX_INTR_ID1,RX_INTR_ID1);

    cleanup_platform();
    return 0;
}


//AXI DMA0接收完成的中断处理函数
static void RxIntrHandler0(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//接收出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error0 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone0 = 1;
	}
}

//AXI DMA1接收完成的中断处理函数
static void RxIntrHandler1(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//接收出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error1 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone1 = 1;
	}
}

//AXI DMA2发送完成的中断处理函数
static void TxIntrHandler2(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//发送出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error2 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		TxDone2 = 1;
	}
}

//AXI DMA2接收完成的中断处理函数
static void RxIntrHandler2(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//接收出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error2 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone2 = 1;
	}
}


//DMA初始化函数
void dma_init(void)
{
    XAxiDma_Config *config0;
    XAxiDma_Config *config1;
    XAxiDma_Config *config2;

    config0 = XAxiDma_LookupConfig(XPAR_AXI_DMA_1_BASEADDR);

    //初始化DMA引擎
    XAxiDma_CfgInitialize(&AxiDma0, config0);

    
    if (XAxiDma_HasSg(&AxiDma0)) 
    {
        xil_printf("Device configured as SG mode \r\n");
    }  


    config1 = XAxiDma_LookupConfig(XPAR_AXI_DMA_2_BASEADDR);

    XAxiDma_CfgInitialize(&AxiDma1, config1);

    if (XAxiDma_HasSg(&AxiDma1)) 
    {
        xil_printf("Device configured as SG mode \r\n");
    }
    

    config2 = XAxiDma_LookupConfig(XPAR_AXI_DMA_3_BASEADDR);

    XAxiDma_CfgInitialize(&AxiDma2, config2);

    if (XAxiDma_HasSg(&AxiDma2)) 
    {
        xil_printf("Device configured as SG mode \r\n");
    }
}


//串口初始化函数
int uart_init(XUartPs* uart_Ps)
{
    int Status;
    XUartPs_Config *Config;


    Config = XUartPs_LookupConfig(XPAR_UART1_BASEADDR);
    XUartPs_CfgInitialize(uart_Ps, Config, Config->BaseAddress);

    //硬件自检
    Status = XUartPs_SelfTest(uart_Ps);
    if(Status != XST_SUCCESS)
    {
        return XST_FAILURE;
    }

    //设置串口工作模式
    XUartPs_SetOperMode(uart_Ps, XUARTPS_OPER_MODE_NORMAL);

    //设置波特率
    XUartPs_SetBaudRate(uart_Ps, 115200);

    //设置接受FIFO的触发阈值
    XUartPs_SetFifoThreshold(uart_Ps, 1);    

    return Status;
}


//UART中断处理函数
void uart_intr_handler(void *CallbackRef)
{
    u32 IsrStatus; 
    u32 recv_data = 0;    

    XUartPs *uart_instance_ptr = (XUartPs *)CallbackRef;

    //获取中断状态寄存器的值
    IsrStatus = XUartPs_ReadReg(uart_instance_ptr -> Config.BaseAddress, XUARTPS_IMR_OFFSET);

    IsrStatus &= XUartPs_ReadReg(uart_instance_ptr -> Config.BaseAddress, XUARTPS_ISR_OFFSET);

    if(IsrStatus & XUARTPS_IXR_RXOVR)
    {
        recv_data = XUartPs_RecvByte(uart_instance_ptr -> Config.BaseAddress);

        //清除中断状态位
        XUartPs_WriteReg(uart_instance_ptr -> Config.BaseAddress, XUARTPS_ISR_OFFSET, XUARTPS_IXR_RXOVR);         
    }

    if(recv_data == 'a')
    {
        printf("11111");
    }
}


//中断初始化函数
static int intr_init(void)
{
    int status;
    XScuGic_Config *intc_cfg;
    
    //初始化中断控制器
    intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
    if (NULL == intc_cfg) {
        return XST_FAILURE;
    }

    //中断初始化
    status = XScuGic_CfgInitialize(&Intc, intc_cfg, intc_cfg -> CpuBaseAddress);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    //设置优先级和触发类型
    XScuGic_SetPriorityTriggerType(&Intc, RX_INTR_ID0, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(&Intc, RX_INTR_ID1, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(&Intc, TX_INTR_ID2, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(&Intc, RX_INTR_ID2, 0xA0, 0x3);

    //关联中断ID和中断处理函数
    status = XScuGic_Connect(&Intc, RX_INTR_ID0,
            (Xil_InterruptHandler) RxIntrHandler0, &AxiDma0);
    if (status != XST_SUCCESS) {
        return status;
    }
    
    status = XScuGic_Connect(&Intc, RX_INTR_ID1,
            (Xil_InterruptHandler) RxIntrHandler1, &AxiDma1);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XScuGic_Connect(&Intc, TX_INTR_ID2,
            (Xil_InterruptHandler) TxIntrHandler2, &AxiDma2);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XScuGic_Connect(&Intc, RX_INTR_ID2,
            (Xil_InterruptHandler) RxIntrHandler2, &AxiDma2);
    if (status != XST_SUCCESS) {
        return status;
    }
            
    status = XScuGic_Connect(&Intc, UART_INT_IRQ_ID,
            (Xil_InterruptHandler) uart_intr_handler, &Uart_Ps);
    if (status != XST_SUCCESS) {
        return status;
    }

    //设置并打开UART中断异常处理功能
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler) XScuGic_InterruptHandler,(void *) &Intc);
    Xil_ExceptionEnable();

    //设置UART中断触发方式
    XUartPs_SetInterruptMask(&Uart_Ps, XUARTPS_IXR_RXOVR);

    XAxiDma_IntrDisable(&AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrDisable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    
    //使能中断
    XScuGic_Enable(&Intc, UART_INT_IRQ_ID);
    XScuGic_Enable(&Intc, RX_INTR_ID0);
    XScuGic_Enable(&Intc, RX_INTR_ID1);
    XScuGic_Enable(&Intc, TX_INTR_ID2);
    XScuGic_Enable(&Intc, RX_INTR_ID2);

    XAxiDma_IntrDisable(&AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrDisable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

    //使能DMA中断
    XAxiDma_IntrEnable(&AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(&AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrEnable(&AxiDma2, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

    /*
    reg_config=XAxiDma_ReadReg(XPAR_XAXIDMA_0_BASEADDR, 0x30);
    reg_config=(reg_config|0x5000);
    XAxiDma_WriteReg(XPAR_XAXIDMA_0_BASEADDR, 0x30,reg_config);

    reg_config=XAxiDma_ReadReg(XPAR_XAXIDMA_1_BASEADDR, 0x30);
    reg_config=(reg_config|0x5000);
    XAxiDma_WriteReg(XPAR_XAXIDMA_1_BASEADDR, 0x30,reg_config);
    */
    
    return XST_SUCCESS;
}


static void disable_intr_system(XScuGic * int_ins_ptr, u16 rx1_intr_id,
        u16 rx2_intr_id)
{
    XScuGic_Disconnect(int_ins_ptr, rx1_intr_id);
    XScuGic_Disconnect(int_ins_ptr, rx2_intr_id);
}





float dmadata_2_realvoltage_vp(float data)
{
    float voltage;
    voltage=data*0.0024414;
    return voltage;
}






/* 等一个 MM2S 帧完成(轮询帧计数,无需 GIC) */
static int wait_vdma_frame(void)
{
    u32 pend, t;
    for (t = 0; t < 2000000UL; t++) {        /* 上限放宽到约几百ms即可 */
        pend = XAxiVdma_IntrGetPending(&vdma, XAXIVDMA_READ);
        if (pend & XAXIVDMA_IXR_FRMCNT_MASK) {
            XAxiVdma_IntrClear(&vdma, XAXIVDMA_IXR_FRMCNT_MASK, XAXIVDMA_READ);
            return XST_SUCCESS;
        }
    }
    xil_printf("wait frame timeout\r\n");
    return XST_FAILURE;
}

void LCD_init(void)
{
    (void)touch_init();

    ui_app_default(&app);            /* 示波器 + 信号发生器 + 当前界面 */
    spec_fs_tab_init();              /* 设置窗第 2 项"采样率": 登记 clk_wiz1.h 的小数档 */
    app.osc.sample_rate = (s32)(adc_fs_mhz * 1000000.0 + 0.5);   /* 由 adc_fs_mhz 推 */
    app.osc.adc_bits    = 12;        
    app.osc.adc_mv_fs   = 10330;     /* ★ 模块量程(mV 峰峰): 用【直流】标定所得 10000×1001/969
                                        (直流读数才可信; 信号源标称 1Vpp 实测约 1.12Vpp) */
    app.osc.adc_zero    = 0;         /* 归零: 输入短路时读"有效值", 码值 = mV×4096/adc_mv_fs 填这里 */
    /* ★ 数据是"零中心有符号流": 通道中心码值显式跟 adc_zero 对齐。
       (ui_osc_default() 里默认 2048, 现在只靠下面 ui_osc_autoset() 兜底;
        这里显式设一次: 以后即使挪掉/去掉 autoset 调用, 波形也不会整体偏 2048 码) */
    app.osc.ch[0].center = app.osc.adc_zero;
    app.osc.ch[1].center = app.osc.adc_zero;
    app.osc.ch[0].data  = adc_buf0;  /* 静态缓冲, 永远有效 */
    app.osc.ch[1].data  = adc_buf1;
    app.osc.ch[0].len   = SAMPLE_LEN;
    app.osc.ch[1].len   = SAMPLE_LEN;

    ui_osc_autoset(&app.osc);
    ui_app_draw(LIVE_FB, &app);      /* 整屏画一次 */
    ui_touch_draw_bar(LIVE_FB, &app, UI_BTN_NONE);
    lcd_flush(LIVE_FB);

    run_vdma_frame_buffer(&vdma, XPAR_XAXIVDMA_0_BASEADDR, LCD_WIDTH,
                                   LCD_HEIGHT, LIVE_FB, 1, 1);
}

/* ---------------- 信号源(DAC 数据选择器) + ADC 使能: AXI GPIO 实现 ----------------
   放文件最后; 调用点: 采样率切换处 pl_adc_off(), 每帧采集 pl_adc_pulse(), 开机 pl_awg_src_set() */
static u8 g_awg_src  = 0u;         /* ★ DAC 源选择码 AWG_SRC_xxx(= wave_sel 的 sel) */
static u8 g_gpio_val = 0xFFu;      /* 影子: 只在值变化时才真的写总线 */

static u8 pl_gpio_value(void)      /* 常态: ADC 位 = 0, 源位来自 g_awg_src */
{
    return (u8)((g_awg_src & 3u) << 2);      /* 00/01/10/11 -> GPIO[3:2] = 0x00/0x04/0x08/0x0C */
}

static void pl_gpio_apply(void)    /* 只在值变化时写(常态值 = 源位, ADC 位为 0) */
{
    u8 v = pl_gpio_value();

    if (v != g_gpio_val) {
        XGpio_DiscreteWrite(&AXI_Gpio, 1, v);
        g_gpio_val = v;
    }
}

static void pl_adc_off(void)       /* ADC 使能位清零, 源位保留; 强制写一次 */
{
    u8 v = pl_gpio_value();

    XGpio_DiscreteWrite(&AXI_Gpio, 1, v);
    g_gpio_val = v;
}

static void pl_adc_pulse(void)     /* 采集触发: 拉高 1us 再回落(两个沿都必须真写) */
{
    u8 v = (u8)(pl_gpio_value() | AWG_ADC_BITS);

    XGpio_DiscreteWrite(&AXI_Gpio, 1, v);   /* 上升沿 */
    g_gpio_val = v;                         /* 影子跟上, 保证下降沿一定写得下去 */
    usleep(1);
    pl_adc_off();                           /* 下降沿: ADC 位=0, 源位不变 */
}

/* ---------------- ★ Chirp-Z 的 8192 点模式 ---------------- */
/* g_czt_on 的声明已提到文件前部(main 之前), 这里不再重复定义 */

static u32 czt_nfft(void)       /* 当前 FFT 点数 */
{
    return (u32)N;                       /* ★ 统一 16384: 原来 CZT 打开时降成 8192 -> 与 CZT 的 L 来回切档 -> DMA 超时 */
}

static u32 czt_adc_len(void)    /* 当前 ADC 单次触发点数(必须与 FFT 点数一致) */
{
    return (u32)SAMPLE_LEN;              /* ★ 统一 16384(同上) */
}

/* ★ 唯一的切换点: 三个寄存器一起改(ADC0 / ADC1 / FFT_INIT_D)。
   注意 adc_fs_apply() 里还有同名两行(改采样率时), 那里也必须用 czt_adc_len(),
   否则采样率一动就把点数写回 16384, 与 FFT 的 8192 不一致。 */
static void czt_pl_set(int on)
{
    g_czt_on = (u8)((on != 0) ? 1u : 0u);

    MY_ADC_mWriteReg(XPAR_MY_ADC_0_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
    MY_ADC_mWriteReg(XPAR_MY_ADC_1_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
    FFT_INIT_D_mWriteReg(XPAR_FFT_INIT_D_0_BASEADDR, FFT_INIT_D_S00_AXI_SLV_REG0_OFFSET,
                         (u32)fft_log2(czt_nfft()));
}

static void pl_awg_src_set(u8 src)          /* 0 = 普通 DDS, 1 = BRAM DDS */
{
    g_awg_src = (u8)(src & 3u);                               /* ★ 参数就是 sel 码 */
    pl_gpio_apply();
}


static void adc_fs_apply(double mhz, int is_spec)   /* 改时钟; is_spec=1: 允许频谱仪的小数频率 */
{
    if (is_spec == 0) {
        mhz = ui_osc_fs_snap(mhz);
    }
    adc_fs_is_spec = (u8)(is_spec != 0);

    if (fabs(mhz - adc_fs_mhz) < 0.02 * adc_fs_mhz) {      /* 判据放宽到 2%:
        分频是"整数+小数"算出来的, 实得值和请求值本来就有千分之几的差,
        卡太严会每按一次"自动"都重配一次 MMCM */
        /* ★ 但 ADC 点数寄存器必须同步! 早退会把下面那两行 czt_adc_len() 的写入一起跳过:
           离开频谱仪 / 开关 CZT 时点数会变(8192 <-> 16384), 若这里直接 return,
           点数寄存器就停在旧值 -> DMA 长度与 PL FFT 档不一致(正是 1497 行注释里记录过的坑)。
           时钟不重配, 所以这里不需要 usleep(2000)。 */
        adc_fs_is_spec = (u8)(is_spec != 0);
        MY_ADC_mWriteReg(XPAR_MY_ADC_0_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
        MY_ADC_mWriteReg(XPAR_MY_ADC_1_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
        return;
    }
    clk_wiz_cfg(XPAR_CLK_WIZ_0_BASEADDR, mhz);   /* 新版返回 void, 内部轮询等 MMCM 锁定 */


    /* ★ 必须用 czt_adc_len(): CZT 开着时这里是 8192。以前写死 SAMPLE_LEN(16384),
       而"进频谱仪"就会调本函数 -> 一进频谱仪就把点数写回 16384, 与 FFT 的 8192 不一致。 */
    MY_ADC_mWriteReg(XPAR_MY_ADC_0_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
    MY_ADC_mWriteReg(XPAR_MY_ADC_1_BASEADDR, MY_ADC_S01_AXI_SLV_REG0_OFFSET, czt_adc_len());
    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR, MY_FIR_INIT_S00_AXI_SLV_REG10_OFFSET, 0);
    pl_adc_off();                 /* ADC 使能位清零(等效原来的 0x00), 源位保留 */
    usleep(2000);

    adc_fs_mhz = mhz;                            /* ★ 必须是真正写进 clk_wiz 的那个值(表内值) */
    app.osc.sample_rate = (s32)(adc_fs_mhz * 1000000.0 + 0.5);   /* 时基/测量/显示都跟着走 */
    app.osc.meas[0].freq_mhz = 0;
    app.osc.meas[1].freq_mhz = 0;
    if (app.screen == UI_SCREEN_SCOPE) {                  /* 信息行的 fs 显示跟着变 */
        ui_osc_draw_info(LIVE_FB, &app.osc);
        ui_osc_flush_chrome(LIVE_FB);
    }
    adc_fs_quiet = 3;                                     /* MMCM 重锁那几帧丢掉 */
}

//---------------- 频谱仪采样率档位----------------
static void spec_fs_tab_init(void)
{
    double v[UI_SPEC_FS_N];
    u32 i;
    int n = 0;

    for (i = 0u; (i < (u32)CLK_WIZ_TABLE_LEN) && (n < (int)UI_SPEC_FS_N); i++) {
        u32 f = clk_wiz_table[i].freq_hz;

        if (1) {   /* XXFSALL: 原来只放 [6.144,49.152] 且排除整数 MHz; 按需求放开为频率表里的全部档位(含 5/6/8/16/32/48/64 MHz), 频率表本身未改 */
            v[n++] = (double)f / 1000000.0;              /* 表本身升序 */
        }
    }
    ui_spec_fs_tab_set(&app.spec, v, n);
    app.spec.fs_idx = (u8)ui_spec_fs_idx_of_hz(&app.spec,
                                               (u32)(ADC_FS_SPEC_MHZ * 1000000.0 + 0.5));
    xil_printf("[fs] 频谱仪可选采样率 %d 档:", n);
    for (i = 0u; i < (u32)n; i++) {
        xil_printf(" %d", adc_fs_khz(v[i]));
    }
    xil_printf(" kHz; 当前 %d kHz\r\n", adc_fs_khz(ui_spec_fs_mhz(&app.spec)));
}



/* FFT 点数 -> log2(点数): FFT_INIT_D 的寄存器要的是 log2(N)。
   在代码里算, 而不是写死 13/14 —— 以后改 FFT_N 时不会忘记改这里。 */
static u32 fft_log2(u32 n)
{
    u32 k = 0u;

    while (((u32)1u << k) < n) {
        k++;
    }
    return k;
}

/* ================= 信号发生器 -> PL 桥接(①b): 实现 =================
   (放文件最后; 调用者: 开机验证块, 以后是信号发生器界面的 awg_apply_hw()) */



/* 0~AWG_FULL_MVPP -> 普通 DDS 幅度寄存器(0~128); 128 = 数字满幅 */
static u8 awg_dds_amp(s32 mv)
{
    s32 a;

    if (mv < 0) {
        mv = 0;
    }
    if (mv > AWG_FULL_MVPP) {
        mv = AWG_FULL_MVPP;
    }
    a = (s32)(((double)mv * (double)AWG_DDS_AMP_FS / (double)AWG_FULL_MVPP) + 0.5);
    if (a > AWG_DDS_AMP_FS) {
        a = AWG_DDS_AMP_FS;
    }
    return (u8)a;
}

/* 0~AWG_FULL_MVPP -> BRAM DDS 幅度寄存器(0~1024); 1024 = 数字满幅(HDL 里 >>10) */
/* ★ BRAM 幅度补偿(千分比, 1000 = 不补偿): 有些波形的表峰峰天生不到满幅 (最典型是 Sinc: +1.0/-0.217 -> 只有满幅的 60.9%), 同一幅度设置下输出就偏小。    下发前按表的实际峰峰把幅度寄存器补上去(上限仍是 1024)。 */
static s32 g_awg_amp_gain = 1000;
static u16 awg_bram_amp(s32 mv)
{
    s32 a;

    if (mv < 0) {
        mv = 0;
    }
    if (mv > AWG_FULL_MVPP) {
        mv = AWG_FULL_MVPP;
    }
    a = (s32)(((double)mv * (double)AWG_BRAM_AMP_FS / (double)AWG_FULL_MVPP) + 0.5);
    a = (s32)(((s64)a * (s64)g_awg_amp_gain + 500) / 1000);      /* ★ 峰峰补偿 */
    if (a > AWG_BRAM_AMP_FS) {
        a = AWG_BRAM_AMP_FS;
    }
    return (u16)a;
}

/* 0.1° -> 12 位相位字(4096 = 360.0°) */
static u16 awg_ph12(s32 mdeg)
{
    s32 p = (s32)(((s64)mdeg * 4096) / 3600);   /* ★ 0.1度单位, 原来 /360000 -> 相位只有 1/100 */

    p %= 4096;
    if (p < 0) {
        p += 4096;
    }
    return (u16)p;
}

/* 普通 DDS 单路: ch=0 -> reg0..4(DAC1), ch=1 -> reg5..9(DAC2)
   shape: 0=正弦 1=方波 2=三角 3=锯齿; on=0 时写"幅度 0 + 使能 0" = 干净 0V */
static void awg_apply_dds_ch(int ch, u8 on, u8 shape, s32 freq_hz, s32 vpp_mv, s32 phase_mdeg)
{
    /* 通道二的寄存器偏移已由 pl_dds2_config() 内部处理, 这里不再需要 off */
    u8  oh;

    switch (shape) {
    case 1u:  oh = 0x2u; break;              /* 方波 */
    case 2u:  oh = 0x4u; break;              /* 三角 */
    case 3u:  oh = 0x8u; break;              /* 锯齿 */
    default:  oh = 0x1u; break;              /* 正弦 */
    }
    /* 顺序: 先波型/频率/幅度/相位, 最后开使能(避免开使能瞬间冲出满幅) */
    /* ★ 两个通道都直接调你写好的函数: 频率的 120MHz 标定在函数内部;
       on = 0 时幅度写 0(输出干净 0V, 等效于原来的"关"); type 用上面的位掩码 oh。 */
    if (ch == 0) {
        pl_dds1_config(freq_hz, (int)awg_ph12(phase_mdeg),
                       (int)(on ? (u32)awg_dds_amp(vpp_mv) : 0u), (int)oh);
    } else {
        pl_dds2_config(freq_hz, (int)awg_ph12(phase_mdeg),
                       (int)(on ? (u32)awg_dds_amp(vpp_mv) : 0u), (int)oh);
    }
}

/* BRAM DDS 配置(只影响 DAC1): on=0 时 reg0=0 -> 输出直流中点 = 0V(干净关断) */
static void awg_bram_cfg(s32 freq_hz, s32 phase_mdeg, s32 vpp_mv, u8 on)
{
    /* ★ 直接调你写好的函数: 内部依次写 起始地址/读长度/频率/幅度/相位 并启动读(120MHz 标定在内部)。
       on = 0 时幅度写 0 -> 输出干净 0V(等效于原来的"关")。 */
    u32 amp = on ? (u32)awg_bram_amp(vpp_mv) : 0u;

    pl_bram_config(freq_hz, (int)awg_ph12(phase_mdeg), (int)amp);
}

/* 生成 BRAM 波形表并写入 BRAM(调用 dds_wave.c 的生成函数, 它们内部会调 str_wr_bram)
   p[] 的含义见顶部 AWG_W_* 注释。幅度/相位/频率由 awg_bram_cfg() 单独配置。 */
/* ★ 2ASK 自己生成(不用 dds_wave.c 的 set_2ASK_wave): 原作者那个函数有两处硬伤 ——
   1) bit_type==0(固定码)分支是 b=(k<one_cnt)?1:0, 而 one_cnt 只在 b==1 时自增,
      所以恒为 0 -> 固定码输出一条直线(没有载波); 源码里作者自己标了"需自己改这两行";
   2) m 序列的 LFSR 在"每个采样点"都移位(应该每个码元才移一次),
      码元长度退化成 1 个采样点 -> 输出被切碎成噪声。
   这里按"每码元 cyc 个载波周期、共 bits 个码元、载波相位跨码元连续"重写:
     固定码: 前一半码元为 1、后一半为 0;  m 序列: x^15+x^14+1, 每个码元移一次。
   表由 str_wr_bram() 写进 BRAM(它同时留一份 g_awg_tab[] 给预览)。 */
/* ★ 谐波合成自己做(不用 dds_wave.c 的 set_harmonic_wave): 那个函数会按"各次之和"
   自动归一化, 于是填进去的数值只成了相对比例, 不是真实幅度 —— 界面上填 3Vpp 实际并不是 3Vpp。
   这里按【绝对幅度】合成:
     基波(1 次) 幅度 = 参数区"幅度"行; 各次(2..n) 幅度 = 各自的电压幅值; 单位都是 mV,
     满幅 = AWG_FULL_MVPP <=> 数字 8191。相位 0.1 度。
   若总峰值超出满幅, 整体等比缩小(保持各次比例), 不会削顶。 */
static void awg_gen_harm_abs(int fund_mv, const s32 *h_mv, const s32 *h_pha_mdeg, int n)
{
    int tab[ch_data_len];
    double sc = 8191.0 / (double)AWG_FULL_MVPP;
    double pk = 0.0;
    int i, h;

    if (n < 2) { n = 2; }
    if (n > UI_AWG_HARM_MAX) { n = UI_AWG_HARM_MAX; }

    for (i = 0; i < ch_data_len; i++) {
        double x = (double)i / (double)ch_data_len;      /* 0..1 个基波周期 */
        double v = (double)fund_mv * sin(2.0 * PI * x);  /* 基波, 相位 0 */

        for (h = 2; h <= n; h++) {
            double ph = 2.0 * PI * ((double)h_pha_mdeg[h - 1] / 3600.0);

            v += (double)h_mv[h - 1] * sin(2.0 * PI * (double)h * x + ph);
        }
        v *= sc;
        if (v > pk) { pk = v; }
        if (-v > pk) { pk = -v; }
        tab[i] = (int)(8192.0 + v);
    }
    if (pk > 8191.0) {                                   /* 等比缩小, 不削顶 */
        for (i = 0; i < ch_data_len; i++) {
            double v = ((double)tab[i] - 8192.0) * (8191.0 / pk);

            tab[i] = (int)(8192.0 + v);
        }
    }
    for (i = 0; i < ch_data_len; i++) {
        if (tab[i] > 16383) { tab[i] = 16383; }
        if (tab[i] < 0) { tab[i] = 0; }
    }
    str_wr_bram(tab);
}
/* ★ 2ASK: 只负责"造码元图样", 调制与写 BRAM 交给原作者的新接口
   set_2ASK_wave(Ac, fmfc, bits, code[]) —— fmfc 是【整表】的载波周期数,
   所以传 每码元周期数 x 码元数 就能让载波相位跨码元保持连续。
   码型: 0 = 固定码(前一半码元为 1、后一半为 0), 1 = m 序列(x^15+x^14+1, 每个码元移一次)。
   注意: 短窗口(UI 里 8~64 个码元)的 m 序列本身就偏不均衡, 看着会"几段突发 + 较长静默",
   这是 m 序列的特性, 不是波形出错; 想要短突发也均衡的话得换固定图样。 */
static void awg_gen_2ask(int cyc, int bits, int type, const u8 *pat)
{
    int code[64];                            /* 与界面上限(64 码元)一致 */
    /* ★ 初值是搜出来的: 原来的 0x0001/0x7FFF 在"前 8/16 个码元"这种短窗口里几乎全是 0
       (LFSR 的反馈位要 14 拍才翻), 8 码元时甚至 0% 载波。0x0253 的前 8/16/32/64 个输出
       分别是 3/8/16/33 个 1, 短突发也很均衡。m 序列与初值无关, 只是相位不同。 */
    u32 lfsr = 0x0253u;
    int i, half;

    if (bits < 1) { bits = 1; }
    if (bits > 64) { bits = 64; }
    if (cyc < 1) { cyc = 1; }
    half = bits / 2;
    if (half < 1) { half = 1; }

    for (i = 0; i < bits; i++) {
        if (type == 0) {
            code[i] = (i < half) ? 1 : 0;
        } else if (type == 1) {
            u32 fb = ((lfsr >> 14) ^ (lfsr >> 13)) & 1u;

            lfsr = ((lfsr << 1) | fb) & 0x7FFFu;
            code[i] = (int)(lfsr & 1u);
        } else {
            /* ★ 自定义码序列(界面逐个码元设置); pat==0 时退回交替图样 */
            code[i] = (pat != 0) ? ((pat[i] != 0u) ? 1 : 0) : ((i & 1) ? 1 : 0);
        }
    }
    set_2ASK_wave(1.0, cyc * bits, bits, code);
}
static void awg_gen_wave(u8 wave, const s32 *p)
{
    switch (wave) {
    case AWG_W_PULSE:
        set_pulse((float)p[0] / 1000.0f);                       /* 占空比 0~1 */
        break;
    case AWG_W_SINC:
        set_Sinc_wave((double)p[0]);                            /* wide */
        break;
    case AWG_W_AM:
        /* Am/Ac 只有比值有意义(函数内部按 Ac+Am 归一化): 取 Ac=1, Am=调制比 */
        set_AM_wave((double)p[0] / 1000.0, 1.0, (int)p[1]);
        break;
    case AWG_W_DSB:
        set_AMDSB_wave(1.0, 1.0, (int)p[0]);                    /* 内部按 Ac*Am 归一化 */
        break;
    case AWG_W_FM:
        /* 相位偏移 ∝ Kf×Am(输出幅度按 Ac 归一化, 与 Am 无关):
           用 Am=0.1, Kf=Kf×10 表达 0.1 分辨率的 Kf */
        set_FM_wave(0.1, 1.0, (int)p[0], (int)p[1]);
        break;
    case AWG_W_PM:
        /* 峰值相偏(周) = Kp×Am -> 取 Am=1, Kp = p[1]/10 周 */
        set_PM_wave(1.0, 1.0, (int)p[0], (double)p[1] / 10.0);
        break;
    case AWG_W_ASK:
        /* 载波周期数/周期 = 每码元周期数 × 码元数(保证能被码元数整除) */
        awg_gen_2ask((int)p[0], (int)p[1], (int)p[2], 0);   /* 演示路径: 预设 */
        break;
    default:
        break;
    }
}



/* ---------------- 信号发生器: 界面状态 ----------------
   普通 DDS(my_dds_0)双路: CH1 -> reg0..4(DAC1), CH2 -> reg5..9(DAC2);
   BRAM(my_dds_bram_0)单路只接 DAC1: 表 + reg3(频率) / reg4(幅度) / reg5(相位) / reg0(运行);
   数据选择器(GPIO 高两位): 当前波形是 BRAM 那 8 种时切 11, 否则 00。
   ★ 只在 need_apply 置位时调用(参数变化), 不每帧写。 */
/* ================= ★ 数字滤波器: 设计 + 量化 + 下发(只调用你写的函数) =================
   fs 固定 = 8192000 Hz(BD 实测 8.19199MHz, ADC 内部 PLL 直出采样时钟, 不可切档)。
   FIR 硬件固定 10 抽头: 低通用 order=9(10 个系数); 高通/带通要求偶数 order -> 用 8(9 个系数, 补 1 个 0)。
   IIR 硬件 8+8 个系数: a[0] 恒为 1, 硬件用 Ycoe0 做除法自归一化(所以 q 不必下发)。
   全程只调 filter.c 的 FIR_COEFFICIENT_RELOAD / IIR_COEFFICIENT_SET, 本函数不碰任何寄存器。 */
static void filt_apply_from_ui(ui_filter_t *f)
{
    /* ★ 滤波器设计用的采样率必须是【硬件实际值】, 不能写死 8192000:
       归一化截止 2*fc/fs 会随 fs 变, 写死的话一旦在滤波器页切了采样率,
       设计出来的系数就与真实采样率不符(频响整体偏 fs 比例), 而且 fc 靠近新
       Nyquist 时设计函数会直接返回 0(界面显示"参数非法:设计失败")。
       adc_fs_mhz 由 adc_fs_apply() 维护, 与硬件一致; 异常时退回 8.192MHz。 */
    const double fs = (adc_fs_mhz > 0.0) ? (adc_fs_mhz * 1000000.0) : 8192000.0;

    f->status = UI_FLT_ST_FAIL;
    {   /* ★ 先清掉上一次的系数显示, 失败时界面不会显示旧数据 */
        int ic;

        for (ic = 0; ic < 16; ic++) { f->coef[ic] = 0; f->coef_y[ic] = 0; }
    }
    if (f->kind == UI_FLT_KIND_FIR) {
        double h[16];
        int    hq[10];
        int    ok = 0, i, n = 10;      /* ★ 必须初始化: 下面 if (ok != 1) 会读它 */

        for (i = 0; i < 16; i++) { h[i] = 0.0; }
        {   /* ★ 按界面上显示的 f->order 下发(不再写死 9/8), 并保证 FIR 的硬件约束:
               高通/带通要求 order 为偶数; order 上限 9(10 抽头, h[16] 足够) */
            int fo = (int)f->order;

            if (fo < 1) { fo = 1; }
            if (fo > 9) { fo = 9; }
            if (((f->type == UI_FLT_TYPE_HP) || (f->type == UI_FLT_TYPE_BP)) && ((fo & 1) != 0)) {
                fo--;                       /* 奇数 -> 退一阶变偶数(仅高通/带通要求偶数阶; 带阻不要求, 界面给 9 就按 9 下发) */
            }
            switch (f->type) {
            case UI_FLT_TYPE_LP: ok = fir_lowpass_design(fs, (double)f->fc_hz, fo, (int)f->win, h); break;
            case UI_FLT_TYPE_HP: ok = fir_highpass_design(fs, (double)f->fc_hz, fo, (int)f->win, h); break;
            case UI_FLT_TYPE_BP: ok = fir_bandpass_design(fs, (double)f->f1_hz, (double)f->f2_hz, fo, (int)f->win, h); break;
            default:             ok = fir_bandstop_design(fs, (double)f->f1_hz, (double)f->f2_hz, fo, (int)f->win, h); break;
            }
        }
        if (ok != 1) { f->coef_n = 0; f->coef_max = 0; return; }
        fir_quantize_12bit(h, hq, n);            /* 不足 10 个时 h 已补 0 -> hq[9] = 0 */
        /* ★ 在你写的 fir_quantize_12bit() 之外做饱和夹取(不改你的库函数):
           它是 na[i] = round(a[i]*2047), 没有上下限。而高通是"delta - 低通"后再按奈奎斯特
           归一, 主抽头理论上可能略超 1.0 -> 会写出 |值|>2047, 硬件按 12 位截断后会变成反号,
           滤波器就完全错了(而且状态还显示"已下发")。这里夹到 12 位有符号范围并上报饱和。 */
        {
            int sat = 0;

            for (i = 0; i < n; i++) {
                if (hq[i] > 2047)       { hq[i] = 2047;  sat = 1; }
                else if (hq[i] < -2048) { hq[i] = -2048; sat = 1; }
            }
            /* ★ 这里必须把 OK 也一起定下来: 函数末尾原来有一句无条件的
               f->status = UI_FLT_ST_OK; 会把刚置上的 SAT 覆盖掉 -> 饱和上报永远看不到。 */
            if (sat != 0) {
                f->status = UI_FLT_ST_SAT;      /* 有截断: 建议改窗或放宽截止 */
            } else {
                f->status = UI_FLT_ST_OK;
            }
        }
        for (i = 0; i < n; i++) { f->coef[i] = (s16)hq[i]; }   /* ★ 存下真正下发的这组系数(供界面显示) */
        FIR_COEFFICIENT_RELOAD(hq);              /* ★ 你写的下发函数 */
        f->coef_n = n;
        f->q_shift = 0;
        f->coef_max = 0;
        for (i = 0; i < n; i++) {
            int v = (hq[i] < 0) ? -hq[i] : hq[i];

            if (v > (int)f->coef_max) { f->coef_max = v; }
        }
        /* ★ 这里原来是无条件的 f->status = UI_FLT_ST_OK; —— 它会把上面 sat 分支
           刚置上的 UI_FLT_ST_SAT 覆盖掉, 导致 FIR 的饱和上报永远看不到。
           OK 已经移到上面 sat 的 else 分支里, 所以这里删掉。 */
    } else {
        double b[24], a[24];
        int    bq[16], aq[16], q = 0;
        int    order = (int)f->order;
        int    ok, r, i, n;

        /* ★ 先按硬件能力夹住, 越界直接报失败(不要事后截断系数 -> 会砍掉最高阶项, 极点改变甚至不稳定) */
        if (order < 1) { order = 1; }
        if ((f->type == UI_FLT_TYPE_BP) || (f->type == UI_FLT_TYPE_BS)) {
            if (order > 3) { f->coef_n = 0; f->coef_max = 0; return; }   /* 2*order+1 必须 <= 7 */
        } else {
            if (order > UI_FLT_IIR_MAX_ORDER) { f->coef_n = 0; f->coef_max = 0; return; }
        }
        for (i = 0; i < 24; i++) { b[i] = 0.0; a[i] = 0.0; }
        switch (f->type) {
        case UI_FLT_TYPE_LP: ok = iir_lowpass(fs, (double)f->fc_hz, order, b, a); break;
        case UI_FLT_TYPE_HP: ok = iir_highpass(fs, (double)f->fc_hz, order, b, a); break;
        case UI_FLT_TYPE_BP: ok = iir_bandpass(fs, (double)f->f1_hz, (double)f->f2_hz, order, b, a); break;
        default:             ok = iir_bandstop(fs, (double)f->f1_hz, (double)f->f2_hz, order, b, a); break;
        }
        if (ok != 1) { f->coef_n = 0; f->coef_max = 0; return; }
        n = order + 1;                            /* 低通/高通: order+1 个系数 */
        if ((f->type == UI_FLT_TYPE_BP) || (f->type == UI_FLT_TYPE_BS)) {
            n = (order * 2) + 1;                  /* 带通/带阻: 2*order 阶 */
        }
        /* ★ 这里不再做 n>8 的截断: 上面已保证 order 不会超出硬件能力(去掉截断避免砍最高阶项) */
        /* ★ 必须清零: IIR_COEFFICIENT_SET() 无条件写 8+8 个系数寄存器,
           而量化只写前 n 个(n = order+1 <= 7)。不清零 => 未用抽头(尤其 Ycoe7, 它是活的反馈抽头)
           会是栈垃圾 => 实际滤波器与设计不符, 甚至不稳定。 */
        for (i = 0; i < 16; i++) { bq[i] = 0; aq[i] = 0; }
        r = iir_quantize_12bit(b, a, n, bq, aq, &q);
        if (r > 0) {
            IIR_COEFFICIENT_SET(bq, aq);          /* ★ 顺序: 分子 bq, 分母 aq(HDL 已确认) */
            f->status = UI_FLT_ST_OK;            /* IIR 下发成功(饱和时走 else 不下发) */
        } else {
            f->status = UI_FLT_ST_SAT;            /* r<0: 有饱和截断 -> 建议降阶或提高截止 */
        }
        /* ★ 只有真正下发成功(r > 0)才更新"系数个数/q"的显示。
           饱和(r < 0)时上面走 else 并没有调 IIR_COEFFICIENT_SET(), 硬件里还是上一次的系数,
           若这里仍写 coef_n=n/q_shift=q, 界面就会显示一组【从未下发过】的数字(显示与硬件不符)。 */
        if (r > 0) {
            f->coef_n = n;
            f->q_shift = q;
            for (i = 0; i < n; i++) { f->coef[i]   = (s16)bq[i]; }   /* ★ 分子 b(供界面显示) */
            for (i = 0; i < n; i++) { f->coef_y[i] = (s16)aq[i]; }   /* ★ 分母 a(供界面显示, IIR 才算完整一组) */
        } else {
            f->coef_n = 0;
            f->q_shift = 0;
        }
        f->coef_max = 0;
        for (i = 0; i < n; i++) {
            int v1 = (bq[i] < 0) ? -bq[i] : bq[i];
            int v2 = (aq[i] < 0) ? -aq[i] : aq[i];

            if (v1 > (int)f->coef_max) { f->coef_max = v1; }
            if (v2 > (int)f->coef_max) { f->coef_max = v2; }
        }
    }
}
static void awg_apply_from_ui(ui_awg_t *a)
{
    const ui_awg_ch_t *c1 = &a->ch[0u];          /* DAC1: 普通 DDS CH1 或 BRAM */
    const ui_awg_ch_t *c2 = &a->ch[1u];          /* DAC2: 始终普通 DDS CH2 */
    u8 src_bram = ui_awg_is_bram(c1->wave);

    /* 普通 DDS 两路(CH1 即使被 BRAM 接管也照常配置: 切回普通 DDS 时立刻可用) */
    awg_apply_dds_ch(0, c1->on, c1->wave, c1->freq_hz, c1->vpp_mv, c1->phase_mdeg);
    awg_apply_dds_ch(1, c2->on, c2->wave, c2->freq_hz, c2->vpp_mv, c2->phase_mdeg);

    /* XXDDSYNC: 同步重启两路 DDS 相位累加器 —— 修"两路都设 0 相位却差 90 度"。
       机理: PL 的 dds.v 里 P_WORD 只是加在【自由运行的 32 位累加器】上的偏移;
             累加器【只在 wave_en==0 / 复位时清 0】(dds.v:34-38)。
             而 dds_wave.c 的 pl_dds*_config 把使能【写死为 1】, 全工程从不写 0,
             上电也只初始化 CH1(helloworld.c:368) -> CH2 晚几十~几百 ms 才起跑
             -> 两路相位差 = 360 度 x f x dt (mod 360) = 一个【任意常量】(实测 90 度)。
       做法: 把两个使能先清 0(两路 fre_add 同时清 0), 再一起置 1(各自从 P_WORD 起跑)。
       精度: 两次写间隔约 1~2 个 AXI 周期(10~20ns) -> 残差约 360*f/100MHz:
             1kHz 约 0.004 度(可忽略), 1MHz 约 3.6 度, 20MHz 可达 72 度(那时要 PL 加公共复位)。
       代价: 使能拉低期间 DAC 被钳到中点, 持续几百 ns(肉眼看不出)。
       若只想在"相位/波形/开关"变化时才对齐, 可把这段包进条件里。 */
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR, MY_DDS_S0_AXI_SLV_REG2_OFFSET, 0u);   /* CH1 使能=0 -> fre_add 清 0 */
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR, MY_DDS_S0_AXI_SLV_REG7_OFFSET, 0u);   /* CH2 使能=0 -> fre_add 清 0 */
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR, MY_DDS_S0_AXI_SLV_REG2_OFFSET, 1u);   /* CH1 使能=1, 从 P_WORD 起跑 */
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR, MY_DDS_S0_AXI_SLV_REG7_OFFSET, 1u);   /* CH2 使能=1, 从 P_WORD 起跑 */

    g_awg_amp_gain = 1000;                  /* 默认不补偿(普通波形本身就是满幅) */
    if ((src_bram != 0u) && (g_awg_tab_ok != 0u) && (c1->wave != AWG_W_HARM)) {
        /* ★ 每次下发都按"当前表"重算一次补偿。
           不能只在表刚生成(tab_dirty)时算: 只改幅度时表不会重生成, 上面那行复位
           会把增益清成 1000 -> Sinc 的幅值就又变回偏小了(而且表没变, 本该继续补偿)。 */
        s32 lo2 = 16383, hi2 = 0, i2;

        for (i2 = 0; i2 < (s32)ch_data_len; i2++) {
            if ((s32)g_awg_tab[i2] < lo2) { lo2 = (s32)g_awg_tab[i2]; }
            if ((s32)g_awg_tab[i2] > hi2) { hi2 = (s32)g_awg_tab[i2]; }
        }
        if ((hi2 - lo2) >= 512) {
            g_awg_amp_gain = (s32)((16383 * 1000) / (hi2 - lo2));
            if (g_awg_amp_gain > 3000) { g_awg_amp_gain = 3000; }
        }
    }
    if (src_bram != 0u) {
        if (c1->tab_dirty != 0u) {               /* 波形/形状参数变了才重算表并写 BRAM */
            s32 p[4] = { 0, 0, 0, 0 };

            switch (c1->wave) {
            case AWG_W_PULSE: p[0] = c1->duty_x10; break;
            case AWG_W_SINC:  p[0] = c1->wide;     break;
            case AWG_W_AM:    p[0] = c1->am_x10; p[1] = c1->fmfc; break;
            case AWG_W_DSB:   p[0] = c1->fmfc;     break;
            case AWG_W_FM:    p[0] = c1->fmfc; p[1] = c1->kf_x10; break;
            case AWG_W_PM:    p[0] = c1->fmfc; p[1] = c1->kp_x10; break;
            case AWG_W_ASK:   p[0] = c1->ask_cyc; p[1] = c1->ask_bits; p[2] = c1->ask_type; break;
            default: break;
            }
            if (c1->wave == AWG_W_HARM) {
                s32 ha[UI_AWG_HARM_MAX];
                s32 hp[UI_AWG_HARM_MAX];
                int i;

                for (i = 0; i < UI_AWG_HARM_MAX; i++) {
                    ha[i] = (s32)c1->harm_a[i];
                    hp[i] = c1->harm_p[i];
                }
                awg_gen_harm_abs(c1->vpp_mv, ha, hp, (int)c1->harm_n);   /* ★ 基波=幅度行 */
            } else if (c1->wave == AWG_W_ASK) {
                        /* ★ 2ASK 不要先调 awg_gen_wave(): 它内部会以 pat=0 生成一遍表
                           并写进 BRAM(4096 次 AXI 写 + 4096 次 cos), 紧接着又被下一行真正的
                           码序列覆盖 -> DAC 上先出现约 10ms 的错误码型, 而且白花一倍时间。 */
                awg_gen_2ask((int)p[0], (int)p[1], (int)p[2], c1->ask_code);   /* ★ 自定义码序列 */
            } else {
                awg_gen_wave(c1->wave, p);
            }
            a->ch[0u].tab_dirty = 0u;
            a->redraw_preview = 1u;   /* ★ 表刚变 -> 让预览重画(界面在帧边界消费) */
        }
        /* ★★ 关键: 表刚在上面按当前波形生成/更新完, 这里必须再按【新表】重算一次峰峰补偿。
           上面那次计算发生在表生成之前, 用的是旧表(首次选 Sinc、或改了"宽度"之后尤其明显)
           -> 补偿值不对 -> 实际输出幅度与设置不符(实测现象)。 */
        if ((g_awg_tab_ok != 0u) && (c1->wave != AWG_W_HARM)) {
            s32 lo3 = 16383, hi3 = 0, i3;

            for (i3 = 0; i3 < (s32)ch_data_len; i3++) {
                if ((s32)g_awg_tab[i3] < lo3) { lo3 = (s32)g_awg_tab[i3]; }
                if ((s32)g_awg_tab[i3] > hi3) { hi3 = (s32)g_awg_tab[i3]; }
            }
            g_awg_amp_gain = 1000;
            if ((hi3 - lo3) >= 512) {
                g_awg_amp_gain = (s32)((16383 * 1000) / (hi3 - lo3));
                if (g_awg_amp_gain > 3000) { g_awg_amp_gain = 3000; }
            }
        }
        if (c1->wave == AWG_W_HARM) {
            /* ★ 谐波的幅度已经做进表里(含基波), 寄存器给满幅, 且不做峰峰补偿 */
            awg_bram_cfg(c1->freq_hz, c1->phase_mdeg, AWG_FULL_MVPP, c1->on);
        } else {
            awg_bram_cfg(c1->freq_hz, c1->phase_mdeg, c1->vpp_mv, c1->on);
        }
    }

        /* ★ 守卫: 数字滤波器输出开着时, 信号发生器不许抢 DAC(离开滤波器页也一样);
       否则按信号发生器自己的选择: BRAM 波形 -> 11, 普通 DDS -> 00。 */
    pl_awg_src_set((app.flt.out_on != 0u) ? ui_filter_sel(&app.flt)
                                          : ((src_bram != 0u) ? AWG_SRC_BRAM : AWG_SRC_DDS));
    a->need_apply = 0u;
}


/* 用 PL FFT 对 src(16384 点 s16) 做一次不加窗 FFT, 取第 k 个 bin 的复数; 0 = 成功 */
static int fra_fft_bin(const s16 *src, int k, s32 *o_re, s32 *o_im)
{
    s32 mean = 0;
    u32 i;

    if ((k < 1) || (k >= (N / 2))) { return -1; }
    /* ★ 显式前提(原来是隐含假设): 本函数固定按 N=16384 点做 FFT, 与 k=f/500 的栅格假设配套。
       若当前 PL FFT/ADC 点数不是 16384(频谱仪开 CZT 时是 8192), 这里直接报失败 ——
       否则会拿"半条记录 + 上一轮残留数据"当整条用, 频率与相位都是错的。
       实际调用时 FRA 已用 adc_fs_apply(8.192MHz) 且离开频谱仪强制 czt_pl_set(0), 所以恒成立。 */
    if ((u32)czt_nfft() != (u32)N) { return -1; }
    for (i = 0; i < N; i++) { mean += (s32)src[i]; }
    /* 四舍五入(截断会残留最多 1LSB 直流, 虽只落 bin0, 但仍按最干净做法) */
    mean = (mean >= 0) ? ((mean + (s32)N / 2) / (s32)N) : ((mean - (s32)N / 2) / (s32)N);
    for (i = 0; i < N; i++) {                       /* 去直流(否则 0Hz 顶满) */
        ((s32 *)TxBufferPtr2)[i] = (s32)(s16)((s32)src[i] - mean);
    }
    for (i = 0; i < N; i++) {                       /* 压回 FFT 输入打包格式 */
        ((s32 *)TxBufferPtr2)[i] = (s32)((u16)(s16)((s32 *)TxBufferPtr2)[i]);
    }
    fft_pl_accelerate(TxBufferPtr2, RxBufferPtr2, (int)N, AxiDma2);   /* DMA/等待全在 plfft 里 */
    if ((RxDone2 == 0u) || (Error2 != 0u)) { return -1; }
#if FRA_FFT_RE_HIGH
    *o_re = (s32)(u32)((unsigned long long)Rx2[k] >> 32);          /* 高 32 位 = 实部 */
    *o_im = (s32)(u32)((unsigned long long)Rx2[k] & 0xFFFFFFFFu);  /* 低 32 位 = 虚部 */
#else
    *o_re = (s32)(u32)((unsigned long long)Rx2[k] & 0xFFFFFFFFu);
    *o_im = (s32)(u32)((unsigned long long)Rx2[k] >> 32);
#endif
    return 0;
}

/* ================= FRA 解算: 拟合 -> 逆FFT -> 阶跃 =================
   1) 实测的 |H|/相位 -> 复数响应;
   2) Levy + Sanathanan-Koerner 拟合 H(s)=B(s)/A(s)(阶数取界面"阶数", 失败自动降阶重试);
   3) 在 0..f_stop 的均匀栅格 f_k = k*2*f_stop/4096 上用模型求值 -> 4096 点逆FFT
      -> 冲激响应; 逐点累加 -> 阶跃响应(终值 = 直流增益)。
   ★ 频率必须缩放: 500Hz..1MHz 直接代入 s=j*2*pi*f, a_i 会跨十几个数量级, 正规方程
     严重病态(拟合直接失败)。这里统一用 f/1e5(100kHz 为单位), 求值时用同一单位,
     响应完全一样, 只是系数换了刻度。 */
#define FRA_FIT_SCALE   1.0e5

#if (UI_FRA_IFFT_N != FRA_IFFT_N)
#error "UI_FRA_IFFT_N 必须与 fra_core.h 的 FRA_IFFT_N 一致"
#endif

static double fra_ff[UI_FRA_MAX_PTS];        /* 拟合输入(频率, 已缩放) */
static double fra_fr[UI_FRA_MAX_PTS];        /* 实部 */
static double fra_fi[UI_FRA_MAX_PTS];        /* 虚部 */

/* ================= ★ PL(Levy HLS IP)二阶拟合 =================
   只用【前 512 个实测扫频点】(与屏幕上打点一致的那批数据里的前 512 个)。
   levy_ps_fit 的输出是 H(s) = (b0 + b1*s) / (s*s + a1*s + a0)  —— 分母 s*s 系数固定 1;
   而 fra_core 的约定是 A(s) = 1 + a1*s + a2*s*s, B(s) = b0 + b1*s
   ⇒ 同除 a0 归一化: b[0]=b0/a0, b[1]=b1/a0, a[1]=a1/a0, a[2]=1/a0   (二阶 ✓)
   返回 1 = 成功(b/a 有效), 0 = 失败(调用方回退 PS 软算 fra_fit)。 */
#define FRA_PL_MAX_PTS 512
static int fra_pl_fit(const ui_fra_t *f, int np, double *b, double *a, double *pco)
{
    /* ★ 静态 + 32 字节对齐: levy_ps_fit 要求 samples 32 字节对齐且位于 [0x100000,0x10000000),
       并在 IP 空闲前不得改写 —— 用静态数组保证存储期; 每点 3 个 float: [f, Re, Im]。 */
    static float s_levy[3 * FRA_PL_MAX_PTS] __attribute__((aligned(32)));
    double coeff[LEVY_PS_COEFFS];
    int    n = (np > FRA_PL_MAX_PTS) ? FRA_PL_MAX_PTS : np;
    int    k;

    if ((n < 4) || (b == 0) || (a == 0)) { return 0; }
    /* ★ 跳过第 0 个扫频点: 起扫第一点的相位是坏点(实测 f=1kHz 相位 -48.8°, 而 f=1.5kHz 只有 -3.1°),
       用户确认丢弃。所以从 k=1 开始, 数据紧凑写到 s_levy[3*(k-1)], 点数传 n-1。 */
    for (k = 1; k < n; k++) {
        double ph = (double)f->hpha[k] * (3.14159265358979323846 / 180.0);   /* 度 -> 弧度 */
        int    m2 = k - 1;

        s_levy[3 * m2 + 0] = (float)((double)f->f_start_hz + (double)k * (double)f->step_hz);  /* Hz ✓ */
        s_levy[3 * m2 + 1] = (float)((double)f->habs[k] * cos(ph));                            /* Re(H) ✓ */
        s_levy[3 * m2 + 2] = (float)((double)f->habs[k] * sin(ph));                            /* Im(H) ✓ */
    }
    if (levy_ps_fit(s_levy, (uint32_t)(n - 1), coeff) != LEVY_PS_OK) { return 0; }
    /* ★ 先把【物理】系数留一份给显示框: H(s) = (b0 + b1*s)/(s^2 + a1*s + a0), 单位 rad/s。
       (下面 b[]/a[] 要换算成 fra_eval 用的 u 域, 那是另一套, 不能拿去显示。) */
    if (pco != 0) {
        pco[0] = coeff[0]; pco[1] = coeff[1]; pco[2] = coeff[2]; pco[3] = coeff[3];
    }
    /* ★ a0 的相对判据(a0 真实量级 1e7~1e9, 绝对阈值 1e-30 形同虚设) */
    {
        double amax = fabs(coeff[0]);

        if (fabs(coeff[1]) > amax) { amax = fabs(coeff[1]); }
        if (fabs(coeff[2]) > amax) { amax = fabs(coeff[2]); }
        if (fabs(coeff[3]) > amax) { amax = fabs(coeff[3]); }
        if ((amax <= 0.0) || (fabs(coeff[2]) <= (1e-12 * amax))) { return 0; }   /* a0 退化 -> 无法归一化 */
    }

    /* ★★ 频率尺度换算(关键!):
       levy_ps 输出的 [b0,b1,a0,a1] 是【真 rad/s 域】: H(s)=(b0+b1*s)/(s*s+a1*s+a0), s = j*2*pi*f。
       而 fra_core 的 fra_eval(b,m,a,n,f_hz) 内部是 w = 2*pi*f_hz, 且 helloworld 传进去的是
       fra_ff[k] = 真实频率 / FRA_FIT_SCALE(1e5), 而 fra_eval 内部还会再乘 2*pi
       所以必须做代换 s = k*u:
           H(k*u) = (b0 + b1*k*u) / (k^2*u^2 + a1*k*u + a0)   再除以 a0 归一化成
           B(s) = b0/a0 + (b1*k/a0)*u ,  A(u) = 1 + (a1*k/a0)*u + (k^2/a0)*u^2
       以前漏了 k 与 k^2 -> 分母的 u^2 项被忽略 -> 拟合曲线被压成一条平线(实测现象)。 */
    {
        /* s_phys = FRA_FIT_SCALE * u  (u = fra_eval 内部的 w = 2*pi*f_hz, f_hz = f_real/1e5)
           规律: s 缩小 k 倍时 b_i -> b_i*k^i, a_i -> a_i*k^i。
           注意 k 就是 FRA_FIT_SCALE 本身(1e5), 不要乘 2*pi —— 2*pi 已经在 fra_eval 里面乘过了。 */
        const double k = (double)FRA_FIT_SCALE;

        a[0] = 1.0;                       /* ★★ 必须! fra_core 的 cpoly 会读 a[0](归一化常数项) */
        b[0] = coeff[0] / coeff[2];
        b[1] = coeff[1] / coeff[2] * k;
        a[1] = coeff[3] / coeff[2] * k;
        a[2] = 1.0 / coeff[2] * k * k;
    }
    return 1;
}

/* ================= ★ 用 PL 二阶模型算时域响应(供冲激/阶跃页叠加显示) =================
   与实测那条路【完全同一套换算】: 同一 ksc 定标 -> 同一 bin 映射(f/500) -> 同一共轭镜像
   -> 同一个 ifft_pl_accelerate -> 同一个 re/sc*vpp/2000 电压换算 -> 同一个 fra_step 累加。
   只把频域数据从"实测 |H|/相位"换成"模型 fra_eval 的结果"。全 16384 点(阶跃要全部点)。 */
static void fra_time_from_model(const double *b, const double *a, int np)
{
    ui_fra_t *f = &app.fra;
    int nbins = UI_FRA_IFFT_N / 2;
    double ksc, kmax = 0.0;
    int i2, k2;

    f->fit_time_ok = 0u;
    if ((np < 4) || (b == 0) || (a == 0)) { return; }

    for (i2 = 0; i2 < UI_FRA_IFFT_N; i2++) { ((u32 *)TxBufferPtr2)[i2] = 0u; }
    for (k2 = 0; k2 < np; k2++) {                       /* 模型 |H| 的最大值(定标与实测同法) */
        double vr, vi, mg;

        fra_eval(b, 1, a, 2, fra_ff[k2], &vr, &vi);
        mg = sqrt(vr * vr + vi * vi);
        if (mg > kmax) { kmax = mg; }
    }
    if (kmax < 1e-12) { kmax = 1e-12; }
    ksc = 32000.0 / kmax;

    {   /* 模型频响 -> bin; 点间线性插值补空 bin(与实测路一致) */
        int    prev_bin = -1;
        double prev_r = 0.0, prev_i = 0.0;

        for (k2 = 0; k2 < np; k2++) {
            int    bin = (int)(((double)f->f_start_hz + (double)k2 * (double)f->step_hz) / 500.0 + 0.5);
            double vr, vi, cr, ci;
            s32    re, im;

            fra_eval(b, 1, a, 2, fra_ff[k2], &vr, &vi);
            cr = vr;
            ci = vi;
            if ((bin < 1) || (bin > nbins)) { continue; }
            if ((prev_bin >= 1) && ((bin - prev_bin) > 1)) {
                int bb;

                for (bb = prev_bin + 1; bb < bin; bb++) {
                    double u = (double)(bb - prev_bin) / (double)(bin - prev_bin);
                    s32 rr = (s32)((prev_r + (cr - prev_r) * u) * ksc);
                    s32 ii = (s32)((prev_i + (ci - prev_i) * u) * ksc);

                    if (rr >  32767) { rr =  32767; }
                    if (rr < -32768) { rr = -32768; }
                    if (ii >  32767) { ii =  32767; }
                    if (ii < -32768) { ii = -32768; }
                    ((u32 *)TxBufferPtr2)[bb] = (u32)(((u32)(u16)(s16)rr) | (((u32)(u16)(s16)ii) << 16));
                }
            }
            re = (s32)(cr * ksc);
            im = (s32)(ci * ksc);
            if (re >  32767) { re =  32767; }
            if (re < -32768) { re = -32768; }
            if (im >  32767) { im =  32767; }
            if (im < -32768) { im = -32768; }
            ((u32 *)TxBufferPtr2)[bin] = (u32)(((u32)(u16)(s16)re) | (((u32)(u16)(s16)im) << 16));
            prev_bin = bin;
            prev_r = cr;
            prev_i = ci;
        }
    }
    {   /* 直流 bin = 最低频 bin 的实部, 虚部强置 0 */
        u32 v0 = ((u32 *)TxBufferPtr2)[1];
        s32 re0 = (s32)(v0 & 0xFFFFu);

        if (re0 >= 32768) { re0 -= 65536; }
        ((u32 *)TxBufferPtr2)[0] = (u32)(u16)(s16)re0;
    }
    {   /* 奈奎斯特 bin 虚部清零 */
        u32 vn = ((u32 *)TxBufferPtr2)[nbins];
        s32 ren = (s32)(vn & 0xFFFFu);

        if (ren >= 32768) { ren -= 65536; }
        ((u32 *)TxBufferPtr2)[nbins] = (u32)(u16)(s16)ren;
    }
    for (i2 = 1; i2 < nbins; i2++) {                   /* 共轭对称 */
        u32 v = ((u32 *)TxBufferPtr2)[i2];
        s32 re = (s32)(v & 0xFFFFu);
        s32 im = (s32)(v >> 16);

        if (re >= 32768) { re -= 65536; }
        if (im >= 32768) { im -= 65536; }
        im = -im;
        ((u32 *)TxBufferPtr2)[UI_FRA_IFFT_N - i2] = (u32)(((u32)(u16)(s16)re) | (((u32)(u16)(s16)im) << 16));
    }

    ifft_pl_accelerate(TxBufferPtr2, RxBufferPtr2, (int)UI_FRA_IFFT_N, AxiDma2);
    if ((RxDone2 == 0u) || (Error2 != 0u)) { return; }

    {   /* 与实测同一套电压换算 + 阶跃累加 */
        double sc = (double)UI_FRA_IFFT_N * ksc;

        for (i2 = 0; i2 < UI_FRA_IFFT_N; i2++) {
            s32 re = (s32)((u32)(Rx2[i2] & 0xFFFFFFFFu));

            f->imp2[i2] = (float)((double)re / sc * (double)f->vpp_mv / 2000.0);
        }
        fra_step(f->imp2, UI_FRA_IFFT_N, f->stp2);

        {   /* 模型自身的自动量程(用于与实测取并集) */
            float lo, hi;
            int   i4;

            lo = f->imp2[0]; hi = f->imp2[0];
            for (i4 = 1; i4 < UI_FRA_IFFT_N; i4++) {
                if (f->imp2[i4] < lo) { lo = f->imp2[i4]; }
                if (f->imp2[i4] > hi) { hi = f->imp2[i4]; }
            }
            if (lo > 0.0f) { lo = 0.0f; }
            if (hi < 0.0f) { hi = 0.0f; }
            if ((hi - lo) < 1e-6f) { hi = lo + 1e-3f; }
            f->imp2_lo = lo; f->imp2_hi = hi;

            lo = f->stp2[0]; hi = f->stp2[0];
            for (i4 = 1; i4 < UI_FRA_IFFT_N; i4++) {
                if (f->stp2[i4] < lo) { lo = f->stp2[i4]; }
                if (f->stp2[i4] > hi) { hi = f->stp2[i4]; }
            }
            if (lo > 0.0f) { lo = 0.0f; }
            if (hi < 0.0f) { hi = 0.0f; }
            if ((hi - lo) < 1e-6f) { hi = lo + 1e-3f; }
            f->stp2_lo = lo; f->stp2_hi = hi;
        }
        f->fit_time_ok = 1u;
    }
}

static void fra_solve(void)
{
    ui_fra_t *f = &app.fra;
    double b[FRA_MAX_ORDER + 1], a[FRA_MAX_ORDER + 1];
    double res = -1.0;
    int np, m = 0, n = 0, ord, k;
    int pl_used = 0;                 /* ★ 1 = 本次走 PL(levy_ps_fit), 系数为物理 rad/s */
    double pco[4] = { 0.0, 0.0, 0.0, 0.0 };   /* ★ PL 返回的物理系数 [b0,b1,a0,a1] */

    f->fit_ok    = 0u;
    f->fit_time_ok = 0u;          /* ★ 模型时域响应每次重算时先作废(失败时不显示旧曲线) */
    f->tf_ok       = 0u;          /* ★ 传递函数显示框: 本次没拟合成功就不显示 */
    f->fit_res   = 0.0f;
    f->fit_order = 0u;
    np = (int)f->npt;
    if (np < 4) { return; }

        /* ★ 截止频率: 以【0dB(|H|=1.0)】为基准, 找 |H| 首次下降到 -3.01dB(0.7071) 的穿越点,
       然后取【跨阈值的相邻两点中离 -3dB 更近的那个网格点】(不插值, 按用户要求)。
       这样 fc 一定落在扫描网格上 -> 光标能正好停在 fc 上, 读数与标注是同一个点。
       兜底: 若 0dB 基准穿不过(通带本来就不是 0dB, 真分压等), 退回"实测峰值 -3dB"。
       与拟合无关(拟合失败也能显示)。 */
    f->fc_ok  = 0u;
    f->fc_hz  = 0;
    f->fc_lvl = 0.0f;
    {
        double hmax = 0.0;
        int i3;

        for (i3 = 0; i3 < np; i3++) {
            double mg = (double)f->habs[i3];

            if (mg > hmax) { hmax = mg; }
        }
        if (hmax > 1e-9) {
            /* XXFC0DB: 截止频率的基准改成【0dB(|H| = 1.0)】—— 与光标显示的 -3.00dB 同一定义,
               这样"标注的 fc"和"用光标找 -3dB"给出的是同一个频率(原来是相对【实测峰值 hmax】,
               而 hmax 常因参考/被测通道标定差而非 1.0(例如 0.967 = -0.29dB) -> 两者差一段频率)。
               兜底: 若通带本来就不是 0dB(真分压等), 0dB 基准永远穿不过 -> 再退回 hmax 基准,
               保证任何情况下都不会丢掉标注。 */
            double thr = 0.7071067811865476;             /* |H| = 1.0 上的 -3.0103dB */
            int found = 0;

            f->fc_lvl = (float)thr;
            for (i3 = 0; (i3 + 1) < np; i3++) {
                double m0 = (double)f->habs[i3];
                double m1 = (double)f->habs[i3 + 1];

                if ((m0 >= thr) && (m1 < thr)) {         /* 向下穿过 -3dB(0dB 基准) */
                    /* XXNEAR: 按用户要求【取离 -3dB 最近的那个网格点】, 不再在两点之间插值。
                       好处: fc 一定落在扫描网格上 -> 光标可以正好停在 fc 上, 读数与标注完全一致;
                       且 fc_lvl 存该点的真实电平, 横线正好穿过那个点。 */
                    int kk = ((thr - m1) < (m0 - thr)) ? (i3 + 1) : i3;

                    f->fc_hz  = (s32)((double)f->f_start_hz + (double)kk * (double)f->step_hz + 0.5);
                    f->fc_lvl = f->habs[kk];   /* 恒等于该网格点的实测电平 */
                    f->fc_ok  = 1u;
                    found = 1;
                    break;
                }
            }
            if (found == 0) {                            /* 兜底: 退回"实测峰值 -3dB" */
                thr = hmax * 0.7071067811865476;
                f->fc_lvl = (float)thr;
                for (i3 = 0; (i3 + 1) < np; i3++) {
                    double m0 = (double)f->habs[i3];
                    double m1 = (double)f->habs[i3 + 1];

                    if ((m0 >= thr) && (m1 < thr)) {
                        /* XXNEAR: 兜底路径同样取离 -3dB 最近的网格点(与主路径一致)。 */
                        int kk = ((thr - m1) < (m0 - thr)) ? (i3 + 1) : i3;

                        f->fc_hz  = (s32)((double)f->f_start_hz + (double)kk * (double)f->step_hz + 0.5);
                        f->fc_lvl = f->habs[kk];   /* 恒等于该网格点的实测电平 */
                        f->fc_ok  = 1u;
                        break;
                    }
                }
            }
        }
    }

    for (k = 0; k < np; k++) {
        double mag = (double)f->habs[k];
        double ph  = (double)f->hpha[k] * (3.14159265358979323846 / 180.0);

        fra_ff[k] = ((double)f->f_start_hz + (double)k * (double)f->step_hz) / FRA_FIT_SCALE;
        fra_fr[k] = mag * cos(ph);
        fra_fi[k] = mag * sin(ph);
    }
    for (ord = 2; ord >= 1; ord--) {                              /* ★ 阶数固定二阶(PL 只出二阶) */
        if (ord != 2) { break; }                              /* 不再自动降阶 */
        m = ord;
        n = ord;
        if (np <= ((m + n + 1) * 4)) { continue; }   /* ★ 每 4 个点养 1 个未知数: 否则正规方程病态, 会拟合出"野模型" */
        for (k = 0; k <= FRA_MAX_ORDER; k++) { b[k] = 0.0; a[k] = 0.0; }
        /* ★★ 先试 PL(Levy HLS IP)二阶拟合 —— 调用你写好的 levy_ps_fit(), 只用前 512 个实测点。
           成功: b[0..1]/a[1..2] 已是 fra_core 约定形式(由 fra_pl_fit 归一化), 自己算残差 RMS。
           失败: 落到下面的 PS 软算 fra_fit()(回退, 行为与以前完全一致)。 */
        if ((ord == 2) && (fra_pl_fit(f, np, b, a, pco) != 0)) {
            double s2 = 0.0;
            int    kk;

            for (kk = 0; kk < np; kk++) {
                double vr, vi, er, ei;

                fra_eval(b, m, a, n, fra_ff[kk], &vr, &vi);
                er = fra_fr[kk] - vr;
                ei = fra_fi[kk] - vi;
                s2 += er * er + ei * ei;
            }
            pl_used = 1;                              /* ★ 记录: 本组系数是物理 rad/s */
            res = sqrt(s2 / (double)np);
            f->fit_order = (u8)ord;
            break;
        }
        res = fra_fit(fra_ff, fra_fr, fra_fi, np, m, n, b, a);
        if (res >= 0.0) { f->fit_order = (u8)ord; break; }
    }
    if (!(res >= 0.0)) { return; }        /* ★ NaN 也算失败(原来 res<0.0 对 NaN 为假 -> 乱码显示) */
    f->fit_res = (float)res;
    /* ★★ 保存【物理】传递函数系数(二阶, 单位 rad/s): H(s) = (b0 + b1*s) / (s^2 + a1*s + a0)
       - PL 路径: coeff=[b0,b1,a0,a1] 本来就是物理值 -> 直接除 a0 归一化即可;
       - 软算路径: fra_fit 得到的是 u = s/k 域的系数(k = FRA_FIT_SCALE), 要反算回去:
             s_phys = k*u  =>  B_phys = (b0 + b1*u)*k^2/a2 , A_phys = s^2 + (a1*k/a2)*s + k^2/a2
           (以前我直接把 u 域系数当物理值显示 -> a0 变成 1、a1 被乘了 1e5, 所以显示框的数字不对) */
    {
        double pb0, pb1, pa0, pa1;

        /* ★ 统一成"常数项归一化为 1"的写法(用户要求的格式, 也是 fra_core 的约定):
             H(s) = (1 + b1*s + b0*s^2) / (1 + a1*s + a0*s^2)
           PL 原始返回是 H=(B0 + B1*s)/(A0 + A1*s + s^2), 所以:
             b0 = 0(分子无 s^2 项) , b1 = B1/B0 , a1 = A1/A0 , a0 = 1/A0           */
        if (pl_used != 0) {
            double B0 = pco[0], B1 = pco[1], A0 = pco[2], A1 = pco[3];

            if (fabs(B0) > 0.0) { pb1 = B1 / B0; } else { pb1 = 0.0; }
            pb0 = 0.0;
            if (fabs(A0) > 0.0) { pa1 = A1 / A0; pa0 = 1.0 / A0; }
            else                { pa1 = 0.0;      pa0 = 1.0; }
        } else if (fabs(a[2]) > 1e-30) {
            const double kk = (double)FRA_FIT_SCALE;

            pb0 = b[0] * kk * kk / a[2];
            pb1 = b[1] * kk / a[2];
            pa0 = kk * kk / a[2];
            pa1 = a[1] * kk / a[2];
        } else {
            pb0 = b[0]; pb1 = b[1]; pa0 = a[0]; pa1 = a[1];
        }
        f->tf_b0 = (float)pb0;
        f->tf_b1 = (float)pb1;
        f->tf_a0 = (float)pa0;
        f->tf_a1 = (float)pa1;

        {   /* 极点: pa0*s^2 + pa1*s + 1 = 0  ->  s = [-pa1 ± sqrt(pa1^2 - 4*pa0)] / (2*pa0) */
            double disc = pa1 * pa1 - 4.0 * pa0;
            double d2   = 2.0 * pa0;

            if (fabs(d2) < 1e-300) { d2 = 1e-300; }
            if (disc >= 0.0) {                      /* 两个实极点 */
                double rt = sqrt(disc);

                f->tf_pr = (float)((-pa1 + rt) / d2);
                f->tf_pi = 0.0f;
            } else {                                /* 一对共轭复极点: 实部 -pa1/(2*pa0) */
                f->tf_pr = (float)(-pa1 / d2);
                f->tf_pi = (float)(sqrt(-disc) / d2);
            }
        }
    }
    f->tf_ok = 1u;


    /* ★ 拟合曲线(叠加到幅频/相频页): 在实测点上用模型求值。
       相位先对齐到实测那一支, 再压回 (-180,180], 与实测同域, 白线才会贴着彩点。
       (这段之前被我误删过 —— 删"PS 逆 FFT"时范围划大了, 结果白线一直是全 0 的平线。) */
    for (k = 0; k < np; k++) {
        double vr, vi, mag, ph, tgt;

        fra_eval(b, m, a, n, fra_ff[k], &vr, &vi);
        mag = sqrt(vr * vr + vi * vi);
        ph  = atan2(vi, vr) * (180.0 / 3.14159265358979323846);
        tgt = (double)f->hpha[k];
        while ((ph - tgt) > 180.0)  { ph -= 360.0; }
        while ((ph - tgt) < -180.0) { ph += 360.0; }
        if (ph > 180.0)   { ph -= 360.0; }
        if (ph <= -180.0) { ph += 360.0; }
        f->fit_mag[k] = (float)mag;
        f->fit_pha[k] = (float)ph;
    }

    /* ★ 冲激/阶跃响应 = 【实测】频率响应(|H| 与相位)的逆 FFT —— 直接用实测数据, 不经拟合模型。
       实测点本来就落在 fs/N = 500Hz 的整数倍上(起止频率与步长都是 500Hz 的整数倍),
       所以直接放进 4096 点逆 FFT 的第 bin = f/500 个 bin, 其余 bin 补 0(带外本来就没测)。
       栅格 Δf = 500Hz, 最高 2048*500 = 1.024MHz; 时基 dt = 1/(2*1.024MHz) = 488ns,
       记录长度 4096*488ns = 2ms。 */
    {
        int nbins = UI_FRA_IFFT_N / 2;                 /* 2048 */
        double ksc, kmax = 0.0;
        int i2, k2;

        for (i2 = 0; i2 < UI_FRA_IFFT_N; i2++) { ((u32 *)TxBufferPtr2)[i2] = 0u; }

        for (k2 = 0; k2 < np; k2++) {                  /* 定标: 取实测最大幅值 */
            double mg = (double)f->habs[k2];

            if (mg > kmax) { kmax = mg; }
        }
        if (kmax < 1e-12) { kmax = 1e-12; }
        ksc = 32000.0 / kmax;      /* ★ 实部/虚部各尽量用满 16 位(共用同一刻度, 不破坏相位) */

        {   /* 实测点 -> 对应 bin; 相邻两点之间线性插值补齐。
               步长 500Hz 时每个 bin 都有实测点(不会发生插值);
               步长 1000Hz 时会隔一个空一个, 不补的话冲激响应会"断断续续"。 */
            int    prev_bin = -1;
            double prev_r = 0.0, prev_i = 0.0;

            for (k2 = 0; k2 < np; k2++) {
                int bin = (int)(((double)f->f_start_hz + (double)k2 * (double)f->step_hz)
                                / 500.0 + 0.5);
                double ph = (double)f->hpha[k2] * (3.14159265358979323846 / 180.0);
                double cr = (double)f->habs[k2] * cos(ph);
                double ci = (double)f->habs[k2] * sin(ph);
                s32 re, im;

                if ((bin < 1) || (bin > nbins)) { continue; }
                if ((prev_bin >= 1) && ((bin - prev_bin) > 1)) {   /* 补中间空 bin */
                    int bb;

                    for (bb = prev_bin + 1; bb < bin; bb++) {
                        double u = (double)(bb - prev_bin) / (double)(bin - prev_bin);
                        s32 rr = (s32)((prev_r + (cr - prev_r) * u) * ksc);
                        s32 ii = (s32)((prev_i + (ci - prev_i) * u) * ksc);

                        if (rr >  32767) { rr =  32767; }
                        if (rr < -32768) { rr = -32768; }
                        if (ii >  32767) { ii =  32767; }
                        if (ii < -32768) { ii = -32768; }
                        ((u32 *)TxBufferPtr2)[bb] =
                            (u32)(((u32)(u16)(s16)rr) | (((u32)(u16)(s16)ii) << 16));
                    }
                }
                re = (s32)(cr * ksc);
                im = (s32)(ci * ksc);
                if (re >  32767) { re =  32767; }
                if (re < -32768) { re = -32768; }
                if (im >  32767) { im =  32767; }
                if (im < -32768) { im = -32768; }
                ((u32 *)TxBufferPtr2)[bin] =
                    (u32)(((u32)(u16)(s16)re) | (((u32)(u16)(s16)im) << 16));
                prev_bin = bin;
                prev_r = cr;
                prev_i = ci;
            }
        }
        {   /* 直流 bin: 借用最低频 bin 的实部, 虚部强制 0(实序列要求) */
            u32 v0 = ((u32 *)TxBufferPtr2)[1];
            s32 re0 = (s32)(v0 & 0xFFFFu);

            if (re0 >= 32768) { re0 -= 65536; }
            ((u32 *)TxBufferPtr2)[0] = (u32)(u16)(s16)re0;
        }
        {   /* 奈奎斯特 bin 虚部清零 */
            u32 vn = ((u32 *)TxBufferPtr2)[nbins];
            s32 ren = (s32)(vn & 0xFFFFu);

            if (ren >= 32768) { ren -= 65536; }
            ((u32 *)TxBufferPtr2)[nbins] = (u32)(u16)(s16)ren;
        }
        for (i2 = 1; i2 < nbins; i2++) {               /* 厄米对称 X[N-k] = conj(X[k]) */
            u32 v = ((u32 *)TxBufferPtr2)[i2];
            s32 re = (s32)(v & 0xFFFFu);
            s32 im = (s32)(v >> 16);

            if (re >= 32768) { re -= 65536; }
            if (im >= 32768) { im -= 65536; }
            im = -im;
            ((u32 *)TxBufferPtr2)[UI_FRA_IFFT_N - i2] =
                (u32)(((u32)(u16)(s16)re) | (((u32)(u16)(s16)im) << 16));
        }

        ifft_pl_accelerate(TxBufferPtr2, RxBufferPtr2, (int)UI_FRA_IFFT_N, AxiDma2);

        f->imp_fmax_hz = (UI_FRA_IFFT_N / 2) * 500;    /* 1.024MHz */
        f->ifft_ok = 0u;
        if ((RxDone2 != 0u) && (Error2 == 0u)) {
            double sc = (double)UI_FRA_IFFT_N * ksc;

            for (i2 = 0; i2 < UI_FRA_IFFT_N; i2++) {
                s32 re = (s32)((u32)(Rx2[i2] & 0xFFFFFFFFu));

                /* ★ 换算成电压: h[n] 是"每伏输入"的响应, 乘输入幅度 A = vpp_mv/2000 (V)
                   阶跃终值 = A*|H(0)| = 实际输出直流电压, 纵轴单位才是真的 V */
                f->imp[i2] = (float)((double)re / sc * (double)f->vpp_mv / 2000.0);
            }
            fra_step(f->imp, UI_FRA_IFFT_N, f->stp);
            f->ifft_ok = 1u;
            /* ★ 计算完成后只做一次"自动设置"(类似示波器 autoset): 取曲线的最小/最大,
               上下各留 5% 余量, 并把 0V 包进范围(基线可见)。显示期间不再变动。 */
            {
                int i4;
                float lo = f->imp[0], hi = f->imp[0];

                for (i4 = 1; i4 < UI_FRA_IFFT_N; i4++) {
                    if (f->imp[i4] < lo) { lo = f->imp[i4]; }
                    if (f->imp[i4] > hi) { hi = f->imp[i4]; }
                }
                if (lo > 0.0f) { lo = 0.0f; }              /* 把 0V 包进来 */
                if (hi < 0.0f) { hi = 0.0f; }
                if ((hi - lo) < 1e-6f) { hi = lo + 1e-3f; }
                {
                    float dd = (hi - lo) * 0.05f;

                    f->imp_lo = lo - dd;
                    f->imp_hi = hi + dd;
                }
                lo = f->stp[0]; hi = f->stp[0];
                for (i4 = 1; i4 < UI_FRA_IFFT_N; i4++) {
                    if (f->stp[i4] < lo) { lo = f->stp[i4]; }
                    if (f->stp[i4] > hi) { hi = f->stp[i4]; }
                }
                if (lo > 0.0f) { lo = 0.0f; }
                if (hi < 0.0f) { hi = 0.0f; }
                if ((hi - lo) < 1e-6f) { hi = lo + 1e-3f; }
                {
                    float dd = (hi - lo) * 0.05f;

                    f->stp_lo = lo - dd;
                    f->stp_hi = hi + dd;
                }
            }
        }
        {
            float amax = 0.0f;
            int pn = 0;

            for (i2 = 0; i2 < UI_FRA_IFFT_N; i2++) {
                float av = (f->imp[i2] < 0.0f) ? -f->imp[i2] : f->imp[i2];

                if (av > amax) { amax = av; pn = i2; }
            }
            f->dbg_sumh   = f->stp[UI_FRA_IFFT_N - 1];
            f->dbg_h0dc   = (float)((double)f->habs[0]);   /* 参考: 最低频实测 |H| */
            f->dbg_peak_n = (s32)pn;
        }
    }

    /* ★ 用 PL 二阶模型再算一遍时域响应(冲激/阶跃页叠加显示用)。
       必须放在实测 IFFT 之后(它会复用 TxBufferPtr2/RxBufferPtr2 与同一个 DMA),
       且必须传模型系数 b/a 与点数 np。函数内部自带 fit_time_ok 的置位/清零。 */
    fra_time_from_model(b, a, np);

    f->fit_ok = 1u;      /* ★ 拟合成功: 幅频/相频页的白色叠加线靠它才会画(之前被误删 -> 白线消失) */
}

/* 每帧调用一次: 扫频状态机(不阻塞, 只在 FRA 界面跑, 与频谱仪互斥) */
/* ================= ★ 扫频点抽稀(仅当点数 > 2048 时启用) =================
   需求: 扫频点数仍由"步长+范围"决定, 可以超过 2048; 超过时从扫频点里取 2048 个,
         不是按数组位置平均, 而是【按 log f 平均】—— 横轴是 log, 等屏幕距离 = 等频率比,
         于是低频段每 Hz 取到的点更多, 更利于还原。
   实现: 扫频仍逐点全扫(不跳点), 只保存"命中目标序号"的点。
         点数 <= 2048 时目标就是 0,1,2,... (等价于全存) -> 行为与以前完全一致。 */
#define FRA_PICK_MAX 2048
static u16 fra_pick[FRA_PICK_MAX];      /* 要保存的扫频序号(严格递增) */
static int fra_pick_n = 0;              /* 目标个数 */
static int fra_pick_w = 0;              /* 下一个要命中的目标下标 */

static void fra_pick_build(s32 npt)
{
    int j;

    fra_pick_n = 0;
    fra_pick_w = 0;
    if (npt <= (s32)FRA_PICK_MAX) {                     /* 不超上限: 全存(原行为, 一行不变) */
        fra_pick_n = (int)npt;
        for (j = 0; j < fra_pick_n; j++) { fra_pick[j] = (u16)j; }
        return;
    }
    {   /* 超上限: 按 log f 均匀取 FRA_PICK_MAX 个(等比级数 -> 去重) */
        double f0 = (double)app.fra.f_start_hz;
        double r  = (double)app.fra.f_stop_hz / ((f0 > 1.0) ? f0 : 1.0);
        int    last = -1;
        int    cnt  = 0;

        for (j = 0; j < (int)FRA_PICK_MAX; j++) {
            double fr  = f0 * pow(r, (double)j / (double)(FRA_PICK_MAX - 1));
            int    idx = (int)((fr - f0) / (double)app.fra.step_hz + 0.5);

            if (idx < 0) { idx = 0; }
            if (idx > (int)(npt - 1)) { idx = (int)(npt - 1); }
            if (idx == last) { continue; }              /* 低频端会重合: 去重 */
            last = idx;
            fra_pick[cnt] = (u16)idx;
            cnt++;
        }
        if ((cnt > 0) && (fra_pick[cnt - 1] != (u16)(npt - 1))) {   /* 末端点一定保留 */
            if (cnt < (int)FRA_PICK_MAX) { fra_pick[cnt] = (u16)(npt - 1); cnt++; }
            else { fra_pick[cnt - 1] = (u16)(npt - 1); }
        }
        fra_pick_n = cnt;
    }
}

/* 当前扫频序号是否需要保存(序号严格递增, 线性游标即可) */
static int fra_pick_hit(int idx)
{
    if (fra_pick_w >= fra_pick_n) { return 0; }
    if (idx == (int)fra_pick[fra_pick_w]) { fra_pick_w++; return 1; }
    return 0;
}
static void fra_sweep_step(void)
{
    ui_fra_t *f = &app.fra;
    s32 npt, ftgt;
    int k;

    if ((f->running == 0u) || (app.screen != UI_SCREEN_FRA)) {
            f->running = 0u;            /* ★ 离开 FRA 界面必须停扫: 否则 running 永远为 1,
                                           下面 AWG 的 fra.running==0 门控永久生效,
                                           信号发生器的参数再也下发不出去(要回 FRA 页按停止才恢复)。 */
        fra_last_run = 0u;                 /* 停止或离开界面: 下次按"运行"从第一步开始 */
        return;
    }
    if (fra_last_run == 0u) {              /* 刚按下"运行": 清掉上次中途停下的相位 */
        fra_phase = 0u;
    }
    fra_last_run = 1u;
    npt = (f->step_hz > 0) ? ((f->f_stop_hz - f->f_start_hz) / f->step_hz + 1) : 0;
    if (npt <= 0) { f->running = 0u; return; }        /* ★ 不再限制 2048: 扫全部, 只存目标点 */

    if (fra_phase == 0u) {                          /* 起扫 */
        fra_pidx = 0u; fra_fcnt = 0u; f->npt = 0u; f->running = 1u;
        fra_pick_build(npt);           /* ★ npt>2048 时按 log f 均匀取 2048 个目标序号; <=2048 时全存 */
        /* ★ 必须把 ADC 采样率切到 8.192MHz: bin 号 k = f/500 就是按 fs=8.192MHz+N=16384 算的。
           示波器可能停在 5/6/8/16/32/48/64MHz, 不切的话整条频率轴都会错。
           is_spec=1 允许小数档(8.192MHz 正是频谱仪档); 频率没变时函数内部直接返回。 */
        adc_fs_apply(ADC_FS_SPEC_MHZ, 1);
        ftgt = f->f_start_hz;
        awg_apply_dds_ch(1, 1u, 0u, ftgt, f->vpp_mv, 0);   /* CH2 = DAC2: 同时喂参考与 DUT */
        f->f_meas_hz = ftgt;
        f->redraw_curve = 1u;
        f->fit_ok  = 0u;      /* ★ 重新起扫: 先清掉上一次的拟合结果, 否则旧的白色拟合线会叠在新点上 */
        f->run_win = 1u;                   /* ★ 主循环弹"正在运行......"子窗口 */
        fra_phase = 1u;
        return;
    }
    if (fra_phase == 1u) {                          /* 等稳定(首点等更久: 上电/换频后要时间) */
        fra_fcnt++;
        if (fra_fcnt >= (u8)((fra_pidx == 0u) ? (FRA_SETTLE_FRAMES * 10u) : FRA_SETTLE_FRAMES)) {
            /* ★ 进测量前再写一次频率: 上电初始化流程可能在首点之后又写过 DDS, 把 CH2 覆盖掉
               (实测现象: 第一个频点没信号, 第二个起正常) */
            ftgt = f->f_start_hz + (s32)fra_pidx * f->step_hz;
            awg_apply_dds_ch(1, 1u, 0u, ftgt, f->vpp_mv, 0);
            fra_fcnt = 0u; fra_hr = 0.0; fra_hi = 0.0;
            fra_fok = 0u; fra_retry = 0u;      /* ★ 本频点的有效帧计数清零 */
            fra_phase = 2u;
        }
        return;
    }
    if (fra_phase == 2u) {                          /* 本帧: 两路各 FFT 一次 → 复数比值 */
        ftgt = f->f_start_hz + (s32)fra_pidx * f->step_hz;
        k = (int)((double)ftgt * (double)czt_nfft() / (adc_fs_mhz * 1000000.0)   /* ★ 用实际采样率与当前 FFT 点数(N 是写死的 16384) */ + 0.5);      /* = f/500 */
        {
            const s16 *pdut = (FRA_DUT_IS_BUF0 != 0) ? adc_buf0 : adc_buf1;   /* 被测 */
            const s16 *pref = (FRA_DUT_IS_BUF0 != 0) ? adc_buf1 : adc_buf0;   /* 参考 */
            s32 yr, yi, xr, xi;

            if ((fra_fft_bin(pdut, k, &yr, &yi) == 0) &&
                (fra_fft_bin(pref, k, &xr, &xi) == 0)) {
                double d = (double)xr * (double)xr + (double)xi * (double)xi;

                if (d > 1.0) {
                    fra_fok++;            /* ★ 这一帧有效, 参与平均 */
                    fra_hr += ((double)yr * (double)xr + (double)yi * (double)xi) / d;
                    fra_hi += ((double)yi * (double)xr - (double)yr * (double)xi) / d;
                }
            }
        }
        fra_fcnt++;
        if (fra_fcnt >= f->avg) {                   /* 本频点测完 */
            /* ★ 分母必须是"真正算进去的帧数": 用 avg 的话, 只要有几帧 FFT 失败/参考太弱,
               幅度就会被按比例缩小(4 帧成 1 帧 -> 只剩 1/4) -> 曲线一段段断裂。 */
            if ((fra_fok == 0u) && (fra_retry < 20u)) {     /* 一帧都没成: 重测这个频点 */
                fra_retry++;
                fra_fcnt = 0u;
                return;
            }
            double m = sqrt(fra_hr * fra_hr + fra_hi * fra_hi) / (double)((fra_fok > 0u) ? fra_fok : 1u);
            double p = atan2(fra_hi, fra_hr) * 180.0 / 3.14159265358979323846;

            /* ★ 相位范围固定 (-180, 180](用户给的框架, 直接卷绕, 不做跨点解卷绕):
               H = Y/X 的相位本来就是 atan2 的结果, 落在这里; 再显式卷绕一次保证范围。
               (原来我额外加的"跨点解卷绕"会让相位跑出 ±180, 已删除。) */
            if (p > 180.0) {
                p -= 360.0;
            } else if (p <= -180.0) {
                p += 360.0;
            }
            if (fra_pick_hit((int)fra_pidx) != 0) {   /* ★ 只存命中目标表的点(npt<=2048 时恒真) */
                f->habs[f->npt] = (float)m;
                f->hpha[f->npt] = (float)p;
                f->npt++;
            }
            fra_fcnt = 0u;
            fra_pidx++;
            if ((s32)fra_pidx >= npt) {             /* 扫完 */
                fra_phase = 3u;
                f->running = 0u;
                fra_solve();                        /* ★ 拟合 + 逆FFT -> 冲激/阶跃响应 */
                f->run_win = 2u;                    /* ★ 主循环显示"运行结束"并延迟关窗 */
            } else {
                ftgt = f->f_start_hz + (s32)fra_pidx * f->step_hz;
                awg_apply_dds_ch(1, 1u, 0u, ftgt, f->vpp_mv, 0);
                f->f_meas_hz = ftgt;
                fra_phase = 1u;
            }
        f->redraw_curve = 1u;              /* 每个点都重画曲线(用户要求: 每点更新一次) */
        }
        return;
    }
    /* fra_phase == 3: 已扫完, 等界面消费 */
}