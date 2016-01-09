#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <openssl/sha.h>
#include <sys/ioctl.h>

#include "StringObject.h"
#include "SinhronizationObjects.hpp"

#include "MultiChipMainer.hpp"
#include "BitcoinMine.hpp"

int fd_gpio;
volatile uint32_t *gpio;

int SPI_COUNT;
int CHIPS_PER_SPI;

int SPI_CYCLES_PER_TAKT = 6;
int SPI_CYCLES_PER_TAKT_2 = 2;
int SPI_READ_CYCLES = 30;

int SPI_RESET_CYCLES = 6;

/*
RPi GPIO documentation: http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
RPi pin-out: http://elinux.org/RPi_BCM2835_GPIOs
RPi example: http://elinux.org/RPi_Low-level_peripherals
Note: keep in mind that there are 2 versions of pin-outs for different RPi HW versions
*/

// Mapping GPIO (not pin numbers!!!) to SPI: 
//                                           0   1   2   3   4   5   6   7
//unsigned short clk_gpio [MAX_SPI_COUNT] = { 11, 11, 22, 22,  4,  4,  0,  0 };
//unsigned short out_gpios[MAX_SPI_COUNT] = { 10, 25, 27, 15,  8,  23,  0,  0 };
//unsigned short in_gpios [MAX_SPI_COUNT] = {  9, 24, 18, 14,  7,  22,  0,  0 };

// Mapping GPIO (not pin numbers!!!) to SPI: 
//                                           0   1   2   3   4   5   6   7
unsigned short clk_gpio [MAX_SPI_COUNT] = { 11, 11, 22, 22,  4,  4,  0,  0 };
unsigned short out_gpios[MAX_SPI_COUNT] = { 10, 25, 27, 23, 15,  8,  0,  0 };
unsigned short in_gpios [MAX_SPI_COUNT] = {  9, 24, 18, 17, 14,  7,  0,  0 };

unsigned short watch_dog_gpio = 2;
unsigned short power_reset_gpio = 3;

#define OUT_PIN false
#define IN_PIN true

inline int configure_pin(int gpio_number, bool in_out=true)
{
    // All pins should be configured as IN at the begin!!!
    INP_GPIO(gpio_number);
    
    if (in_out == OUT_PIN) {
      OUT_GPIO(gpio_number);
    }
    
    return 1;
}

cMultyBufferSPIExchange::cMultyBufferSPIExchange()
{

}
bool cMultyBufferSPIExchange::ConfigClkPorts()
{
    for(uint8_t i=0; i<SPI_COUNT; i++)
    {
        configure_pin(clk_gpio[i], OUT_PIN);
    }
    
    Clk.PinSetup(SPI_COUNT, clk_gpio);
    return true;
}
bool cMultyBufferSPIExchange::ConfigOutPorts()
{
    uint8_t i;
    for(i=0;i<SPI_COUNT;i++)
    {
        configure_pin(out_gpios[i], OUT_PIN);
        Out[i].PinSetup(out_gpios[i]);
    }
    return true;
}
bool cMultyBufferSPIExchange::ConfigInPorts()
{
    uint8_t i;
    for(i=0;i<SPI_COUNT;i++)
    {
        configure_pin(in_gpios[i], IN_PIN);
        In[i].PinSetup(in_gpios[i]);
    }
    return true;
}
bool cMultyBufferSPIExchange::ConfigControlPorts()
{
	configure_pin(watch_dog_gpio, OUT_PIN);
	pinWatchDog.PinSetup(watch_dog_gpio);
	
	configure_pin(power_reset_gpio, OUT_PIN);
	pinPowerReset.PinSetup(power_reset_gpio);
}
bool cMultyBufferSPIExchange::Init()
{
	fd_gpio = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd_gpio < 0)
	{
		printf("Could not open memory\n");
		return false;
	}

	gpio = (uint32_t*) mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd_gpio, 0x20200000);
	if (gpio == MAP_FAILED)
	{
		printf("Gpio Mapping failed\n");
		close(fd_gpio);
		return false;
	}

	if(!ConfigClkPorts()) return false;
	if(!ConfigOutPorts()) return false;
	if(!ConfigInPorts()) return false;
	if(!ConfigControlPorts()) return false;

	return true;
}

