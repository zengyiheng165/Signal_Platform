clc;clear;
F1=1;%频率
Fs=2^12;%采样频率
P1=0;%初始相位
N=2^12;%采样点数
t=0:1/Fs:(N-1)/Fs;%采样时刻
ADC=2^7-1; %直流分量
A=2^7; %信号幅度

s=A*sin(2*pi*F1*t+pi*P1/180)+ADC;

plot(t,s);

fild=fopen('siw_wave_4096x8.coe','wt');

fprintf(fild,'%s\n','MEMORT,MEMORY_INITIALIZATION_RADIX=10;');
fprintf(fild, '%s\n','MEMORY_INITIALIZATION_VECTOR='); 


s0=zeros(1,N);
for n=1:N
    s0(n)=round(s(n));
    if(s0(n)<0)
         s0(n)=0;
    end
    if(n==N)
        fprintf(fild, '%d',s0(n));      %数据写入
        fprintf(fild, '%s',';');        %最后一个数据使用分号结束
    else
         fprintf(fild, '%d',s0(n));      %数据写入
         fprintf(fild, '%s\n',',');      %逗号，换行
    end
end
fclose(fild);
