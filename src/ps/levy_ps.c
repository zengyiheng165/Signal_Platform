#include "levy_ps.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief  配置 Levy HLS IP 的控制地址与 DDR 数据接口。
 * @note   默认读取当前 BSP 的 XPAR_LEVY_MOMENTS_HLS_0_BASEADDR 或兼容宏。
 *         也可通过编译宏 LEVY_PS_IP_BASEADDR 指定地址，须与当前硬件地址分配一致。
 *         本模块只直接访问 AXI-Lite 寄存器，不需要额外建立 DMA 驱动实例。
 */
#ifndef LEVY_PS_IP_BASEADDR
#if defined(XPAR_LEVY_MOMENTS_HLS_0_BASEADDR)
#define LEVY_PS_IP_BASEADDR XPAR_LEVY_MOMENTS_HLS_0_BASEADDR
#elif defined(XPAR_XLEVY_MOMENTS_HLS_0_BASEADDR)
#define LEVY_PS_IP_BASEADDR XPAR_XLEVY_MOMENTS_HLS_0_BASEADDR
#else
#error "Define LEVY_PS_IP_BASEADDR or generate the Levy IP base address in the BSP."
#endif
#endif

#define IP_BASE        ((UINTPTR)LEVY_PS_IP_BASEADDR)
#define REG_CONTROL    0x00u
#define REG_GIE        0x04u
#define REG_IER        0x08u
#define REG_ISR        0x0Cu
#define REG_STATUS     0x10u
#define REG_INPUT_LOW  0x18u
#define REG_INPUT_HIGH 0x1Cu
#define REG_COUNT      0x24u
#define CTRL_START     0x01u
#define CTRL_DONE      0x02u
#define CTRL_IDLE      0x04u
#define CTRL_AUTORESTART 0x80u
#define DDR_BEGIN ((uintptr_t)0x00100000u)
#define DDR_END   ((uintptr_t)0x10000000u)

static const uint32_t moment_registers[LEVY_PS_MOMENTS] = {
    0x2Cu, 0x3Cu, 0x4Cu, 0x5Cu, 0x6Cu, 0x7Cu, 0x8Cu
};
typedef char LevyFloatMustBe32Bits[(sizeof(float) == sizeof(uint32_t)) ? 1 : -1];

/**
 * @brief  让现有 Levy IP 直接读取 DDR，返回七项统计量。
 * @param  samples      DDR 中已整理好的复数频响数组。
 * @param  point_count  实际有效频点数。
 * @param  moments      输出七项 float 统计量。
 * @return LEVY_PS_OK 表示成功，其它值表示地址、IP 或数据错误。
 * @note   通过 AXI-Lite 启动并轮询；数据读取由 IP 的 m_axi 接口完成。
 */
static LevyPsStatus read_moments(const float *samples, uint32_t point_count,
                                 float moments[LEVY_PS_MOMENTS])
{
    const uintptr_t address = (uintptr_t)samples;
    const uint32_t bytes = 3u * point_count * (uint32_t)sizeof(float);
    uint32_t poll, i, control, pending, pl_status;

    /* 检查地址，再确认 IP 空闲；读取控制寄存器会清除上一次完成标志。 */
    if ((address & 31u) != 0u || address < DDR_BEGIN || address >= DDR_END ||
        bytes > DDR_END - address)
        return LEVY_PS_BUFFER_OUT_OF_RANGE;
    control = Xil_In32(IP_BASE + REG_CONTROL);
    if ((control & CTRL_IDLE) == 0u || (control & CTRL_AUTORESTART) != 0u)
        return LEVY_PS_IP_BUSY;

    /* 使用轮询方式。ISR 为写一翻转，只清除当前置位的中断状态。 */
    Xil_Out32(IP_BASE + REG_GIE, 0u);
    Xil_Out32(IP_BASE + REG_IER, 0u);
    pending = Xil_In32(IP_BASE + REG_ISR) & 3u;
    if (pending != 0u) Xil_Out32(IP_BASE + REG_ISR, pending);

    /* PS 写好的频响刷新到 DDR，然后将地址和点数交给 PL。 */
    Xil_DCacheFlushRange((INTPTR)address, bytes);
    Xil_Out32(IP_BASE + REG_INPUT_LOW, (u32)address);
    Xil_Out32(IP_BASE + REG_INPUT_HIGH, 0u); /* 当前 Zynq-7000 使用 32 位 DDR 地址 */
    Xil_Out32(IP_BASE + REG_COUNT, point_count);
    Xil_Out32(IP_BASE + REG_CONTROL, CTRL_START);

    for (poll = 0u; poll < LEVY_PS_DEFAULT_POLL_LIMIT; ++poll)
        if ((Xil_In32(IP_BASE + REG_CONTROL) & CTRL_DONE) != 0u) break;
    if (poll == LEVY_PS_DEFAULT_POLL_LIMIT) return LEVY_PS_TIMEOUT;

    pl_status = Xil_In32(IP_BASE + REG_STATUS);
    if (pl_status == 1u) return LEVY_PS_BAD_ARGUMENT;
    if (pl_status == 2u) return LEVY_PS_INVALID_INPUT;
    if (pl_status == 3u) return LEVY_PS_NUMERIC_OVERFLOW;
    if (pl_status != 0u) return LEVY_PS_IP_ERROR;
    for (i = 0u; i < LEVY_PS_MOMENTS; ++i) {
        const uint32_t bits = Xil_In32(IP_BASE + moment_registers[i]);
        memcpy(&moments[i], &bits, sizeof(bits)); /* 将寄存器位模式解释为 float */
    }
    return LEVY_PS_OK;
}