cUSBComChipExchange::cUSBComChipExchange()
{
    fd=-1;
    TimeOuts=LenError=FrameError=0;
}
cUSBComChipExchange::~cUSBComChipExchange()
{
    CloseFD();
}
void cUSBComChipExchange::CloseFD()
{
    if(fd>0)
    {
        close(fd);
        fd=0;
    }
}
bool cUSBComChipExchange::Init(const char *file_name)
{
    CloseFD();
    fd=open("/dev/ttyACM0",O_RDWR);
    if(fd<=0)
    {
        return false;
    }
    return true;
}
bool cUSBComChipExchange::Ping()
{
    if(!IsConnectionOk())
        return false;
    uint8_t ping[5];
    ping[0]=1;
    ping[1]=2;
    ping[2]=0;
    ping[3]=0xAA;
    ping[4]=0xBB;
    ssize_t res=write(fd,ping,5);
    if(res!=5)
    {
        CloseFD();
        return false;
    }
    pollfd p[1];
    p[0].fd=fd;
    p[0].events=POLLIN;
    int poll_res;
    while((poll_res=poll(p,1,150))<0){ }
    if(p[0].revents&POLLIN)
    {
        uint8_t ping_res[40];
        res=read(fd,ping_res,40);
        if(res<=0)
        {
            CloseFD();
            return false;
        }
        if(res!=5 || memcmp(ping_res,ping,5)!=0)
        {
            printf("Error frame structure ! res=%zd,ping[0]=%u\n"
                   ,res,ping_res[0]);
            FrameError++;
            return false;
        }
    }else
    {
        TimeOuts++;
        return false;
    }
    return true;
}
uint16_t cUSBComChipExchange::TxRx(uint32_t spi_mask,uint8_t *InBuff[],uint8_t *OutBuff[],uint16_t Len)
{
    // Implement SPI masking by spi_mask
    if(!IsConnectionOk())
        return 0;
    uint8_t ping[2048*8],i;
    uint16_t TotalLen=Len*SPI_COUNT,TempLen;
    ping[0]=2;
    ping[1]=TotalLen&0xFF;
    ping[2]=(TotalLen>>8)&0xFF;
    for(TempLen=0;TempLen<Len;TempLen++)
    {
        for(i=0;i<SPI_COUNT;i++)
        {
           ping[3+i+TempLen*SPI_COUNT]=InBuff[i][TempLen];
        }
    }
    TotalLen+=3;
    ssize_t res=write(fd,ping,TotalLen);
    if(res!=TotalLen)
    {
        CloseFD();
        return 0;
    }
    pollfd p[1];
    p[0].fd=fd;
    p[0].events=POLLIN;
    int poll_res;
    uint16_t LeftLen=3;
    uint16_t RecvPos=0;
    while(RecvPos<TotalLen+3)
    {
        while((poll_res=poll(p,1,150+LeftLen*5))<0){ }
        if(poll_res>0 && p[0].revents&POLLIN)
        {
            res=read(fd,&ping[RecvPos],2048*8);
            if(res<=0)
            {
                CloseFD();
                return false;
            }
            RecvPos+=res;
            if(RecvPos<3)
                continue;
            if(ping[0]==2)
            {
                TotalLen=ping[1]|(ping[2]<<8);
                if(TotalLen+3>RecvPos)
                    LeftLen=TotalLen+3-RecvPos;
                else
                    LeftLen=0;
                if(LeftLen>0)
                    continue;
            }
            if(RecvPos<5 || ping[0]!=2 || TotalLen+3!=RecvPos)
            {
                printf("Error frame structure ! res=%u,ping[0]=%u,TotalLen=%u (must %u)\n"
                       ,RecvPos,ping[0],TotalLen+3,RecvPos);
                FrameError++;
                return false;
            }
            if(TotalLen==Len*SPI_COUNT)
            {
                for(TempLen=0;TempLen<Len;TempLen++)
                {
                    for(i=0;i<SPI_COUNT;i++)
                    {
                        OutBuff[i][TempLen]=ping[3+i+TempLen*SPI_COUNT];
                    }
                }
            }
        }else
        {
            printf("Timeout!\n");
            TimeOuts++;
            return false;
        }
    }
    return true;
}
uint16_t cUSBComChipExchange::TxRx(uint16_t spi_number,const uint8_t *InBuff,uint8_t *OutBuff,uint16_t Len)
{
    if(!IsConnectionOk())
        return 0;
    uint8_t ping[2048];
    uint16_t TotalLen=Len+1;
    ping[0]=3;
    ping[1]=TotalLen&0xFF;
    ping[2]=(TotalLen>>8)&0xFF;
    ping[3]=spi_number;
    memcpy(&ping[4],InBuff,Len);
    TotalLen+=3;
    ssize_t res=write(fd,ping,TotalLen);
    if(res!=TotalLen)
    {
        CloseFD();
        return 0;
    }
    pollfd p[1];
    p[0].fd=fd;
    p[0].events=POLLIN;
    int poll_res;
    uint16_t LeftLen=TotalLen;
    uint16_t RecvPos=0;
    while(RecvPos<TotalLen+3)
    {
        while((poll_res=poll(p,1,150+LeftLen*5))<0){ }
        if(poll_res>0 && p[0].revents&POLLIN)
        {
            res=read(fd,&ping[RecvPos],2048-RecvPos);
            if(res<=0)
            {
                CloseFD();
                return false;
            }
            RecvPos+=res;
            if(RecvPos<4)
                continue;
            TotalLen=ping[1]|(ping[2]<<8);
            if(TotalLen+3>RecvPos)
                LeftLen=TotalLen+3-RecvPos;
            else
                LeftLen=0;
            if(LeftLen>0)
                continue;
            if(RecvPos<6
               || ping[0]!=3
               || ping[3]!=spi_number
               || TotalLen+3!=RecvPos)
            {

                printf("Error frame structure ! res=%u,ping[0]=%u,chip=%u(must %u),TotalLen=%u (must %u)\n"
                       ,RecvPos,ping[0],ping[3],spi_number,TotalLen+3,RecvPos);
                FrameError++;
                return false;
            }
            memcpy(OutBuff,&ping[4],TotalLen-1);
        }else
        {
            TimeOuts++;
            return false;
        }
    }
    return true;
}

