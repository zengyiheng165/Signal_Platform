#include "dds_wave.h"
#include "math.h"
#include <stdlib.h>

/* 预览表(②): str_wr_bram() 写 BRAM 时顺手留一份 14 位无符号样本,
   给信号发生器界面画"真正会输出的波形"。g_awg_tab_ok = 1 表示表已有效。 */
u16 g_awg_tab[ch_data_len];
u8  g_awg_tab_ok = 0u;


void str_wr_bram(int* dds_data) 
{
    int i=0,wr_cnt=0;
    for(i = START_ADDR*BRAM_DATA_BYTE;i<(START_ADDR+ch_data_len)*BRAM_DATA_BYTE;i+=4)
    {
        XBram_WriteReg(XPAR_XBRAM_0_BASEADDR,i,dds_data[wr_cnt]);
        if (wr_cnt < ch_data_len) {                 /* ★ 顺手留一份给界面预览(②新增) */
            s32 v = dds_data[wr_cnt];
            if (v < 0) { v = 0; }
            else if (v > 16383) { v = 16383; }
            g_awg_tab[wr_cnt] = (u16)v;
        }
        wr_cnt++;
    }
    g_awg_tab_ok = 1u;
}

void set_AM_wave(double Am,double Ac,double fmfc) //AM调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数
{
    int dds_data[4096];
    double st[4096];
    double mt,ct;
    for(int i=0;i<4096;i++)
    {
        mt=Am*cos((i/4096.0)*2.0*PI);
        ct=Ac*cos((i/4096.0)*2.0*PI*fmfc);
        st[i]=(Ac+mt)*ct;
        dds_data[i]= (int)(st[i]/(Ac+Am)*8191+8192.0);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }     
    str_wr_bram(dds_data); 
}

void set_AMDSB_wave(double Am,double Ac,int fmfc) //DSB调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数
{
    int dds_data[4096];
    double st[4096];
    double mt,ct;
    for(int i=0;i<4096;i++)
    {
        mt=Am*cos((i/4096.0)*2.0*PI);
        ct=Ac*cos((i/4096.0)*2.0*PI*fmfc);
        st[i]=(mt)*ct;
        dds_data[i]= (int)(st[i]/(Ac*Am)*8191+8192.0);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }     
    str_wr_bram(dds_data); 
}

void set_FM_wave(double Am,double Ac,int fmfc,int Kf) //FM调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数,Kf:归一化调频灵敏度
{  
    int dds_data[4096];
    double st;
    double mt;
    double integ_m=0;
    for(int i=0;i<4096;i++)
    {
        mt=Am*cos((i/4096.0)*2.0*PI);
        st=Ac*cos(2.0*PI*fmfc*(i/4096.0)+2.0*PI*Kf*integ_m); 
        integ_m+=(mt/4096.0);
        dds_data[i]=(int)(st/Ac*8191+8192.0);                 
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }
    str_wr_bram(dds_data);
}

void set_PM_wave(double Am,double Ac,int fmfc,double Kp) //PM调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数,Kp:归一化调相灵敏度(峰值相位偏移，单位：周)
{
    int dds_data[4096];
    double st;
    double mt;
    for(int i=0;i<4096;i++)
    {
        mt=Am*cos((i/4096.0)*2.0*PI);
        st=Ac*cos(2.0*PI*fmfc*(i/4096.0)+2.0*PI*Kp*mt);
        dds_data[i]=(int)(st/Ac*8191+8192.0);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }
    str_wr_bram(dds_data);
}

void set_2ASK_wave(double Ac,int fmfc,int bits,int* code) 
{
    int dds_data[4096];
    int per;                           // 每个码元占用的采样点数
    double st;

    if(code==0) return;                
    if(bits<1 || bits>4096) return;
    if((4096%bits)!=0) return;
    per=4096/bits;

    for(int i=0;i<4096;i++)
    {
        int k=i/per;                   // 当前第几个码元(k落在0..bits-1内)
        int b=(code[k]!=0)?1:0;        // 该码元的内容由数组给出

        /* 相位用全局采样点号算，保证码元边界的载波相位连续 */
        double car=Ac*cos(2.0*PI*fmfc*(i/4096.0));
        st=(b==1)?car:0.0;             // 码元为1输出载波，为0输出低电平

        dds_data[i]=(int)(st/Ac*8191+8192.0);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }
    str_wr_bram(dds_data);
}

void set_pulse(float duty) //方波，任意占空比，duty：占空比（数据范围0-1）
{
    int dds_data[4096];
    for(int i=0;i<4096;i++)
    {
        if(i<4096.0*duty){
            dds_data[i]=16383;
        }
        else {
            dds_data[i]=0; 
        }
    }
    str_wr_bram(dds_data); 
}


void set_Sinc_wave(double wide)//Sinc函数，star：周期开始，end：周期结束
{
    int dds_data[4096];
    double distance=(wide+wide)/4096.0;
    double x=-wide;
    for(int i=0;i<4096;i++)
    {
        double s;
        x=-wide+i*distance;  
        /*   */            // 直接算，避免累加漂移
        if(fabs(x)<1e-9){                   // x→0 时 sinc 的极限为 1
           s=1.0;
        }
        else{
            s=sin(x)/x;
        }
        // s=sin(x)/x;
        dds_data[i]=(int)(s*8191+8192);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }
    str_wr_bram(dds_data); 
}