/* 二、PS 求解：复用现有尺度均衡与带列主元 Householder QR 算法。 */
/**
 * @brief  使用带列主元的 Householder QR 求解 4x4 方程。
 * @param  r         输入矩阵，原地变换为上三角矩阵。
 * @param  rhs       右端项，与矩阵同步变换。
 * @param  solution  输出四个未知量。
 * @return LEVY_PS_OK 表示成功，其它值表示奇异或数值溢出。
 * @note   回代后恢复列交换前的未知量顺序。
 */
static LevyPsStatus qr_solve_4(double r[4][4], double rhs[4],
                               double solution[4]) {
    unsigned permutation[4] = {0, 1, 2, 3};
    double reference_norm = 0.0;
    double permuted_solution[4] = {0};
    unsigned k, i, j;

    for (k = 0; k < 4; ++k) {
        unsigned best_column = k;
        double best_norm = 0.0;

        /* 一、选择剩余部分中范数最大的列，避免对很小的主元求解。 */
        for (j = k; j < 4; ++j) {
            double norm = 0.0;
            for (i = k; i < 4; ++i) norm = hypot(norm, r[i][j]);
            if (norm > best_norm) {
                best_norm = norm;
                best_column = j;
            }
        }
        if (k == 0) reference_norm = best_norm;
        if (!isfinite(best_norm) ||
            !(best_norm > LEVY_PS_RANK_TOL * reference_norm))
            return LEVY_PS_SINGULAR;

        if (best_column != k) {
            unsigned temporary_index = permutation[k];
            permutation[k] = permutation[best_column];
            permutation[best_column] = temporary_index;
            for (i = 0; i < 4; ++i) {
                double temporary = r[i][k];
                r[i][k] = r[i][best_column];
                r[i][best_column] = temporary;
            }
        }

        /* 二、构造反射向量，符号选择用于避免相近数相减。 */
        {
            double v[4] = {0};
            double v_norm = 0.0;
            double dot = 0.0;
            const double diagonal = r[k][k] >= 0.0 ? -best_norm : best_norm;
            for (i = k; i < 4; ++i) v[i] = r[i][k];
            v[k] -= diagonal;
            for (i = k; i < 4; ++i) v_norm = hypot(v_norm, v[i]);
            if (!(v_norm > 0.0) || !isfinite(v_norm))
                return LEVY_PS_SINGULAR;
            for (i = k; i < 4; ++i) v[i] /= v_norm;

            /* 三、反射矩阵和右端项，得到上三角系统。 */
            for (j = k; j < 4; ++j) {
                dot = 0.0;
                for (i = k; i < 4; ++i) dot += v[i] * r[i][j];
                for (i = k; i < 4; ++i) r[i][j] -= 2.0 * v[i] * dot;
            }
            dot = 0.0;
            for (i = k; i < 4; ++i) dot += v[i] * rhs[i];
            for (i = k; i < 4; ++i) rhs[i] -= 2.0 * v[i] * dot;
            r[k][k] = diagonal;
            for (i = k + 1; i < 4; ++i) r[i][k] = 0.0;
        }
    }

    /* 四、上三角回代，然后恢复列交换前的未知量顺序。 */
    {
        int row;
        for (row = 3; row >= 0; --row) {
            double value = rhs[row];
            for (j = (unsigned)row + 1; j < 4; ++j)
                value -= r[row][j] * permuted_solution[j];
            permuted_solution[row] = value / r[row][row];
            if (!isfinite(permuted_solution[row]))
                return LEVY_PS_NUMERIC_OVERFLOW;
        }
    }
    for (i = 0; i < 4; ++i)
        solution[permutation[i]] = permuted_solution[i];
    return LEVY_PS_OK;
}