uint16_t cMultyBufferSPIExchange::TxRx(uint16_t spi_number,const uint8_t *InBuff,uint8_t *OutBuff,uint16_t Len)
{
    ResetLine(spi_number);
    uint16_t Pos=0;
    while (Pos<Len)
    {
        SendRecv(spi_number,InBuff[Pos],OutBuff[Pos]);
        Pos++;
    }
    return Len;
}
uint16_t cMultyBufferSPIExchange::TxRx(uint32_t spi_mask,uint8_t *InBuff[],uint8_t *OutBuff[],uint16_t Len)
{
    /*printf("TxRx on all SPI, len %d, spi_mask %x08x\n", Len, spi_mask);
    for(uint8_t ii=0;ii<SPI_COUNT;ii++) if (spi_mask & (1 << ii)) {
      printf("OUT spi %d:", ii);
      for (uint8_t jj=0;jj<8;jj++) printf(" 0x%02x", InBuff[ii][jj]);
      printf("\n");
    }*/
    
    ResetLine();
    uint8_t In[MAX_SPI_COUNT];
    uint8_t Out[MAX_SPI_COUNT];
    uint16_t Pos=0;
    uint8_t i;
    while (Pos<Len)
    {
        for(i=0;i<SPI_COUNT;i++) if (spi_mask & (1 << i))
            In[i]=InBuff[i][Pos];
        SendRecv(spi_mask,In,Out);
        for(i=0;i<SPI_COUNT;i++) if (spi_mask & (1 << i))
            OutBuff[i][Pos]=Out[i];

        Pos++;
    }
    /*for(uint8_t ii=0;ii<SPI_COUNT;ii++) if (spi_mask & (1 << ii)) {
      printf("IN  spi %d:", ii);
      for (uint8_t jj=0;jj<8;jj++) printf(" 0x%02x", OutBuff[ii][jj]);
      printf("  int[17]:");
      for (uint8_t jj=0;jj<4;jj++) printf(" 0x%02x", OutBuff[ii][17*4+jj]);
      printf("\n");
    }*/
    return Len;
}

void cMultyBufferSPIExchange::doWatchDog()
{
	static const int timeInterval = 1; // watch dog interval in seconds
	static time_t lastTime = time(NULL);
	
	time_t now = time(NULL);
	if (now < lastTime + timeInterval) return;
	lastTime = now;
	
	static int pinLevel = 0;
	pinLevel = (pinLevel+1) % 2;
	
	//printf("WATCH DOG: changing level to %d\n", pinLevel);
	
	if (pinLevel == 0)
		pinWatchDog.Clean();
	else
		pinWatchDog.Set();
}

void cMultyBufferSPIExchange::doPowerReset(int powerResetCycles)
{
	printf("POWER RESET: sending power reset request\n");
	static int pinLevel = 0;
	for (uint32_t i=0; i < powerResetCycles; i++)
	{
		pinLevel = (pinLevel+1) % 2;
		
		if (pinLevel == 0)
			pinPowerReset.Clean();
		else
			pinPowerReset.Set();
		
		usleep(1000 * 250); // mili-seconds
	}
}

