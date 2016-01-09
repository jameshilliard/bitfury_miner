#ifndef __ALL_MINERS_CLASSES__
#define __ALL_MINERS_CLASSES__

#include "common.h"
#include "MySystemUtils.h"

//поменять порядок байт
inline uint32_t SwapEndian(uint32_t k)
{
    uint8_t val;
    uint8_t *b=(uint8_t*)&k;
    val=b[0];b[0]=b[3];b[3]=val;
    val=b[1];b[1]=b[2];b[2]=val;
    return k;
}

class cNetworkSocket
{
public:
	cNetworkSocket();
	~cNetworkSocket();
	inline bool IsPresent()const{return Sock>0;}
	inline int GetSocketFD()const{return Sock;}
	bool OpenTCPIPv4(const char *ConectTo,const char* Port);
	inline int Write(const void* buff,int Len){return write(Sock,buff,Len);}
	inline int SendStr(const char* buff){return Write(buff,strlen(buff));}
	inline int Read(void* buff,int Len){return read(Sock,buff,Len);}
	inline int GetPresentInputDataSize()const{int val;return ioctl(Sock,FIONREAD,&val)<0?0:val;}
	inline int GetPresentOutputDataSize()const{int val;return ioctl(Sock,TIOCOUTQ,&val)<0?0:val;}
	bool Close();
protected:
	int Sock;
};

/*
class cFIFOBuffer
{
public:
    cFIFOBuffer(uint32_t valBufferSize=1024*4);
    ~cFIFOBuffer();
    inline uint32_t GetFreeSpace()const{return BufferSize-WritePos+ReadPos-1;}
    inline uint32_t GetDataSpace()const{return BufferSize-WritePos+ReadPos-1;}
protected:
    uint8_t *Buffer;
    uint32_t BufferSize;
    uint32_t ReadPos;
    uint32_t WritePos;
};


cFIFOBuffer::cFIFOBuffer(uint32_t valBufferSize)
    :Buffer(nullptr),BufferSize(valBufferSize),ReadPos(0),WritePos(0)
{
    Buffer=new uint32_t[BufferSize];
}
cFIFOBuffer::~cFIFOBuffer()
{
}
*/

class cBase64Encoder
{
public:
    cBase64Encoder(const char *Str=nullptr);
    ~cBase64Encoder();
    const char *Encode(const char *Str,size_t input_length=0);
    inline const char* GetResult()const{return ResultStr?ResultStr:"";}
private:
    char *ResultStr;
    size_t ResultStrSize;
    static const char encoding_table[];
    static const uint8_t mod_table[];
};

class cHTTPConection;
class cHTTPVar
{
public:
    inline cHTTPVar():Name(nullptr),Value(nullptr),Next(nullptr){}
    inline ~cHTTPVar(){Name=nullptr;Value=nullptr;Next=nullptr;}
    inline const char *GetName()const{return Name;}
    inline const char *GetValue()const{return Value;}
    inline cHTTPVar* GetNext(){return Next;}
    inline const cHTTPVar* GetNext()const{return Next;}
protected:
    const char *Name;
    const char *Value;
    bool Parse(char *&Str);
    cHTTPVar *Next;
    inline void SetNext(cHTTPVar *Obj){Next=Obj;}
friend class cHTTPConection;
};

class cHTTPConection//:public cNetworkSocket
{
public:
    cHTTPConection(const char *valHost,const char *valPort=nullptr,const char *valUserName=nullptr,const char *valPasswd=nullptr);
    ~cHTTPConection();
    bool Open();
    inline cNetworkSocket& GetSock(){return Sock;}
    bool OnProcessData();
    inline bool IsConnectionPresent()const{return Sock.IsPresent();}
    inline bool IsParsingFinished()const{return ResultParsingIsFinished;}
    inline bool IsWaitingForResult()const{return WaitingForResult;}
    const cHTTPVar* SearchHeaderVar(const char *Name)const;
    inline const cHTTPVar* GetFirstHeaderVar()const{return Vars;}
    inline uint32_t GetHeaderVarsCount()const{return VarsCount;}
    inline uint32_t GetResultDataSize()const{return DataSize;}
    inline uint32_t GetHTTPResultCode()const{return HTTPResultCode;}
    inline uint32_t GetHTTPVersionHi()const{return HTTPVersion/100000;}
    inline uint32_t GetHTTPVersionLo()const{return HTTPVersion%100000;}
    inline const char* GetHTTPResultText()const{return ResultText?ResultText:"";}
    inline const uint8_t* GetResultData()const{return &Buffer[BufferEndHeaderPos];}
    bool SendNewRequest(const char *Buffer,int len=0);
    inline cBase64Encoder& GetAuthBase64Encoder(){return Auth;}
    inline uint32_t GetUserStateValue()const{return UserStateValue;}
    inline void SetUserStateValue(uint32_t Val){UserStateValue=Val;}
    void DumpHeaderVars()const;
protected:
    uint32_t UserStateValue;
    uint32_t BufferSize;
    uint8_t *Buffer;
    uint32_t BufferPos;
    uint32_t BufferEndHeaderPos;

