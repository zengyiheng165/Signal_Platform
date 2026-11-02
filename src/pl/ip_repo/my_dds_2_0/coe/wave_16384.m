clc;clear;
F1=1;%频率
Fs=2^12;%采样频率
P1=0;%初始相位
N=2^12;%采样点数
t=0:1/Fs:(N-1)/Fs;%采样时刻
ADC=2^13-1; %直流分量
A=2^13; %信号幅度
 

fild=fopen('wave_16384x14.coe','wt');

fprintf(fild,'%s\n','MEMORY_INITIALIZATION_RADIX=10;');
fprintf(fild, '%s\n','MEMORY_INITIALIZATION_VECTOR='); 


s0=zeros(1,N);

for m=1:4
    if(m==1)
        s1=A*sin(2*pi*F1*t+pi*P1/180)+ADC;
    elseif(m==2)
        s1=A*square(2*pi*F1*t+pi*P1/180)+ADC;
    elseif(m==3)
         s1=A*sawtooth(2*pi*F1*t+pi*P1/180,0.5)+ADC;
    elseif(m==4)
         s1=A*sawtooth(2*pi*F1*t + pi*P1/180)+ADC; 
    end
    for n=1:N
        s0(n)=round(s1(n));
        if(s0(n)<0)
             s0(n)=0;
        end
        if(n==N && m==4)
            fprintf(fild, '%d',s0(n));      %数据写入
            fprintf(fild, '%s',';');        %最后一个数据使用分号结束
        else
             fprintf(fild, '%d',s0(n));      %数据写入
             fprintf(fild, '%s\n',',');      %逗号，换行
        end
    end
end
fclose(fild);