cChipCommandBuffer::cChipCommandBuffer(uint16_t DefBuffSize)
:BufferSize(DefBuffSize),Buffer((uint8_t*)malloc(BufferSize)),BufferPos(0)
{
    memset(Buffer, 0, BufferSize);
}
cChipCommandBuffer::~cChipCommandBuffer()
{
    if(Buffer)
    {
        free(Buffer);
        Buffer=nullptr;
        BufferSize=0;
    }
    BufferPos=0;
}
void cChipCommandBuffer::EmitData(uint16_t addr, const uint8_t *buf, uint16_t len)
{
        uint8_t otmp[3];
        if (len < 4 || len > 128) {
			printf("ERROR: can not send more then 128 bytes in one frame, len = %d.\n", len);
            return; // This cannot be programmed in single frame!
		}
        len /= 4; // Strip
        otmp[0] = (len - 1) | 0xE0;
        otmp[1] = (addr >> 8)&0xFF;
        otmp[2] = addr & 0xFF;
        EmitBuff(otmp, 3);
        EmitBuffReverse(buf, len*4);
}
void cChipCommandBuffer::EmitBuff(const uint8_t *str, uint16_t Size)
{
    if (BufferPos + Size >= BufferSize)
        return;
    memcpy(&Buffer[BufferPos], str, Size);
    BufferPos += Size;
}
void cChipCommandBuffer::EmitBuffReverse(const uint8_t *str, uint16_t Size)
{
    {
        uint16_t i;
        if (BufferPos + Size >= BufferSize) return;
        for (i = 0; i < Size; i++)
        { // Reverse bit order in each byte!
                uint8_t p = str[i];
                p = ((p & 0xaa)>>1) | ((p & 0x55) << 1);
                p = ((p & 0xcc)>>2) | ((p & 0x33) << 2);
                p = ((p & 0xf0)>>4) | ((p & 0x0f) << 4);
                Buffer[BufferPos+i] = p;
        }
        BufferPos += Size;
    }
}


