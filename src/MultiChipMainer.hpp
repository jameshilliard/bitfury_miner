#ifndef __MINER_HPP__
#define __MINER_HPP__

extern volatile uint32_t *gpio;
extern const uint32_t sha_initial_state[8];

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0
#define GPIO_GET *(gpio+13) // read

// Max number of SPIs: 8
// Max number of chips per spi: 255
#define MAX_SPI_COUNT 8
#define MAX_CHIPS_PER_SPI 250

extern int SPI_COUNT;
extern int CHIPS_PER_SPI;

extern int SPI_CYCLES_PER_TAKT;
extern int SPI_CYCLES_PER_TAKT_2;
extern int SPI_READ_CYCLES;

extern int SPI_RESET_CYCLES;


class cPinWork
{
public:
    inline void PinSetup(uint8_t gpioNumber) { 
      mask = 1 << gpioNumber;
    }
    inline void PinSetup(uint8_t count, unsigned short gpioNumbers[]) { 
      mask = 0;
      for (uint8_t i=0; i<count; i++)
        mask |= 1 << gpioNumbers[i];
    }
    
    inline void Set() const {
      for (uint8_t i=0; i<8; i++)
        GPIO_SET = mask;
    }
    inline void LS_Set() const {
      for (uint8_t i=0; i<5; i++)
        Set();
    }
    inline void Clean() const {
      for (uint8_t i=0; i<8; i++)
        GPIO_CLR = mask;
    }
    inline void LS_Clean() const {
      for (uint8_t i=0; i<5; i++)
        Clean();
    }
    inline uint8_t Read() const {
      /*for (uint8_t i = 0; i < 10; i++) {
        uint32_t data = GPIO_GET & mask;
        printf("READ: ADDR 0x%08x CONTENT 0x%08x MASK 0x%08x => DATA: 0x%08x\n", (gpio+13), GPIO_GET, mask, data);
      }*/
      uint32_t data = 0;
      for (uint8_t i = 0; i < SPI_READ_CYCLES; i++) {
        data = GPIO_GET & mask;
      }
      return data == 0 ? 0 : 1;
    }
    
    inline uint32_t GetMask() const {
      return mask;
    }

	
	inline static void SetAndCleanWithMask(uint32_t set_mask, uint32_t clean_mask) {
      for (uint8_t i=0; i<SPI_CYCLES_PER_TAKT_2; i++) {
        GPIO_SET = set_mask;
        GPIO_CLR = clean_mask;
      }
	}
	
protected:
    uint32_t mask;
};

class cUSBComChipExchange
{
public:
    cUSBComChipExchange();
    ~cUSBComChipExchange();
    bool Init(const char *file_name);
    uint16_t TxRx(uint32_t spi_mask,uint8_t *InBuff[],uint8_t *OutBuff[],uint16_t Len);
    uint16_t TxRx(uint16_t spi_number,const uint8_t *InBuff,uint8_t *OutBuff,uint16_t Len);
    bool Ping();
    inline bool IsConnectionOk()const{return fd>0;}
    inline uint32_t GetFrameTimeOut()const{return TimeOuts;}
    inline uint32_t GetLenError()const{return LenError;}
    inline uint32_t GetFrameError()const{return FrameError;}
protected:
    int fd;
    uint32_t TimeOuts,LenError,FrameError;
    void CloseFD();
};

class cMultyBufferSPIExchange
{
public:
    cMultyBufferSPIExchange();
    bool Init();
    inline void ResetLine();
    inline void ResetLine(uint16_t spi_number);
    inline void SendRecv(uint32_t spi_mask,const uint8_t *In,uint8_t *Out);
    inline void SendRecv(uint16_t spi_number,const uint8_t In,uint8_t &Out);
    uint16_t TxRx(uint32_t spi_mask,uint8_t *InBuff[],uint8_t *OutBuff[],uint16_t Len);
    uint16_t TxRx(uint16_t spi_number,const uint8_t *InBuff,uint8_t *OutBuff,uint16_t Len);
	void doWatchDog();
	void doPowerReset(int powerResetCycles);
protected:
    cPinWork Clk;
    cPinWork Out[MAX_SPI_COUNT];
    cPinWork In[MAX_SPI_COUNT];
	cPinWork pinWatchDog;
	cPinWork pinPowerReset;
    