    cNetworkSocket Sock;
    cBase64Encoder Auth;
    cStringType Host;
    cStringType Port;
    cStringType UserName;
    cStringType UserPwd;

    bool CheckIsHeaderFinished();
    bool CheckIsDataFinished();
    bool SearchCunk();

    bool HeaderFinished;
    bool DataFinished;
    cHTTPVar* Vars;
    uint32_t VarsCount;
    bool KeepAliveMode;
    uint32_t DataSize;
    uint32_t HTTPVersion;
    uint32_t HTTPResultCode;
    const char *ResultText;
    bool ResultParsingIsFinished;
    bool WaitingForResult;
    bool ChunkedTransmition;

    void OnFinishParsingHeader();
    bool ParseHeader();
    void PrepareForNewRequest();
    void ClearAllHeaderVars();
};


class cJSonVar;

const cJSonVar* ParseJSon(char *Buffer);

class cJSonVar
{
public:
    cJSonVar();
    ~cJSonVar();
    inline bool IsNull()const{return NullValue;}
    inline bool IsBool()const{return boolValue;}
    inline bool GetBoolValue()const{return bValue;}
    inline bool IsInteger()const{return integerValue;}
    inline int GetIntValue()const{return iValue;}
    inline bool IsArray()const{return Array;}
    inline bool IsVariable()const{return Value!=nullptr;}
    inline bool IsHash()const{return Hash;}
    inline const char* GetName()const{return Name;}
    inline uint32_t GetIndexPosition()const{return ArrayPosition;}
    inline const char* GetValue()const{return Value;}
    inline const cJSonVar* operator[](uint32_t Index)const{return GetByIndex(Index);}
    inline const cJSonVar* operator[](const char *Name)const{return Search(Name);}
    inline uint32_t GetArrayLen()const{return ArrayLen;}
    inline const cJSonVar* GetNext()const{return NextVar;}
    inline const cJSonVar* GetInternal()const{return InternalVarHead;}
    inline uint32_t GetInternalCount()const{return InternalVarCount;}
    const cJSonVar* Search(const char *Name)const;
    const cJSonVar* GetByIndex(uint32_t Index)const;
    void PrintSelf(FILE *f)const;
protected:
    const char *Name;
    const char *Value;
    uint32_t ArrayLen;
    //const char **Values;
    cJSonVar* NextVar;
    cJSonVar* InternalVarHead;
    uint32_t InternalVarCount;
    bool Array;
    bool Hash;
    bool NullValue;
    bool boolValue;
    bool bValue;
    bool integerValue;
    int  iValue;


    void CleanInternalVars();
    void CleanInternalArray();
    uint32_t ArrayPosition;
    inline void SetArrayPos(uint32_t ArrayLen){ArrayPosition=ArrayLen;}

    const char* ParseYourVar(char *&Buffer);
    const char* ParseHash(char *&Buffer);
    const char* ParseVar(char *&Buffer);
    const char* ParseArray(char *&Buffer);
    const char* ParseString(char *&Buffer,const char *&Var);
    const char* ParseValue(char *&Buffer);
    const char* ParseYourArrayValue(char *&Buffer);

friend const cJSonVar* ParseJSon(char *Buffer);
};