cBitfuryChip::cBitfuryChip()
:Enabled(false),SpiId(0),SpiSeqId(0),FreqCode(1),CurrentJob(nullptr),PrevJob(nullptr)
{
    DividerOn=true;FastOscOn=false;
    WorkManager=nullptr;
    FoundSolutions=0;
    FoundSolutionsByDifficulty=0;
    ChipReads=LastChipReads=0;
    FoundErrors=0;
    StartWorkTime=0;
    OutChipInfo=true;
    OutChipInfoOnce=false;
    JobsDone=0;
    JobsWithoutSolutions=0;
}
cBitfuryChip::~cBitfuryChip()
{
    if(CurrentJob)
    {
        CurrentJob->OnJobCanceled();
        CurrentJob=nullptr;
    }
    if(PrevJob)
    {
        PrevJob->OnJobCanceled();
        PrevJob=nullptr;
    }
}
void cBitfuryChip::OnNewResultFromChip()
{
    uint32_t newbuf[17];
    uint8_t i;
    memcpy(newbuf, ResultBuffer.GetBuffer()+4+SpiSeqId, 17*4);
	
	//printf("chip: %d: ", SpiSeqId);
	//for (int i=0; i<17; i++) printf("0x%08x ", newbuf[i]);
	//printf("\n");
	
    ChipReads++;
    bool NeedOutPut=OutChipInfo||OutChipInfoOnce;
    //printf("%d: OutChipInfo = %d, OutChipInfoOnce = %d\n", SpiSeqId, OutChipInfo, OutChipInfoOnce);
    if(newbuf[16]!=PrevJobResults[16])//job switched
    {
        //printf("JOB SWITCHED: spi %d seq %d\n", SpiId, SpiSeqId);
        uint64_t NowTime;
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC,&ts);
        NowTime=ts.tv_sec*1000+ts.tv_nsec/1000000;
        LastJobTakeTime=NowTime-LastJobTime;
        LastJobTime=NowTime;
        JobsWithoutSolutions++;
        
        cStringType Str1,Str2;
        if(NeedOutPut)
        {
            Str1.PrepareSize(128);
            Str2.PrepareSize(64);
            Str1.printf("spi %u seq %u JB-%u %08x %08x:",SpiId,SpiSeqId,newbuf[16]&1, PrevJob?SwapEndian(((uint32_t*)PrevJob->GetMidState())[0]):0,PrevJob?SwapEndian(((uint32_t*)PrevJob->GetLeftData())[1]):0);
            Str2="";
        }
        uint32_t NowSolutions=0;
        if(PrevJob)
        {
            uint32_t FoundErrorsValues[16];
            uint8_t FoundErrorsCount=0;
            uint32_t now_errors=0;
            for (i = 0; i < 16; i++)
            {
				if(PrevJobResults[i] != newbuf[i])
				{
					uint32_t non;
					uint16_t pos;
					uint32_t Diff=0;
					uint32_t res = fix_nonce(newbuf[i], (uint32_t*)PrevJob->GetMidState(), (uint32_t*)PrevJob->GetLeftData(),non,pos,Diff);
					if(res)
                    {
                        if(PrevJob->CheckIsNonceFound(non))
                            continue;
                        if(NeedOutPut)
                        {
                            if(Str2.IsEmpty())
                                Str2.add_printf("%08x(%u)[%u]",non,pos,Diff);
                            else
                                Str2.add_printf(",%08x(%u)[%u]",non,pos,Diff);
                        }
						if(PrevJob->GetCurrentDifficulty()<=Diff)
						{
						    FoundSolutionsByDifficulty+=PrevJob->GetCurrentDifficulty();
							PrevJob->SetFoundNonce(non);
							Str2+="+";
						}
						FoundSolutions++;
						NowSolutions++;
            JobsWithoutSolutions = 0;
						//PrevSolution[PrevSolutionCount++]=non;
					}else if(CurrentJob)
                    {
                        res = fix_nonce(newbuf[i], (uint32_t*)CurrentJob->GetMidState(), (uint32_t*)CurrentJob->GetLeftData(),non,pos,Diff);
                        if(res)
                        {
                            if(CurrentJob->CheckIsNonceFound(non))
                                continue;
                            if(NeedOutPut)
                            {
                                if(Str2.IsEmpty())
                                    Str2.add_printf("!%08x(%u)[%u]",non,pos,Diff);
                                else
                                    Str2.add_printf(",!%08x(%u)[%u]",non,pos,Diff);
                            }
                            if(CurrentJob->GetCurrentDifficulty()<=Diff)
                            {
                                FoundSolutionsByDifficulty+=CurrentJob->GetCurrentDifficulty();
                                CurrentJob->SetFoundNonce(non);
                                Str2+="+";
                            }
                            NowSolutions++;
                            FoundSolutions++;
                            JobsWithoutSolutions = 0;
                        }else
                        {
                            non=newbuf[i];
                            for(uint8_t i1=0;i1<FoundErrorsCount;i1++)
                                if(FoundErrorsValues[i1]==non)
                                    continue;
                            now_errors++;
                            FoundErrorsValues[FoundErrorsCount++]=non;
                        }
                    }
				}
			}
			if(now_errors>1)
            {
                FoundErrors+=(now_errors-1);
                //printf("ERRORs spi %d seq %d error %u\n",SpiId,SpiSeqId,now_errors-1);
                if(NeedOutPut)
                {
                    if(Str2.IsEmpty())
                        Str2.add_printf("E%u",now_errors-1);
                    else
                        Str2.add_printf(",E%u",now_errors-1);
                }
            }
        }else
        {
            if(NeedOutPut)
                Str2="NO WORK!";
		}
		if(NeedOutPut)
            Str1.add_printf("S:%u R:%u S:%u/%u ",NowSolutions,ChipReads,FoundSolutions,FoundErrors);
		if(StartWorkTime!=0)
		{
			time_t now=time(NULL);
			if(NeedOutPut)
            {
                if(now>StartWorkTime)
                    Str1.add_printf("S:%.03f/m(%.2fGH/s) E:%.06f/m ",(double)FoundSolutions*60/(now-StartWorkTime),(double)FoundSolutions*60/(now-StartWorkTime)/60*4.295,(double)FoundErrors*60/(now-StartWorkTime));
            }
		}else
		{
		    StartWorkTime=time(NULL);
		}
		memcpy(PrevJobResults,newbuf,17*4);
		if(PrevJob)
		{
			PrevJob->OnJobFinished();
			PrevJob=nullptr;
		}
		PrevJob=CurrentJob;
		CurrentJob=nullptr;
		if(CurrentJob)
		{
			CurrentJob=CurrentJob->GenerateFromSelfNextJob();
		}
		if(!CurrentJob)
			CurrentJob=WorkManager->GetNextJob();
		if(!CurrentJob)
		{
		    if(NeedOutPut)
                Str1+=("(Error get job)");
		}
		else
		{
		    if(NeedOutPut)
                Str1.add_printf("JL:%u",WorkManager->GetJobCount());
			memcpy(&NextJobBuffer[0],CurrentJob->GetMidState(),32);
			//SwapDWORDEndians(&atrvec[0],32/4);
			memcpy(&NextJobBuffer[16],CurrentJob->GetLeftData(),16);
			//SwapDWORDEndians(&atrvec[16],16/4);
			ms3_compute(&NextJobBuffer[0]);
		}
		if(!Str2.IsEmpty() && NeedOutPut)
		{
			Str1+="[";
			Str1+=Str2;
			Str1+="]";
		}
		if(NeedOutPut)
        {
            printf("%s\n",Str1.GetText());
            OutChipInfoOnce=false;
        }
        LastChipReads=ChipReads;
        ChipReads=0;
        JobsDone++;
    }
}
void cBitfuryChip::PrintChipInfo()
{
    time_t now=time(NULL);
    printf("CHIP: %u/%u = R:%u S:%u/%u ",SpiId,SpiSeqId,LastChipReads,FoundSolutions,FoundErrors);
    printf("S:%.03f/m(%.2fGH/s) E:%.06f/m W:%.2fGH/s (%u)[%llu]\n"
           ,(double)FoundSolutions*60/(now-StartWorkTime)
           ,(double)FoundSolutions*60/(now-StartWorkTime)/60*4.295
           ,(double)FoundErrors*60/(now-StartWorkTime)
           ,(double)JobsDone*3.17089382326/(now-StartWorkTime)
           ,JobsDone
           ,LastJobTakeTime);
}
void cBitfuryChip::FillFreqValueBuffer(uint8_t *Obj)
{
    unsigned char i;
    if(FreqCode>=64)
    {
        memset(Obj,0xFF,8);
        return;
    }
    memset(Obj,0,8);
    if(FreqCode==0)
        return;
    for(i=0;i<FreqCode;i++)
    {
        Obj[i/8]|=(1<<(i%8));
    }
}
bool cBitfuryChip::SendNextWorkBuffer()
{
    CommandBuffer.Clear();
    CommandBuffer.EmitBreak();
    CommandBuffer.EmitFAsync(SpiSeqId);
    CommandBuffer.EmitData(0x3000,(uint8_t*)NextJobBuffer,19*4);
    return true;
}


