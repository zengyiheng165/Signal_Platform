#ifndef  _FIR_FILTER_DESIGN_H__
#define  _FIR_FILTER_DESIGN_H__

/* 窗类型 */
#define FIR_WIN_RECT      0
#define FIR_WIN_HANN      1
#define FIR_WIN_HAMMING   2
#define FIR_WIN_BLACKMAN  3



/**
* @brief  设计任意阶 FIR 低通滤波器（加窗 sinc 法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率，单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数 N，允许 1 ~ 4096；系数个数为 N+1
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/fc 非法、win 非法）
*            或内存分配失败
* @note   1) 系数个数为 order+1，h[k] 对应 y[n] = Σ h[k]*x[n-k]，线性相位，
*            群延迟 = order/2 个采样点。
*         2) 采用加窗 sinc：
*              h[k] = sinc( 2*fc/fs * (k - order/2) ) * w[k]
*            最后整体归一化使直流增益 Σh[k] = 1。
*         3) fc 处增益约为 -6dB（幅度 0.5），这是加窗 sinc 的固有特性；
*            通带与阻带以 fc 为中心对称过渡。
*         4) 阻带衰减由窗决定，与阶数无关；过渡带宽度随阶数变窄。实测：
*              窗型        阻带衰减      过渡带宽度(Hz)
*              RECT        ~-21 dB       ~0.9 * fs / N
*              HANN        ~-44 dB       ~2.0 * fs / N
*              HAMMING     ~-53 dB       ~2.1 * fs / N
*              BLACKMAN    ~-74 dB       ~3.1 * fs / N
*            （过渡带宽度指从 -1dB 到 -60dB；实测 fs=1k/fc=100/N=64 时，
*              Rect/Hann/Hamming/Blackman 分别为 9.3/25.8/27.1/39.0 Hz，
*              与上式吻合。）
*            想加深阻带只能换窗型，加大阶数只会让过渡带变陡、阻带不会更深。
*         5) order 为偶数时（长度 N+1 为奇数）在奈奎斯特频率处增益恒为 0，
*            不能用作高通/带阻原型；奇数 order 无此限制。
*/
int fir_lowpass_design(double fs, double fc, int order, int win, double *h);

/**
* @brief  设计任意阶 FIR 高通滤波器（谱反转法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率，单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096，且必须为偶数
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/fc 非法、win 非法）
*            或内存分配失败
* @note   1) 【关键约束】order 必须为偶数（系数个数 order+1 为奇数）。
*            奇数阶 FIR 的 H(Nyquist) 恒等于 0，结构上无法构成高通，
*            因此直接返回 0。
*         2) 采用谱反转法：先写理想低通双边 sinc，再
*              h[k] = delta[k - order/2] - h_lp[k]
*            然后加窗，最后归一化到奈奎斯特频率处增益 = 1。
*            谱反转是精确恒等式，实测奈奎斯特增益为 0.000000 dB。
*         3) fc 处增益约为 -6dB（与低通一致），通带与阻带以 fc 为中心对称
*            过渡。这是窗化 sinc 的固有特性，不是设计误差。
*         4) 直流增益为 0（阻带在低频侧）；阻带衰减由窗决定，与阶数无关：
*              RECT ~-21dB / HANN ~-44dB / HAMMING ~-53dB / BLACKMAN ~-74dB
*            过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*         5) 若阶数过小（过渡带宽度接近或超过 fc），低频侧阻带衰减会明显
*            变差——例如 fs=48kHz、fc=1kHz、N=40 时 dc 仅约 -12dB。
*            此时应增大阶数，或降低 fc。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*/
int fir_highpass_design(double fs, double fc, int order, int win, double *h);

