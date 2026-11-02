#ifndef LEVY_PS_H
#define LEVY_PS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LEVY_PS_MAX_POINTS 2048u /* 容量上限，本次点数通过参数传入 */
#define LEVY_PS_COEFFS 4u
#define LEVY_PS_MOMENTS 7u
#define LEVY_PS_OMEGA_REF 10000.0
#ifndef LEVY_PS_DEFAULT_POLL_LIMIT
#define LEVY_PS_DEFAULT_POLL_LIMIT 1000000u /* 轮询次数，不是微秒 */
#endif
#ifndef LEVY_PS_RANK_TOL
#define LEVY_PS_RANK_TOL 5e-5 /* 保留原 float 统计对应的相对判秩阈值 */
#endif

/**
 * @brief  Levy 拟合返回状态。
 * @note   仅 LEVY_PS_OK 表示四个系数有效，其它情况输出为零。
 */
typedef enum {
    LEVY_PS_OK = 0,
    LEVY_PS_BAD_ARGUMENT,          /* 参数无效 */
    LEVY_PS_BUFFER_OUT_OF_RANGE,   /* DDR 地址或对齐无效 */
    LEVY_PS_IP_BUSY,               /* IP 尚未空闲或启用了自动重启 */
    LEVY_PS_TIMEOUT,               /* 等待 PL 完成超时 */
    LEVY_PS_INVALID_INPUT,         /* PL 检测到无效频响 */
    LEVY_PS_NUMERIC_OVERFLOW,      /* 浮点数值溢出 */
    LEVY_PS_INVALID_MOMENTS,       /* 七项统计量无效 */
    LEVY_PS_SINGULAR,              /* 正规矩阵难以可靠求解 */
    LEVY_PS_IP_ERROR               /* 未知 IP 返回错误 */
} LevyPsStatus;

/**
 * @brief  二阶 Levy 传递函数拟合：调用 HLS 累加，在 PS 求解并返回四个系数。
 * @param  samples      输入频响数组的 DDR 首地址，类型为 const float *。
 *                     每个频点连续存放三个 float，依次为频率 f、H 的实部和虚部。
 * @param  point_count  输入数组中的实际有效频点数，取值为 4..2048。
 *                     它不是每次 ADC 采集的样本数，也不要求固定为 2048。
 * @param  coeff        输出数组，至少四个 double，顺序为 [b0,b1,a0,a1]。
 *                     coeff[0]=b0，coeff[1]=b1，coeff[2]=a0，coeff[3]=a1。
 * @return LEVY_PS_OK 表示拟合成功，coeff 有效；其它 LevyPsStatus 值表示失败。
 *         coeff 指针有效时，失败返回的四个系数为零，不应使用失败结果。
 * @note   传递函数：H(s)=(b0+b1*s)/(s*s+a1*s+a0)，分母 s*s 的系数固定为 1。
 *         输入约定：f 的单位为 Hz，H=Y/X，Y 为被测系统输出，X 为输入。
 *         数组布局：[f0,Re(H0),Im(H0),f1,Re(H1),Im(H1),...]。
 *         使用幅度 A 和相位 phi 时，Re(H)=A*cos(phi)，Im(H)=A*sin(phi)，phi 为弧度。
 *         硬件及初始化：main 复用原平台、DMA、中断、GPIO、DDS、ADC 和时钟初始化；
 *         当前 BSP 须包含匹配的 levy_moments_hls IP 基地址宏，地址由模块自动读取。
 *         本接口不配置采集硬件，不重新初始化 DMA 或中断，也不重复扫频或计算频响。
 *         DDR 要求：samples 按 32 字节对齐，位于 [0x00100000,0x10000000)；
 *         有效长度至少为 3*point_count*sizeof(float) 字节，输入与 coeff 不得重叠。
 *         调用流程：main 初始化 -> 扫频 -> 采集 -> 计算并保存频响 -> 本接口调用一次。
 *         接口内部：检查参数 -> 刷新输入缓存 -> 配置 IP 地址和点数 -> 启动并轮询
 *         -> 读取七项 float 统计量 -> PS 用 double 构造 4x4 方程并做 QR 求解
 *         -> 恢复频率尺度 -> 返回四个连续域系数。
 *         数据接口：HLS 的 m_axi 读取 DDR，AXI-Lite 传递地址、点数、控制和统计结果。
 *         配置项：LEVY_PS_DEFAULT_POLL_LIMIT 为最大轮询次数，不是时间；
 *         LEVY_PS_OMEGA_REF 必须与 HLS 的频率归一化尺度一致，当前为 10000 rad/s。
 *         应串行调用。超时只停止 PS 等待，不停止 PL；输入须保留到 IP 空闲，
 *         空闲前不得释放、改写或复用输入。main 使用静态数组保证其存储期。
 *         coeff 是连续域传递函数系数，用于 IIR IP 前还需进行离散化和量化。
 */
LevyPsStatus levy_ps_fit(const float *samples, uint32_t point_count,
                         double coeff[LEVY_PS_COEFFS]);

#ifdef __cplusplus
}
#endif
#endif