// 0 .... 31 bit
// 1000 0011 0101 0110 1001 1010 1100 0111

// 1100 0001 0110 1010 0101 1001 1110 0011
// C16A59E3

uint8_t enaconf[4] = { 0xc1, 0x6a, 0x59, 0xe3 };
uint8_t disconf[4] = { 0, 0, 0, 0 };

void cBitfuryChip::config_reg(int cfgreg, int ena)
{
        if (ena) CommandBuffer.EmitData(0x7000+cfgreg*32, enaconf, 4);
        else     CommandBuffer.EmitData(0x7000+cfgreg*32, disconf, 4);
}

#define FIRST_BASE 61
#define SECOND_BASE 4
unsigned char counters[16] = { 64, 64,
        SECOND_BASE, SECOND_BASE+4, SECOND_BASE+2, SECOND_BASE+2+16, SECOND_BASE, SECOND_BASE+1,
        (FIRST_BASE)%65,  (FIRST_BASE+1)%65,  (FIRST_BASE+3)%65, (FIRST_BASE+3+16)%65, (FIRST_BASE+4)%65, (FIRST_BASE+4+4)%65, (FIRST_BASE+3+3)%65, (FIRST_BASE+3+1+3)%65};

void cBitfuryChip::SendOSCValue()
{
    CommandBuffer.Clear();
    CommandBuffer.EmitBreak();
    CommandBuffer.EmitFAsync(SpiSeqId);
    uint8_t oscValue[8];
    FillFreqValueBuffer(oscValue);
    CommandBuffer.EmitData(0x6000, oscValue, 8);
}
bool cBitfuryChip::InitChip()
{
    CommandBuffer.Clear();
    CommandBuffer.EmitBreak();
    CommandBuffer.EmitFAsync(SpiSeqId);
    uint8_t oscValue[8];
    FillFreqValueBuffer(oscValue);

    CommandBuffer.EmitData(0x6000, oscValue, 8); // Program internal on-die slow oscillator frequency

    // Shut off scan chain COMPLETELY, all 5 should be disabled, but it is unfortunately that any of them will be PROGRAMMED anyway by default
    config_reg(7,0); config_reg(8,0); config_reg(9,0); config_reg(10,0); config_reg(11,0);
    // Propagate output to OUTCLK from internal clock to see clock generator performance, it can be measured on OUTCLK pin, and
    // different clock programming methods (i.e. slow internal oscillator, fast internal oscillator, external oscillator, with clock
    // divider turned on or off) can be verified and compared indirectly by using same output buffer. Signal to OUTCLK is taken from rather
    // representative internal node, that is used to feed rounds (close to worst-case).
    config_reg(6,1);

    // Enable slow internal osciallators, as we want to get stable clock. Fast oscillator should be used only for faster clocks (like 400 Mhz and more
    // or divided), and likely would work bad on slower clocks. Slow internal osciallator likely should be more stable and work at clocks below that */
    config_reg(4,1); // Enable slow oscillator

    // Configuring other clock distribution path (i.e. disabling debug step clock, disabling fast oscillator, disabling flip-flop dividing clock) */
    config_reg(1,0); config_reg(2,FastOscOn?1:0); config_reg(3,DividerOn?0:1);

    CommandBuffer.EmitData(0x0100, counters, 16); // Program counters correctly for rounds processing, here baby should start consuming power

    // Prepare internal buffers */
    // PREPARE BUFFERS (INITIAL PROGRAMMING) */
    uint32_t w[16];

    memset(&w, 0, sizeof(w)); w[3] = 0xffffffff; w[4] = 0x80000000; w[15] = 0x00000280;

    // During loads of jobs into chip we load only parts of data that changed, while don't load padding data. Padding data is loaded
    // only once during initialization. Padding data is fixed in SHA256 for fixed-size inputs. For more information looks for SHA256 padding section.
    CommandBuffer.EmitData(0x1000, (uint8_t*)w, 16*4);
    CommandBuffer.EmitData(0x1400, (uint8_t*)w,  8*4);
    memset(w, 0, sizeof(w)); w[0] = 0x80000000; w[7] = 0x100;
    CommandBuffer.EmitData(0x1900, (uint8_t*)&w[0],8*4); // Prepare MS and W buffers!

    /*
    uint32_t atrvec[] = {
        0xb0e72d8e, 0x1dc5b862, 0xe9e7c4a6, 0x3050f1f5, 0x8a1a6b7e, 0x7ec384e8, 0x42c1c3fc, 0x8ed158a1, // MIDSTATE
        0,0,0,0,0,0,0,0,
        0x8a0bb7b7, 0x33af304f, 0x0b290c1a, 0xf0c4e61f}; // WDATA: hashMerleRoot[7], nTime, nBits, nNonce

    memcpy(NextJobBuffer,atrvec,19*4);*/

    ms3_compute(NextJobBuffer);

    CommandBuffer.EmitData(0x3000, (uint8_t*)NextJobBuffer, 19*4);

    return true;
}