/**
* @brief  设计任意阶 FIR 带通滤波器（sinc×cos 调制法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     下截止频率，单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     上截止频率，单位 Hz，要求 f1 < f2 < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096，且必须为偶数
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/f1/f2 非法、win 非法）
*            或内存分配失败
* @note   1) 【阶数约束】order 必须为偶数。若用奇数阶（Type II），当中心
*            频率恰为 fs/4 时响应会被迫为零，无法设计，故统一要求偶数阶。
*         2) 原理：带通冲激响应 = 低通原型 × 余弦载波
*              h[k] = 2*B/fs * sinc(B/fs*(k-N/2)) * cos(pi*(f1+f2)/fs*(k-N/2))
*            其中 B = f2-f1 为带宽。再乘窗、去均值、并在中心频率归一化。
*         3) 增益：中心频率 f0 = (f1+f2)/2 处精确为 0dB（1.0），f1 与 f2 处
*            约为 -6dB（与低通的 fc 处一致），通带与两侧阻带对称过渡。
*         4) 直流增益与奈奎斯特增益均被压制：去均值后 Sum h 达 1e-17 量级，
*            直流响应精确为 0；奈奎斯特衰减 -40 ~ -127dB（取决于窗与阶数）。
*         5) 阻带衰减由窗决定，与阶数无关：
*              RECT ~-21dB / HANN ~-44dB / HAMMING ~-53dB / BLACKMAN ~-74dB
*            过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*            带宽 B 需明显大于过渡带宽度，否则通带无法成形（见注 7）。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*         7) 阶数太小时通带会塌陷：例如 fs=1kHz、100~200Hz、N=8 时 f1/f2
*            处只有 -2.6dB/-1.7dB（通带没平）。经验上取 N ≥ (5~10)*fs/B。
*/
int fir_bandpass_design(double fs, double f1, double f2, int order,
                        int win, double *h);

/**
* @brief  设计任意阶 FIR 带阻（陷波）滤波器（解析频域积分法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     阻带下边沿，单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     阻带上边沿，单位 Hz，要求 f1 < f2 < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096（奇偶均可）
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/f1/f2 非法、win 非法）
*            或内存分配失败
* @note   1) 原理：直接对理想带阻频率响应（通带 1、阻带 0）做逆 DTFT，
*            闭式解为
*              h[k] = (1/pi) * [ sin(om1*x)/x + (sin(pi*x) - sin(om2*x))/x ]
*            其中 x = k - order/2，om1 = pi*f1/(fs/2)，om2 = pi*f2/(fs/2)；
*            x = 0 处取极限 om1 + (pi - om2)。再乘窗、归一化直流增益为 1。
*         2) 与谱反转/互补结构相比，本法能同时保证：
*              - 直流增益精确 0dB（实测 Sum h = 1.000000000000000）
*              - 奈奎斯特增益精确 0dB
*              - 阻带深度只由窗决定，与低通/高通指标无关
*         3) 增益：f1 与 f2 处约为 -6dB（与低通/带通的截断点一致），
*            阻带中心 (f1+f2)/2 处最深。
*         4) 阻带深度由窗决定：
*              RECT ~-110dB / HANN ~-130dB / HAMMING ~-160dB / BLACKMAN ~-170dB
*            注意这里的深度指"理想阻带零点"附近的实测最小增益；受系数精度
*            影响，实际会显著优于仅由旁瓣决定的 -21/-44/-53/-74dB。
*         5) 过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*            阻带宽度 B = f2-f1 必须明显大于过渡带宽度，否则陷波会被过渡带
*            淹没——实测 fs=1kHz、100~200Hz、N=20 时陷波仅 -16dB（过渡带
*            约 42Hz 已占满半个阻带），而 N≥65 时达到 -131dB。
*            经验取 N ≥ (5~10)*fs/B。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*/
int fir_bandstop_design(double fs, double f1, double f2, int order,
                        int win, double *h);

/**
* @brief  
* @param  a 原始系数 
* @param  na 量化系数 
* @param  n  系数个数，阶数+1 
* @return 
* @note   
*/
void fir_quantize_12bit(double *a,int *na,int n);                        

#endif