class cNonceJobsPull;
class cSearchNonceJob
{
public:
    cSearchNonceJob();
    virtual ~cSearchNonceJob();
    const uint8_t *GetMidState()const{return MidState;}
    const uint8_t *GetLeftData()const{return OtherData;}
    //const char *GetData()const{return hexData;}
    virtual bool SetFoundNonce(uint32_t Nonce);
    virtual void OnJobFinished()=0;
    virtual void OnJobCanceled()=0;
    virtual cSearchNonceJob *GenerateFromSelfNextJob(){return nullptr;};
    cSearchNonceJob *GetNext(){return Next;};
    virtual uint16_t GetCurrentDifficulty(){return 1;}
    inline bool CheckIsNonceFound(uint32_t Nonce)const{for(uint8_t i=0;i<FoundNoncesCount;i++){if(FoundNonces[i]==Nonce)return true;}return false;}
protected:
    uint8_t MidState[32];
    uint8_t OtherData[64];
    static const uint8_t MaxFoundNonces=16;
    uint32_t FoundNonces[MaxFoundNonces];
    uint8_t FoundNoncesCount;

    cSearchNonceJob *Next;
    void SetNext(cSearchNonceJob *Obj){Next=Obj;}
friend class cNonceJobsPull;
};

class cFoundNonceJob
{
public:
    cFoundNonceJob();
    virtual ~cFoundNonceJob();
    virtual void OnFinished()=0;
    virtual void OnCanceled()=0;
    cFoundNonceJob *GetNext(){return Next;};
protected:
    cFoundNonceJob *Next;
    void SetNext(cFoundNonceJob *Obj){Next=Obj;}
friend class cNonceJobsPull;
};

class cNonceJobsPull
{
public:
    cNonceJobsPull();
    virtual ~cNonceJobsPull();
    void SetMaxJobCount(uint16_t NewCount);
    uint16_t GetMaxJobCount()const{return MaxJobCount;}
    uint16_t GetJobCount()const{return JobCount;}
    uint16_t GetNeedJobCount()const{return MaxJobCount>JobCount?MaxJobCount-JobCount:0;}
    uint16_t GetNonceCount()const{return NonceCount;}
    cSearchNonceJob* GetNextJob();
    void ClearJobs();
    bool AddJob(cSearchNonceJob* Job);
    cFoundNonceJob* GetNextNonce();
    void ClearNonce();
    bool AddNonce(cFoundNonceJob* Job);
    bool AddNonceToHead(cFoundNonceJob* Job);
    virtual void OnHaveNewNonce();
    virtual void OnNeedMoreJobs();
    bool doTrace;
protected:
    uint16_t MaxJobCount;
    cSearchNonceJob *FirstJob,*LastJob;
    uint16_t JobCount;

    cFoundNonceJob *FirstNonce,*LastNonce;
    uint16_t NonceCount;
    CMutex Mutex;
};

class cGetWorkJobsPull;
class cSearchGetWorkJob:public cSearchNonceJob
{
public:
    cSearchGetWorkJob(cGetWorkJobsPull *OwnerObj,const uint8_t *valMidState,const uint8_t* Other,const char *hexString);
    virtual ~cSearchGetWorkJob();
    virtual bool SetFoundNonce(uint32_t Nonce);
    virtual void OnJobFinished();
    virtual void OnJobCanceled();
    const char *GetHexString()const{return hexData;}
protected:
    char hexData[256+4];
    cGetWorkJobsPull *Owner;
friend class cGetWorkJobsPull;
};

class cFoundGetWorkNonceJob:public cFoundNonceJob
{
public:
    cFoundGetWorkNonceJob(const char *hexString);
    virtual ~cFoundGetWorkNonceJob();
    virtual void OnFinished();
    virtual void OnCanceled();
    const char* GetHexDataString()const{return hexData;}
protected:
    char hexData[256+4];
friend class cGetWorkJobsPull;
};

class cGetWorkJobsPull:public cNonceJobsPull
{
public:
    cGetWorkJobsPull(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd);
    virtual ~cGetWorkJobsPull();
    virtual void OnHaveNewNonce();
    virtual void OnNeedMoreJobs();
    bool Run();
    bool Stop();
    inline uint32_t GetAcceptedShares()const{return PoolAcceptedShares;}

    bool SaveAllJobsToFile(FILE *f);
    bool PlayAllJobsFromFile(FILE *f);
protected:
    cHTTPConection *ListenConnection;
    cHTTPConection **WorkConnections;
    uint8_t WorkConnectionCount;
    CMutex HTTPMutex;