#define rotrFixed(x,y) (((x) >> (y)) | ((x) << (32-(y))))
#define s0(x) (rotrFixed(x,7)^rotrFixed(x,18)^(x>>3))
#define s1(x) (rotrFixed(x,17)^rotrFixed(x,19)^(x>>10))
#define Ch(x,y,z) (z^(x&(y^z)))
#define Maj(x,y,z) (y^((x^y)&(y^z)))
#define S0(x) (rotrFixed(x,2)^rotrFixed(x,13)^rotrFixed(x,22))
#define S1(x) (rotrFixed(x,6)^rotrFixed(x,11)^rotrFixed(x,25))

/* SHA256 CONSTANTS */
const uint32_t SHA_K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

const uint32_t sha_initial_state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

void cBitfuryChip::ms3_compute(uint32_t *p)
{
        uint32_t a,b,c,d,e,f,g,h, ne, na,  i;

        a = p[0]; b = p[1]; c = p[2]; d = p[3]; e = p[4]; f = p[5]; g = p[6]; h = p[7];

        for (i = 0; i < 3; i++) {
                ne = p[i+16] + SHA_K[i] + h + Ch(e,f,g) + S1(e) + d;
                na = p[i+16] + SHA_K[i] + h + Ch(e,f,g) + S1(e) + S0(a) + Maj(a,b,c);
                d = c; c = b; b = a; a = na;
                h = g; g = f; f = e; e = ne;
        }

        p[15] = a; p[14] = b; p[13] = c; p[12] = d; p[11] = e; p[10] = f; p[9] = g; p[8] = h;
}

/* Nonce decoding */
uint32_t cBitfuryChip::decrypt_nonce(uint32_t in)
{
        uint32_t out;

        // First part load (hight part of core number 10 bites)
        out = (in & 0xFF) << 24; in >>= 8;

        // Byte reversal (nonce counter 22 bits in reverse order_
        in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
        in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
        in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

        out |= (in >> 2)&0x3FFFFF; //save nonce counter value

        // Extraction (lower 2 bits of core number)
        if (in & 1) out |= (1 << 23);
        if (in & 2) out |= (1 << 22);

        out -= 0x800004;
        //up 10 bits core number
        //lower 22 bit nonce counter
        return out;
}