    inline void SetAllOUT() {
      static uint32_t mask = 0;
	  if (mask == 0)
        for(uint8_t i=0;i<SPI_COUNT;i++)
          mask |= Out[i].GetMask();
      for (uint8_t i=0; i<SPI_CYCLES_PER_TAKT; i++)
        GPIO_SET = mask;
    }
    inline void CleanAllOUT() {
      static uint32_t mask = 0;
	  if (mask == 0)
        for(uint8_t i=0;i<SPI_COUNT;i++)
          mask |= Out[i].GetMask();
      for (uint8_t i=0; i<SPI_CYCLES_PER_TAKT; i++)
        GPIO_CLR = mask;
    }
    inline void SetAllClk() {
      static uint32_t mask = 0;
	  if (mask == 0)
        mask |= Clk.GetMask();
      for (uint8_t i=0; i<SPI_CYCLES_PER_TAKT; i++)
        GPIO_SET = mask;
    }
    inline void CleanAllClk() {
      static uint32_t mask = 0;
	  if (mask == 0)
	    mask |= Clk.GetMask();
      for (uint8_t i=0; i<SPI_CYCLES_PER_TAKT; i++)
        GPIO_CLR = mask;
    }
    
    bool ConfigClkPorts();
    bool ConfigOutPorts();
    bool ConfigInPorts();
    bool ConfigControlPorts();
};

class cChipCommandBuffer
{
public:
    cChipCommandBuffer(uint16_t DefBuffSize=1024*8);
    ~cChipCommandBuffer();
    inline uint8_t *GetBuffer(){return Buffer;}
    inline void SetLength(uint16_t k){BufferPos=k;}
    inline const uint8_t *GetBuffer()const{return Buffer;}
    inline uint16_t GetLength()const{return BufferPos;}
    inline uint16_t GetSize()const{return BufferSize;}
    inline uint16_t GetFree()const{return BufferSize-BufferPos;}
    inline void Clear(){BufferPos=0;}

    void EmitBuffReverse(const uint8_t *str, uint16_t Size); //add bytes with reversed bits order
    void EmitBuff(const uint8_t *str, uint16_t Size);
    void EmitBreak(void) { EmitBuff((uint8_t*)"\x4", 1); }
    void EmitFSync(void) { EmitBuff((uint8_t*)"\x6", 1); }
    //void EmitFAsync(void) { EmitBuff((uint8_t*)"\x5", 1); }
    void EmitData(uint16_t addr, const uint8_t *buf, uint16_t len);
	
	// 0x4: 100 - break
	// 0x5: 101 - fasync
	// 0x6: 110 - fsync
	// 0x7: 111 - data
	
	// 3 x 8 => 24 bit => 3 bytes => 
	// 76543210.76543210.76543210
	// 10101010                   
	
	// 10110110.11011011.01101101
	// => (hex) B6 DB 6D - works with errors
	
	// does not work:
	// 01101101.11011011.10110110
	// => (hex) 6D DB B6
    void EmitFAsync(int num) { 
		// 3 fasync / 2 bytes: 2 zero pause: 00101001.01001010 => (hex) 29 4A
		/*while (num >= 3) { // 0,1,2 works without errors
			EmitBuff((uint8_t*)"\x29", 1);
			EmitBuff((uint8_t*)"\x4A", 1);
			num -= 3;
		}*/
		
		// 2 fasync per byte: 10101010
		/*while (num >= 2) { // 1,2  9,10 - works (last with errors)
			EmitBuff((uint8_t*)"\xAA", 1);
			num -= 2;
		}*/
		
		/*while (num >= 8) {
			//EmitBuff((uint8_t*)"\xB6", 1);
			//EmitBuff((uint8_t*)"\xDB", 1);
			//EmitBuff((uint8_t*)"\x6D", 1);
			
			//EmitBuff((uint8_t*)"\x55", 1);
			//EmitBuff((uint8_t*)"\x55", 1);
			//EmitBuff((uint8_t*)"\x55", 1);
			//EmitBuff((uint8_t*)"\x55", 1);
			num -= 8;
		}*/
		for (uint8_t i=0; i < num; i++) {
			EmitBuff((uint8_t*)"\x5", 1);
		}
	}
	
protected:
    uint16_t BufferSize;
    uint8_t *Buffer;
    uint16_t BufferPos;
};


class cSearchNonceJob;
class cNonceJobsPull;

class cBitfuryChip
{
public:
    cBitfuryChip();
    ~cBitfuryChip();
    bool InitChip();
    inline uint8_t GetChipFreqCode()const{return FreqCode;}
    inline bool SetChipFreqCode(uint8_t val){FreqCode=val;return true;}
    void FillFreqValueBuffer(uint8_t *Obj);
    inline cChipCommandBuffer &GetSendBuffer(){return CommandBuffer;}
    inline cChipCommandBuffer &GetRecvBuffer(){return ResultBuffer;}
    bool SendNextWorkBuffer();
    void OnNewResultFromChip();
    inline void SetWorkManager(cNonceJobsPull *Obj){WorkManager=Obj;}
    inline void SetSpiId(uint16_t Val){SpiId=Val;}
    inline void SetSpiSeqId(uint16_t Val){SpiSeqId=Val;}
    inline uint32_t GetJobsDone()const{return JobsDone;}
    inline uint32_t GetFoundSolutions()const{return FoundSolutions;}
    inline uint32_t GetFoundSolutionsByDifficulty()const{return FoundSolutionsByDifficulty;}
    inline void EnablePrintChipInfo(bool Val=true){OutChipInfo=Val;}
    inline void EnablePrintChipInfoOnce(bool Val=true){OutChipInfoOnce=Val;}
    inline uint32_t GetLastJobTakeTime()const{return LastJobTakeTime;}
    inline uint32_t GetJobsWithoutSolutions()const{return JobsWithoutSolutions;}
    inline void ResetJobsWithoutSolutions(){JobsWithoutSolutions=0;}
    void PrintChipInfo();
    void SendOSCValue();
    inline void SetEnabled(bool Val) { Enabled=Val; }
    inline bool IsEnabled() const { return Enabled; }
protected:
    uint16_t SpiId, SpiSeqId;
    cNonceJobsPull *WorkManager;
    cChipCommandBuffer CommandBuffer,ResultBuffer;
    uint8_t FreqCode;
    cSearchNonceJob *CurrentJob,*PrevJob;
    bool Enabled,DividerOn,FastOscOn;