/**
 * @brief  用七项统计量构造正规方程，求解并恢复四个物理系数。
 * @param  moments      PL 返回的七项统计量。
 * @param  point_count  实际有效频点数。
 * @param  coeff        输出 [b0,b1,a0,a1]。
 * @return LEVY_PS_OK 表示成功，其它值表示统计量无效、奇异或数值溢出。
 * @note   先均衡矩阵尺度，求解后撤销缩放，恢复原始 s 域系数。
 */
static LevyPsStatus solve_moments(const float moments[LEVY_PS_MOMENTS],
                                  uint32_t point_count,
                                  double coeff[LEVY_PS_COEFFS]) {
    double m[7], gram[4][4], rhs[4], scale[4], z[4], result[4];
    unsigned i, j;
    LevyPsStatus status;

    /* 一、检查输入并清空输出，所有失败路径都返回零系数。 */
    if (coeff == NULL) return LEVY_PS_BAD_ARGUMENT;
    for (i = 0; i < 4; ++i) coeff[i] = 0.0;
    if (moments == NULL || point_count == 0 ||
        point_count > LEVY_PS_MAX_POINTS ||
        !(LEVY_PS_RANK_TOL > 0.0 && LEVY_PS_RANK_TOL < 1.0))
        return LEVY_PS_BAD_ARGUMENT;
    for (i = 0; i < 7; ++i) {
        m[i] = (double)moments[i];
        if (!isfinite(m[i])) return LEVY_PS_INVALID_MOMENTS;
    }
    if (m[0] < 0.0 || m[4] < 0.0 || m[5] < 0.0)
        return LEVY_PS_INVALID_MOMENTS;
    if (m[0] == 0.0 || m[4] == 0.0 || m[5] == 0.0)
        return LEVY_PS_SINGULAR;

    /* 二、由七个统计量构造正规方程 G*theta=rhs。
     * theta=[b0/ref², b1/ref, a0/ref², a1/ref]。
     * 注意：G 是正规矩阵，QR 不能消除构造正规方程时条件数平方的影响。 */
    {
        const double g[4][4] = {
            {(double)point_count, 0.0, -m[1],  m[2]},
            {0.0,                m[0], -m[2], -m[3]},
            {-m[1],             -m[2],  m[4],  0.0},
            {m[2],              -m[3],  0.0,   m[5]}
        };
        for (i = 0; i < 4; ++i)
            for (j = 0; j < 4; ++j) gram[i][j] = g[i][j];
    }
    rhs[0] = -m[3];
    rhs[1] = -m[6];
    rhs[2] =  m[5];
    rhs[3] =  0.0;

    /* 三、尺度均衡：C=S^-1*G*S^-1，d=S^-1*rhs，z=S*theta。
     * 四个未知量的量纲不同，先均衡尺度，再判断矩阵是否可可靠求解。 */
    for (i = 0; i < 4; ++i) scale[i] = sqrt(gram[i][i]);
    for (i = 0; i < 4; ++i) {
        rhs[i] /= scale[i];
        for (j = 0; j < 4; ++j)
            gram[i][j] = (gram[i][j] / scale[i]) / scale[j];
    }

    /* 四、QR 求解和回代，得到均衡后的变量 z。 */
    status = qr_solve_4(gram, rhs, z);
    if (status != LEVY_PS_OK) return status;

    /* 五、撤销尺度均衡，再恢复原始 s 域中的四个系数。 */
    for (i = 0; i < 4; ++i) result[i] = z[i] / scale[i];
    result[0] *= LEVY_PS_OMEGA_REF * LEVY_PS_OMEGA_REF;
    result[1] *= LEVY_PS_OMEGA_REF;
    result[2] *= LEVY_PS_OMEGA_REF * LEVY_PS_OMEGA_REF;
    result[3] *= LEVY_PS_OMEGA_REF;
    for (i = 0; i < 4; ++i)
        if (!isfinite(result[i])) return LEVY_PS_NUMERIC_OVERFLOW;
    for (i = 0; i < 4; ++i) coeff[i] = result[i];
    return LEVY_PS_OK;
}

/* 三、主函数只需调用这个入口。 */
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
                           double coeff[LEVY_PS_COEFFS])
{
    float moments[LEVY_PS_MOMENTS];
    LevyPsStatus status;
    unsigned i;

    if (coeff == NULL) return LEVY_PS_BAD_ARGUMENT;
    for (i = 0u; i < LEVY_PS_COEFFS; ++i) coeff[i] = 0.0;
    if (samples == NULL || point_count < 4u || point_count > LEVY_PS_MAX_POINTS ||
        LEVY_PS_DEFAULT_POLL_LIMIT == 0u)
        return LEVY_PS_BAD_ARGUMENT;

    status = read_moments(samples, point_count, moments);
    if (status != LEVY_PS_OK) return status;
    return solve_moments(moments, point_count, coeff);
}