    bool Working;

    void ThreadFunc();
    bool CreateAllHTTPConnection();
    bool ClearAllHTTPConnection();

    bool SendListenRequest(cHTTPConection &obj);
    bool SendWorkSubmit(cHTTPConection &con,const char *hexData);
    bool SendGetWork(cHTTPConection &con);
    bool OnNewLongPollWorkCome(cHTTPConection &con);
    bool OnNewWorkCome(cHTTPConection &con);
    bool OnSubmitWorkCome(cHTTPConection &con);

    bool CreateNewJob(char *Buff);

    cStringType Host;
    cStringType Port;
    cStringType UserName;
    cStringType UserPwd;
    cStringType LongPullChannel;
    uint32_t PoolAcceptedShares;

    ThreadRunnerWithControlPipes<cGetWorkJobsPull,&cGetWorkJobsPull::ThreadFunc,true> Thread;

    FILE *PlayFile;
    bool Save;
    void ClosePlayFile();
};

void DataToHex(const uint8_t *k,char *Str,uint32_t len,bool needend=true);

class cByteBufferType
{
public:
    inline cByteBufferType(){InitBufferVars();}
    inline cByteBufferType(const uint8_t *val,uint32_t len){InitBufferVars();Assign(val,len);}
    inline cByteBufferType(const char *str){InitBufferVars();Assign(str);}
    cByteBufferType(const cByteBufferType& obj){InitBufferVars();Assign(obj);}
    ~cByteBufferType(){FreeBuffer();}
    void PrepareBuffer(uint32_t PrepareSize=0);
    inline void CleanBuffer(){Len=0;}
    inline void CleanBufferData(uint8_t v=0){if(Buffer){memset(Buffer,v,Len);}}
    inline void CleanAllBufferData(uint8_t v=0){if(Buffer){memset(Buffer,v,Size);}}
    inline void FreeBuffer(){if(Buffer){free(Buffer);Buffer=nullptr;Size=Len=0;}}
    inline bool IsEmpty()const{return Len==0;}
    inline bool IsPresent()const{return Len>0;}
    inline uint32_t GetLength()const{return Len;}
    inline uint32_t GetNeedStringLength()const{return Len*2;}
    inline uint32_t GetBufferSize()const{return Size;}
    inline const uint8_t* GetBuffer()const{return Buffer;}
    inline operator const uint8_t*()const{return Buffer;}
    inline operator uint8_t*(){return Buffer;}
    inline uint8_t* GetBuffer(){return Buffer;}
    inline uint8_t* GetBuffer(uint32_t S){PrepareBuffer(S);return Buffer;}
    inline void SetLength(uint32_t S){PrepareBuffer(S);Len=S;}
    inline uint8_t& operator[](uint32_t index){return Buffer[index];}
    inline uint8_t operator[](uint32_t index)const {return Buffer[index];}
    inline void Assign(const uint8_t *val,uint32_t l){CleanBuffer();PrepareBuffer(l);if(l>0){memcpy(Buffer,val,l);Len=l;}}
    inline void Concat(const uint8_t *val,uint32_t l){PrepareBuffer(Len+l);if(l>0){memcpy(&Buffer[Len],val,l);Len+=l;}}
    inline void Assign(const char *str){CleanBuffer();uint32_t l=strlen(str)/2;PrepareBuffer(l);if(l>0){Len=HexToBuff(str,Buffer,l);}}
    inline void Concat(const char *str){uint32_t l=strlen(str)/2;PrepareBuffer(Len+l);if(l>0){Len+=HexToBuff(str,Buffer,l);}}
    inline void Assign(const cByteBufferType& obj){CleanBuffer();PrepareBuffer(obj.GetLength());if(obj.Len>0){memcpy(Buffer,obj.Buffer,obj.Len);Len=obj.Len;}}
    inline void Concat(const cByteBufferType& obj){PrepareBuffer(Len+obj.Len);if(obj.Len>0){memcpy(&Buffer[Len],obj.Buffer,obj.Len);Len+=obj.Len;}}
    inline cByteBufferType& operator=(const cByteBufferType& obj){Assign(obj);return *this;}
    inline cByteBufferType& operator+=(const cByteBufferType& obj){Concat(obj);return *this;}
    inline cByteBufferType operator+(const cByteBufferType& obj){cByteBufferType k(*this);k+=obj;return k;}
    inline cByteBufferType operator+(const char *str){cByteBufferType k(*this);k+=str;return k;}
    inline bool operator==(const cByteBufferType& obj){return Len==obj.Len && memcmp(Buffer,obj.Buffer,Len)==0;}
    inline cByteBufferType& move(cByteBufferType& obj){FreeBuffer();Buffer=obj.Buffer;Size=obj.Size;Len=obj.Len;obj.Buffer=nullptr;obj.Size=obj.Len=0;return *this;}
    inline bool operator==(const uint8_t* obj){return Len>0 && memcmp(Buffer,obj,Len)==0;}
    inline bool operator==(const char* str){cByteBufferType k(str);return (*this)==k;}
    void swap_array();
    void SwapDWORDEndians();