    uint32_t NextJobBuffer[20];
    //uint32_t PrevJobBuffer[19];


    void config_reg(int cfgreg, int ena);

    uint32_t decrypt_nonce(uint32_t in);
    void ms3_compute(uint32_t *p);//compute round 3 of sha256
    bool test_nonce(uint32_t tnon, uint32_t *ms, uint32_t *w,uint32_t &diff);//test if nonce is ok on double sha256
    uint32_t fix_nonce(uint32_t innonce, uint32_t *ms, uint32_t *w,uint32_t &res_non,uint16_t &KeyPos,uint32_t &Diff);//fix nonce form chip presentation to normal and test if it okey

    uint32_t PrevJobResults[17];

    uint32_t FoundSolutionsByDifficulty;
    uint16_t ChipReads;
    time_t StartWorkTime;
    bool OutChipInfo;
    bool OutChipInfoOnce;
    uint16_t LastChipReads;
    uint32_t JobsDone;
    uint64_t LastJobTime;
    uint64_t LastJobTakeTime;
    uint32_t JobsWithoutSolutions;
    
public:    
    uint32_t FoundSolutions;
    uint32_t FoundErrors;
};


inline void cMultyBufferSPIExchange::ResetLine()
{
    uint8_t i;
    CleanAllOUT();
    SetAllClk();
    for(i = 0; i < SPI_RESET_CYCLES; i++)
    {
        SetAllOUT();
        CleanAllOUT();
    }
    CleanAllClk();
}

inline void cMultyBufferSPIExchange::ResetLine(uint16_t spi_number)
{
    uint8_t i;
    CleanAllOUT();
    Clk.Set();
    for(i = 0; i < SPI_RESET_CYCLES; i++)
    {
        Out[spi_number].LS_Set();
        Out[spi_number].LS_Clean();
    }
    Clk.Clean();
}
inline void cMultyBufferSPIExchange::SendRecv(uint16_t spi_number,const uint8_t InB,uint8_t &OutB)
{
	doWatchDog();

    int8_t i;
    Out[spi_number].Clean();
    Clk.LS_Clean();
    OutB=0;
    for(i = 7; i>=0; i--)
    {
	    if(InB & (1<<i))
        {
            Out[spi_number].LS_Set();
        }
        else
        {
            Out[spi_number].LS_Clean();
        }
        Clk.LS_Set();//send data and prepare read
		//read bit
		if(In[spi_number].Read())
        {
            OutB|=(1<<i);
        }
        Clk.Clean();
	}
}
inline void cMultyBufferSPIExchange::SendRecv(uint32_t spi_mask,const uint8_t *InB,uint8_t *OutB)
{
	doWatchDog();
	
	int8_t i;
	uint8_t i1;
	CleanAllOUT();
	CleanAllClk();
	memset(OutB,0,SPI_COUNT);
	for(i = 7; i>=0; i--)
	{
		uint32_t set_mask = 0;
		uint32_t clean_mask = 0;
		
    for(i1=0;i1<SPI_COUNT;i1++) if (spi_mask & (1 << i1))
    {
        if(InB[i1] & (1<<i))
        {
            //Out[i1].Set();
            set_mask |= Out[i1].GetMask(); 
        }
        else
        {
            //Out[i1].Clean();
            clean_mask |= Out[i1].GetMask();
        }
    }
		
		cPinWork::SetAndCleanWithMask(set_mask, clean_mask);
		
		SetAllClk();;//send data and prepare read
		//read bit
		for(i1=0;i1<SPI_COUNT;i1++) if (spi_mask & (1 << i1))
    {
        if(In[i1].Read())
        {
            OutB[i1]|=(1<<i);
        }
    }
		CleanAllClk();
		//LS_CleanPin(SPI_CLK);
	}
	//CleanAllOUT();
}


void SHA256_Full(uint32_t *state, uint32_t *data, const uint32_t *st);


#endif