bool cBitfuryChip::test_nonce(uint32_t tnon, uint32_t *ms, uint32_t *w,uint32_t &diff)
{
        uint32_t dd[16];
        //uint32_t st[8];
        uint32_t ho[8];
        //int i;

        memset(dd, 0, sizeof(dd));
        dd[0] = w[0]; dd[1] = w[1]; dd[2] = w[2]; dd[3] = tnon; dd[4] = 0x80000000; dd[15] = 0x280;
//      for (i = 0; i < 8; i++) st[i] = ms[7-i];
        SHA256_Full(ho, dd, ms);
        memset(dd, 0, sizeof(dd));
        memcpy(dd, ho, 4*8);
        dd[8] = 0x80000000; dd[15] = 0x100;
        SHA256_Full(ho, dd, sha_initial_state);

        if (ho[7] == 0)
        {
            unsigned val=SwapEndian(ho[6]);//((ho[6]&0xFF)<<8)|((ho[6]&0xFF00)>>8);
            if(ho[6]==0)
            {
                diff=0xFFFFFFFF;
            }else
            {
                diff=0xFFFFFFFF/(val);
                //printf("%08X %u\n",diff,diff);
            }
            return true;
        }

        return false;
}

uint32_t cBitfuryChip::fix_nonce(uint32_t innonce, uint32_t *ms, uint32_t *w,uint32_t &res_non,uint16_t &KeyPos,uint32_t &Diff)
{
        uint32_t tnon;
        uint32_t non = decrypt_nonce(innonce);
        res_non=non;
        KeyPos=0;

        //printf(" fix=%08X ");

        tnon = non;           if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=0;return 1;}
        tnon = non-0x400000;  if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=1;return 1;}
        tnon = non-0x800000;  if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=2;return 1;}
        tnon = non+0x2800000; if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=3;return 1;}
        tnon = non+0x2C00000; if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=4;return 1;}
        tnon = non+0x400000;  if (test_nonce(tnon,ms,w,Diff)) {res_non=tnon;KeyPos=5;return 1;}

        /*
        tnon = non-1;         if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=6;return 1;}
        tnon = non-0x400001;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=7;return 1;}
        tnon = non-0x800001;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=8;return 1;}
        tnon = non+0x2800001; if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=9;return 1;}
        tnon = non+0x2C00001; if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=10;return 1;}
        tnon = non+0x400001;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=11;return 1;}

        tnon = non-2;         if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=12;return 1;}
        tnon = non-0x400002;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=13;return 1;}
        tnon = non-0x800002;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=14;return 1;}
        tnon = non+0x2800002; if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=15;return 1;}
        tnon = non+0x2C00002; if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=16;return 1;}
        tnon = non+0x400002;  if (test_nonce(tnon,ms,w)) {res_non=tnon;KeyPos=17;return 1;}
        */
        return 0;
}

#define blk0(i) (W[i] = data[i])
#define blk2(i) (W[i&15]+=s1(W[(i-2)&15])+W[(i-7)&15]+s0(W[(i-15)&15]))

#define a(i) T[(0-i)&7]
#define b(i) T[(1-i)&7]
#define c(i) T[(2-i)&7]
#define d(i) T[(3-i)&7]
#define e(i) T[(4-i)&7]
#define f(i) T[(5-i)&7]
#define g(i) T[(6-i)&7]
#define h(i) T[(7-i)&7]

#define R(i) h(i)+=S1(e(i))+Ch(e(i),f(i),g(i))+SHA_K[i+j]+(j?blk2(i):blk0(i));\
        d(i)+=h(i);h(i)+=S0(a(i))+Maj(a(i),b(i),c(i))

void SHA256_Full(uint32_t *state, uint32_t *data, const uint32_t *st)
{
        uint32_t W[16];
        uint32_t T[8];
        uint32_t j;

        T[0] = state[0] = st[0]; T[1] = state[1] = st[1]; T[2] = state[2] = st[2]; T[3] = state[3] = st[3];
        T[4] = state[4] = st[4]; T[5] = state[5] = st[5]; T[6] = state[6] = st[6]; T[7] = state[7] = st[7];
        j = 0;
        for (j = 0; j < 64; j+= 16) { R(0); R(1);  R(2); R(3); R(4); R(5); R(6); R(7); R(8); R(9); R(10); R(11); R(12); R(13); R(14); R(15); }
        state[0] += T[0]; state[1] += T[1]; state[2] += T[2]; state[3] += T[3];
        state[4] += T[4]; state[5] += T[5]; state[6] += T[6]; state[7] += T[7];
}