void set_harmonic_wave(int* am,int*pha,int n) //am:存储多次谐波幅度数组，pha：存储多次谐波的相位数组，n:谐波次数
{
    int dds_data[4096];
    double st;
    double amp,phase;
    double peak=0.0;
    double k=1.0;

    /* 参数保护：谐波次数限制在1~4096 */
    if(n<1) n=1;
    if(n>4096) n=4096;

    /* 第一次扫描：求合成波形的峰值，用于防溢出 */
    for(int i=0;i<4096;i++)
    {
        st=0.0;
        for(int h=1;h<=n;h++)
        {
            amp=(double)am[h-1]/4096.0;              //4096对应幅度1(满幅)
            phase=2.0*PI*(double)pha[h-1]/4096.0;    //相位字0~4096对应0~2*PI
            st+=amp*cos(2.0*PI*(double)h*(i/4096.0)+phase);  //第h次谐波(基波h=1)
        }
        if(fabs(st)>peak){
            peak=fabs(st);
        }
    }
    /* 峰值超过1才等比缩放：既保证不削顶，又保持各次谐波相对幅度不变 */
    if(peak>1.0){
        k=1.0/peak;
    }

    /* 第二次扫描：合成并按14位DAC映射 */
    for(int i=0;i<4096;i++)
    {
        st=0.0;
        for(int h=1;h<=n;h++)
        {
            amp=(double)am[h-1]/4096.0;
            phase=2.0*PI*(double)pha[h-1]/4096.0;
            st+=amp*cos(2.0*PI*(double)h*(i/4096.0)+phase);
        }
        st=st*k;

        dds_data[i]=(int)(st*8191+8192.0);
        if(dds_data[i]>16383){
            dds_data[i]=16383;
        }
        else if(dds_data[i]<0){
            dds_data[i]=0;
        }
    }
    str_wr_bram(dds_data);
}




void pl_bram_config(int fre,int pha,int am)
{
    int temp_fre;
    temp_fre=fre=(int)(fre*35.7913941333333333);
    //配置起始地址
    /* ★ 起始地址要写 REG1(slv_reg1 = start_addr), 不能写 REG0!
       HDL 已确认: my_dds_bram_23_v1_0_S00_AXI.v:459-460 -> start_rd = slv_reg0[0], start_addr = slv_reg1;
       原来写 REG0 会先踩掉启动位, 靠下面第 260 行再写 1 才盖回来(所以一直没暴露)。
       注: bram_rd.v:53 标注 start_addr 目前是 legacy unused, 所以本句只是"写对地方", 不影响功能。 */
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG1_OFFSET,START_ADDR*BRAM_DATA_BYTE);
    //配置读长度
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG2_OFFSET,ch_data_len*BRAM_DATA_BYTE);
    //配置频率关键字
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG3_OFFSET,temp_fre);
    //配置幅度关键字 注意缩放本版本调整为0-1024
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG4_OFFSET,am);
    //配置相位关键字
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG5_OFFSET,pha);
    
    //开始读
    MY_DDS_BRAM_23_mWriteReg(XPAR_MY_DDS_BRAM_23_0_BASEADDR,MY_DDS_BRAM_23_S00_AXI_SLV_REG0_OFFSET,1);
}



/**
* @brief  设置普通DDS通道一(DAC1)
* @param  fre     配置频率，信号实际频率，单位Hz 
* @param  pha     配置相位 0-4096 对应0-2pi，为正弦波的相位
* @param  am      配置幅度 0-128
* @param  type    配置波形类型:  二进制0001：正弦波  二进制0010：方波 二进制0100：三角波  二进制1000：锯齿波  
* @return 无
* @note   
*/
void pl_dds1_config(int fre,int pha,int am,int type)
{
    int temp_fre;
    temp_fre=fre=(int)(fre*35.7913941333333333);
    //配置起始地址
    //传入DDS波形类型 _通道一   
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG0_OFFSET,type);

    //传入DDS使能信号_通道一
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG2_OFFSET,1);

    //传入DDS幅度参数_通道一，范围0-128
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG3_OFFSET,am);

    //传入DDS频率关键字_通道一
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG1_OFFSET,temp_fre);

    //传入DDS相位关键字_通道一
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG4_OFFSET,pha);
}

/**
* @brief  设置普通DDS通道二(DAC2)
* @param  fre     配置频率，信号实际频率，单位Hz 
* @param  pha     配置相位 0-4096 对应0-2pi，为正弦波的相位
* @param  am      配置幅度 0-128
* @param  type    配置波形类型:  二进制0001：正弦波  二进制0010：方波 二进制0100：三角波  二进制1000：锯齿波  
* @return 无
* @note   通道二的寄存器在通道一基础上偏移 +20 字节(REG5..REG9)，其余与通道一完全相同
*/
void pl_dds2_config(int fre,int pha,int am,int type)
{
    int temp_fre;
    temp_fre=fre=(int)(fre*35.7913941333333333);
    //配置起始地址
    //传入DDS波形类型 _通道二   
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG5_OFFSET,type);

    //传入DDS使能信号_通道二
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG7_OFFSET,1);

    //传入DDS幅度参数_通道二，范围0-128
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG8_OFFSET,am);

    //传入DDS频率关键字_通道二
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG6_OFFSET,temp_fre);

    //传入DDS相位关键字_通道二
    MY_DDS_mWriteReg(XPAR_MY_DDS_0_BASEADDR,MY_DDS_S0_AXI_SLV_REG9_OFFSET,pha);
}