    static uint32_t HexToBuff(const char *Str,uint8_t *res,uint32_t len);
    inline cStringType ToText()const{cStringType str;str.PrepareSize(Len*2);DataToHex(Buffer,str.GetBuffer(),Len);return str;}
protected:
    uint8_t *Buffer;
    uint32_t Size;
    uint32_t Len;

    inline uint32_t CalculateBufferSize(uint32_t Need){return ((Need)+(((Need)%(32))?((32)-((Need)%(32))):0));}
    inline void InitBufferVars(){Buffer=nullptr;Size=0;Len=0;}
};


class cStratumJobsPull;
class cFoundStratumNonceJob;
class cSearchStratumWorkJob:public cSearchNonceJob
{
public:
    cSearchStratumWorkJob(cStratumJobsPull *OwnerObj,const uint8_t *valMidState,const uint8_t* Other,const char *hexString);
    cSearchStratumWorkJob(const cSearchStratumWorkJob& obj);
    virtual ~cSearchStratumWorkJob();
    virtual bool SetFoundNonce(uint32_t Nonce);
    bool SetTime(uint32_t Nonce);
    virtual void OnJobFinished();
    virtual void OnJobCanceled();
    virtual cSearchNonceJob *GenerateFromSelfNextJob();
    const char *GetHexString()const{return hexData;}
    virtual uint16_t GetCurrentDifficulty();
protected:
    cStringType JobName;
    cByteBufferType ExtraNonce;
    cByteBufferType nTime;
    uint32_t nTimeValue;
    time_t nTimeValueRepresentIn;

    char hexData[256+4];
    cStratumJobsPull *Owner;
    uint32_t JobCounter;
    inline bool IsCanGenerateNextFromSelf()const;
    bool NextJobGenerated;
friend class cStratumJobsPull;
friend class cFoundStratumNonceJob;
friend void TestStratumCalculation();
};

class cFoundStratumNonceJob:public cFoundNonceJob
{
public:
    cFoundStratumNonceJob(const cSearchStratumWorkJob &obj,uint32_t Nonce);
    virtual ~cFoundStratumNonceJob();
    virtual void OnFinished();
    virtual void OnCanceled();
    const char* GetHexDataString()const{return hexData;}
protected:
    char hexData[256+4];

    cStringType JobName;
    cByteBufferType ExtraNonce;
    cByteBufferType nTime;
    cByteBufferType Nonce;

friend class cStratumJobsPull;
};


class cStratumJobsPull:public cNonceJobsPull
{
public:
    cStratumJobsPull(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd);
    virtual ~cStratumJobsPull();
    virtual void OnHaveNewNonce();
    virtual void OnNeedMoreJobs();
    bool Run();
    bool Stop();
    inline uint32_t GetAcceptedShares()const{return PoolAcceptedShares;}
    inline uint32_t GetCurrentJobNumber()const{return JobCounter;}
    inline uint16_t GetCurrentDifficulty()const{return NowDifficulty;}
protected:
    cNetworkSocket sock;
    CMutex SockMutex;
    CMutex SignalMutex;

    bool Working;

    void ThreadFunc();
    /*bool CreateAllHTTPConnection();
    bool ClearAllHTTPConnection();

    bool SendListenRequest(sock &obj);
    bool SendWorkSubmit(sock &con,const char *hexData);
    bool SendGetWork(sock &con);
    bool OnNewLongPollWorkCome(sock &con);
    bool OnNewWorkCome(cHTTPConection &con);
    bool OnSubmitWorkCome(cHTTPConection &con);

    bool CreateNewJob(char *Buff);*/
    void SignalNewNonce();
    void SignalNeedJob();
    void ClearSignalNewNonce();
    void ClearSignalNeedJob();
    bool SignalNewNonceSended;
    bool SignalNeedJobSended;

    cStringType Host;
    cStringType Port;
    cStringType UserName;
    cStringType UserPwd;

    uint32_t PoolAcceptedShares;
    uint32_t PoolShareDiff;
    uint32_t LastPoolCommandId;
    uint32_t GetNextPoolCommandId(){if(LastPoolCommandId==2000000000){LastPoolCommandId=3;return LastPoolCommandId;}else{return ++LastPoolCommandId;}}

    enum ProtocolState
    {
        NotConnected=0,
        Start,
        WaitForSibscribe,
        WaitMinerAuth,
        NormalWork
    };

    ProtocolState ConnectionState;

    ThreadRunnerWithControlPipes<cStratumJobsPull,&cStratumJobsPull::ThreadFunc,true> Thread;

    void OnConnectedToServer();
    void OnProcessInputData();
    static const uint32_t InputBufferSize=4096;
    char InputBuffer[InputBufferSize];
    uint32_t InputBufferUsed,InputBufferLastLineTestedPos;
    inline uint32_t GetInputBufferFreeSpace()const{return InputBufferSize-InputBufferUsed;}
    inline void ClearInputBuffer(){InputBufferUsed=InputBufferLastLineTestedPos=0;}
    inline void CloseSocket(){sock.Close();ClearInputBuffer();ConnectionState=NotConnected;}
    inline char* GetInputBufferFreeSpaceBegin(){return &InputBuffer[InputBufferUsed];}
    inline uint32_t GetInputBufferUsed()const{return InputBufferUsed;}
    uint32_t InputBufferShift(uint32_t FromPos);
    uint32_t PrepareOneLineCommand();
    void ParseOneCommand(char *buff);
    void OnSubscribedToServer(const cJSonVar *Head);
    void OnAuthorizedToServer(const cJSonVar *Head);
    void OnNotifyFromPool(const cJSonVar *Head);
    void OnNewMineJob(const cJSonVar *Head);
    void OnAnswerFromServer(const cJSonVar *Head);
    void OnSetDifficulty(const cJSonVar *Head);


    CMutex MineDataMutex;

    uint32_t JobCounter;

    cByteBufferType ExtraNonce1;
    uint32_t ExtraNonce2Size;
    uint64_t NowExtraNonce2;

    cStringType JobName;
    cByteBufferType PrevHash;
    cByteBufferType CoinBase1;
    cByteBufferType CoinBase2;
    cByteBufferType Version;
    cByteBufferType nBits;
    uint32_t        nTime;
    time_t          nTimeRepresentIn;
    cByteBufferType *MerkleRoot;
    uint32_t        MerkleRootSize;
    uint32_t        MerkleRootLen;
    inline void CleanMerkleRoot(){MerkleRootLen=0;}
    inline void FreeMerkleRoot(){if(MerkleRoot){delete [] MerkleRoot;MerkleRootLen=MerkleRootSize=0;}}
    inline cByteBufferType& GetMerkleRoot(uint32_t Index){return MerkleRoot[Index];}
    cByteBufferType* AddMerkleRoot();

    void DoubleSHA256(cByteBufferType &obj,cByteBufferType &result);
    void CalculateMerkleRoot(cByteBufferType &FirstTransaction,cByteBufferType &result);

    bool GenerateNewJob();
    bool SendNonceToPull(cFoundStratumNonceJob *NonceData);
    bool TestMode;
    inline bool IsTestModeOn()const{return TestMode;}
    uint16_t  NowDifficulty;
friend void TestStratumCalculation();
};
inline bool cSearchStratumWorkJob::IsCanGenerateNextFromSelf()const {return Owner->GetCurrentJobNumber()==JobCounter && !NextJobGenerated;}



#endif // __ALL_MINERS_CLASSES__
