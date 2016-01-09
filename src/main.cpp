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
#include "BitcoinMine.hpp"
#include "MultiChipMainer.hpp"

#define new_array(type,elements) (type*)malloc(sizeof(type)*elements)
#define realloc_array(type,var,elements) (type*)realloc(var,sizeof(type)*elements);
#define delete_array(var) free(var)

#define DefaultPoolHost "stratum.bitcoin.cz"
#define DefaultPoolPort "3333"
#define DefaultPoolUser "slava81k.worker1"
#define DefaultPoolPswd "1"


const char *g_poolHost = DefaultPoolHost;
const char *g_poolPort = DefaultPoolPort;
const char *g_poolUser = DefaultPoolUser;
const char *g_poolPswd = DefaultPoolPswd;


int g_logDelay = 10;
int g_polingDelay = 50;
bool g_spiSmartInit = true;
int g_powerResetCycles = 70;

enum RunMode {
	RUN_MODE_MINING,
	RUN_MODE_TEST_BOARD,
	RUN_MODE_TEST_UNIT
};

RunMode g_runMode = RUN_MODE_MINING;

int g_testtoolOsc = 54-8;
int g_testtoolTestTime = 5;    

inline bool isTestDataSource() {
	return (g_runMode == RUN_MODE_TEST_BOARD || g_runMode == RUN_MODE_TEST_UNIT);
}
 
 
cHTTPConection::cHTTPConection(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd):
    UserStateValue(0),BufferSize(1024*8),Buffer(nullptr),BufferPos(0),BufferEndHeaderPos(0),Host(valHost,true),Port(valPort?valPort:"80",true)
    ,UserName(valUserName?valUserName:"",true),UserPwd(valPasswd?valPasswd:""),HeaderFinished(false),DataFinished(false),Vars(nullptr),VarsCount(0)
{
    Buffer=new_array(uint8_t,BufferSize);

    cStringType Str;
    Str.printf("%s:%s",UserName.GetText(),UserPwd.GetText());
    Auth.Encode(Str);

    PrepareForNewRequest();
}
cHTTPConection::~cHTTPConection()
{
    if(Buffer)
    {
        delete_array(Buffer);
    }
    ClearAllHeaderVars();
}
void cHTTPConection::ClearAllHeaderVars()
{
    while(Vars)
    {
        cHTTPVar *Obj=Vars;
        Vars=Vars->Next;
        delete Obj;
    }
    VarsCount=0;
}
void cHTTPConection::PrepareForNewRequest()
{
    ClearAllHeaderVars();
    BufferPos=BufferEndHeaderPos=0;
    HeaderFinished=DataFinished=false;
    KeepAliveMode=ResultParsingIsFinished=false;
    DataSize=HTTPVersion=HTTPResultCode=VarsCount=0;
    ResultText=nullptr;
    WaitingForResult=false;
    ChunkedTransmition=false;
}
bool cHTTPConection::SendNewRequest(const char *Buffer,int len)
{
    if(Sock.IsPresent() && (!KeepAliveMode || !ResultParsingIsFinished))
    {
        Sock.Close();
    }
    PrepareForNewRequest();
    if(!Sock.IsPresent())
    {
        if(!Open())
            return false;
    }
    int res;
    if(len<=0)
        res=Sock.SendStr(Buffer);
    else
        res=Sock.Write(Buffer,len);
    if(res<=0)
    {
        Sock.Close();
    }else
    {
        WaitingForResult=true;
    }
    return res>0?true:false;
}
const cHTTPVar* cHTTPConection::SearchHeaderVar(const char *Name)const
{
    const cHTTPVar* Obj=Vars;
    while(Obj)
    {
        if(strcmp(Obj->GetName(),Name)==0)
            return Obj;
        Obj=Obj->GetNext();
    }
    return nullptr;
}
bool cHTTPConection::Open()
{
    return Sock.OpenTCPIPv4(Host,Port);
}
inline char* EatEmpty(char *str)
{
    while(*str==' ' || *str=='\t') str++;
    return str;
}
inline char* EatEmptyFromEnd(const char *str,char *end)
{
    while(str<end && (*end==' ' || *end=='\t')) end--;
    if(*end==' ' || *end=='\t')
        *end='\0';
    else
    {
       end++;
       if(*end==' ' || *end=='\t')
        *end='\0';
    }
    return end;
}
inline char* SearchEmptyChar(char *str)
{
    while(*str!=' ' && *str!='\0' && *str!='\t') {str++;}
    return str;
}
bool cHTTPVar::Parse(char *&Str)
{
    char *k=Str;
    Name=k=EatEmpty(k);
    while(*k!='\0' && *k!='\n')
    {
        if(*k==':')
        {
            *k='\0';
            k++;
            break;
        }
        k++;
    }
    if(*k=='\0')
    {
        Str=k;
        return false;
    }
    EatEmptyFromEnd(Name,k-2);
    Value=k=EatEmpty(k);
    while(*k!='\0' && *k!='\n'){k++;}
    if(*k=='\n')
    {
        *k='\0';
        if(*(k-1)=='\r')
            *(k-1)='\0';
    }
    EatEmptyFromEnd(Value,k-1);
    k++;
    Str=k;
    return true;
}

bool cHTTPConection::ParseHeader()
{
    char *FirstLine=(char*)Buffer;
    uint32_t i;
    for(i=0;FirstLine[i]!='\n' && i<BufferPos;i++){ }
    if(i==BufferPos)
        return false;
    FirstLine[i]='\0';
    if(i>0 && FirstLine[i-1]=='\r')
        FirstLine[i-1]='\0';
    char *begin=FirstLine,*end=FirstLine;
    end=SearchEmptyChar(end);
    if(*end=='\0')
        return false;
    *end='\0';
    {
        uint32_t verhi=0,verlo=0;
        if(sscanf(begin,"HTTP/%u.%u",&verhi,&verlo)!=2 || verlo>100000 || verhi>100000)
            return false;
        HTTPVersion=verhi*100000+verlo;
    }
    end=begin=EatEmpty(end+1);
    {
        if(sscanf(begin,"%u",&HTTPResultCode)!=1 || HTTPResultCode==0)
            return false;
    }
    end=SearchEmptyChar(end);
    end=begin=EatEmpty(end+1);
    ResultText=begin;
    begin=&FirstLine[i+1];
    cHTTPVar *Last=nullptr;
    while(begin[0]!='\0' && begin<&FirstLine[BufferEndHeaderPos])
    {
        cHTTPVar *Obj=new cHTTPVar();
        if(!Obj->Parse(begin))
        {
            delete Obj;
            return false;
        }
        if(Last)
        {
            Last->SetNext(Obj);
            Last=Obj;
        }
        else
        {
            Last=Vars=Obj;
        }
        VarsCount++;
    }
    return true;
}
void cHTTPConection::DumpHeaderVars()const
{
    const cHTTPVar* Obj=GetFirstHeaderVar();
    while(Obj)
    {
        printf("[%s]=[%s]\n",Obj->GetName(),Obj->GetValue());
        Obj=Obj->GetNext();
    }
}
void cHTTPConection::OnFinishParsingHeader()
{
    if(ParseHeader())
    {
        const cHTTPVar *Var=SearchHeaderVar("Connection");
        if(Var && strcmp(Var->GetValue(),"keep-alive")==0)
            KeepAliveMode=true;
        Var=SearchHeaderVar("Content-Length");
        DataSize=0;
        if(Var && sscanf(Var->GetValue(),"%u",&DataSize)==1)
        {

        }
        Var=SearchHeaderVar("Transfer-Encoding");
        if(Var && strcmp(Var->GetValue(),"chunked")==0)
        {
            ChunkedTransmition=true;
        }
    }
}
bool cHTTPConection::SearchCunk()
{
    char *str=(char*)Buffer;
    uint32_t i=BufferEndHeaderPos,chunk_begin,len_begin=BufferEndHeaderPos;
    uint32_t NeedLen=0;
    while(str[i]!='\0' && i<BufferPos)
    {
        if(str[i]=='\r' && str[i+1]=='\n')
        {
            //found len begin
            if(sscanf(&str[len_begin],"%x",&NeedLen)!=1)
            {
                printf("HTTP: error read chunk size\n");
                return false;
            }
            if(NeedLen+i+1+3>BufferPos)
                return false;
            if(str[i+2+NeedLen]!='\r' && str[i+2+NeedLen+1]!='\n')
            {
                printf("error in chunk sintax\n");
                Sock.Close();
                return false;
            }
            str[i]='\0';
            str[i+1]='\0';
            i+=2;
            chunk_begin=i;
            break;
        }
        i++;
    }
    if(str[i]=='\0' || i==BufferPos)
        return false;
    i+=NeedLen;
    str[i++]='\0';
    str[i++]='\0';
    len_begin=i;
    if(strncmp(&str[i],"0\r\n\r\n",5)!=0)
    {
        printf("We not support multy chunk data\n");
        Sock.Close();
        return true;
    }
    BufferEndHeaderPos=chunk_begin;
    DataSize=NeedLen;
    WaitingForResult=false;
    ResultParsingIsFinished=true;
    return true;
}
bool cHTTPConection::CheckIsDataFinished()
{
    if(!HeaderFinished)
        return false;
    if(HTTPResultCode==0 || HTTPVersion==0)
        return false;
    if(ResultParsingIsFinished)
        return true;
    if(ChunkedTransmition)
    {
        if(DataSize>0)
        {
            WaitingForResult=false;
            ResultParsingIsFinished=true;
            return true;
        }
        //search cunk data
        return SearchCunk();
    }
    if(KeepAliveMode && DataSize>0 && DataSize<=(BufferPos-BufferEndHeaderPos))
    {
        WaitingForResult=false;
        ResultParsingIsFinished=true;
        return true;
    }
    if(KeepAliveMode && DataSize==0 && !Sock.IsPresent())
    {
        WaitingForResult=false;
        ResultParsingIsFinished=true;
        DataSize=BufferPos-BufferEndHeaderPos;
        return true;
    }
    if(!KeepAliveMode && !Sock.IsPresent())
    {
        WaitingForResult=false;
        ResultParsingIsFinished=true;
        DataSize=BufferPos-BufferEndHeaderPos;
        return true;
    }
    if(!KeepAliveMode && DataSize>0 && DataSize<=(BufferPos-BufferEndHeaderPos))
    {
        //not keep alive mode and all data loaded finishing connection
        Sock.Close();
        WaitingForResult=false;
        ResultParsingIsFinished=true;
        return true;
    }
    return false;
}
bool cHTTPConection::CheckIsHeaderFinished()
{
    if(HeaderFinished)
        return true;
    if(BufferEndHeaderPos<BufferPos && BufferPos>=2)
    {
        uint8_t *str=&Buffer[BufferEndHeaderPos];
        while(str+2<&Buffer[BufferPos])
        {
            if(str[0]=='\n' && str[1]=='\n')
            {
                HeaderFinished=true;
                str[0]='\0';
                str[1]='\0';
                BufferEndHeaderPos=str-Buffer+2;
                OnFinishParsingHeader();
                return true;
            }
            if(str+4<&Buffer[BufferPos] && str[0]=='\r' && str[1]=='\n' && str[2]=='\r' && str[3]=='\n')
            {
                HeaderFinished=true;
                str[0]='\0';
                str[1]='\0';
                str[2]='\0';
                str[3]='\0';
                BufferEndHeaderPos=str-Buffer+4;
                OnFinishParsingHeader();
                return true;
            }
            str++;
        }
        if(BufferPos>4)
        {
            BufferEndHeaderPos=BufferPos-4;
        }
    }
    return false;
}
bool cHTTPConection::OnProcessData()
{
    if(ResultParsingIsFinished)
    {
        if(Sock.IsPresent())
        {
            pollfd p;
            p.fd=Sock.GetSocketFD();
            p.events=POLLIN;
            p.revents=0;
            while(poll(&p,1,0)<0){ }
            if(p.revents&POLLIN)//is after finish parsing come some data, it`s state error
            {
                //need close socket
                Sock.Close();
            }
        }
        return true;
    }
    if(!Sock.IsPresent())
        return false;
    bool NeedReadMoreData=true;
    uint32_t NeedReadDataSize=BufferSize-BufferPos;
    if(CheckIsHeaderFinished())
    {
        if(CheckIsDataFinished())
            return true;
        if(DataSize>0)
        {
            NeedReadDataSize=DataSize-(BufferPos-BufferEndHeaderPos);
        }
    }
    if(!Sock.IsPresent())
        return false;
    pollfd p;
    p.fd=Sock.GetSocketFD();
    p.events=POLLIN;
    p.revents=0;
    while(poll(&p,1,0)<0){ }
    if(NeedReadMoreData && Sock.IsPresent() && (p.revents&POLLIN))
    {
        int readed=0;
        if(NeedReadDataSize>BufferSize-BufferPos)
            NeedReadDataSize=BufferSize-BufferPos;
        if(NeedReadDataSize>0)
        {
           readed=Sock.Read(&Buffer[BufferPos],NeedReadDataSize);
        }
        if(readed==0)//error socket already closed from other side
        {
            Sock.Close();
            return CheckIsDataFinished();
        }
        if(readed<0)
        {
            //some strange error
            Sock.Close();
            return CheckIsDataFinished();
        }
        BufferPos+=readed;
        if(CheckIsHeaderFinished() && CheckIsDataFinished())
            return true;
    }
    return false;
}

cNetworkSocket::cNetworkSocket():Sock(0)
{

}
bool cNetworkSocket::OpenTCPIPv4(const char *ConectTo,const char* Port)
{
	Close();
	struct addrinfo hints;
	struct addrinfo *result, *rp;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Datagram socket
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          // Any protocol

	int s = getaddrinfo(ConectTo, Port, &hints, &result);
	if (s != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return false;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		Sock = socket(rp->ai_family, rp->ai_socktype,
					 rp->ai_protocol);
		if (Sock == -1)
			continue;

		if (connect(Sock, rp->ai_addr, rp->ai_addrlen) != -1)
			break;                  // Success

		Close();
	}

	freeaddrinfo(result);           // No longer needed

	if (rp == NULL)                 // No address succeeded
	{
		fprintf(stderr, "Could not connect\n");
		return false;
	}
	return true;
}
bool cNetworkSocket::Close()
{
	if(Sock>0)
	{
		close(Sock);
		Sock=0;
		return true;
	}
	else
		return true;
}
cNetworkSocket::~cNetworkSocket()
{
	Close();
}


//char TestBuffer[]="{\"result\":{\"midstate\":\"902b8836b10b8f2fb3bc85b8efae27acab2a80979bcdd65eb4dbfd2d4ef480f0\",\"data\":\"00000002a0ff5d63c5223be0c9d34e519310e5117b0d73d750c9e38a000000110000000094696c8397aa17a056b842b85575cc2a5b8b7c9164aa01505e1b5cdb5b75d55851d354c21a00c94e00000000000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000\",\"hash1\":\"00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000\",\"target\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000\"},\"error\":null,\"id\":\"0\"}";


char TestBuffer[]="{\"result\":{\"midstate\":\"744a765464e6d59c8269b4ba52492c9186e84dd332a90274e03e52e3f7e6ef6b\",\"data\":\"00000002bf0aa87040c3431d1cecb8ed4dbde264980a4b232ba2bb5500000084000000000b5427b41363244d6f0fafe0a1a1d3b2f33fc24396c9ed6d0c41b362f7ddcadf51d4099f1a00c94e00000000000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000\",\"hash1\":\"00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000\",\"target\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000\"},\"error\":null,\"id\":\"0\"}";

const cJSonVar* ParseJSon(char *Buffer)
{
    cJSonVar* obj=new cJSonVar();
    size_t len=strlen(Buffer);
    char *Begin=Buffer;
    const char *res=obj->ParseYourVar(Buffer);
    if(res)
    {
        //printf("Some error near %lu (%20s)\n",res-Begin,res);
        printf("Some JSon error near pos %lu, total len %zu, from text [%.20s]\n",res-Begin,len,res);
        delete obj;
        obj=nullptr;
    }
    return obj;
}

void cJSonVar::PrintSelf(FILE *f)const
{
    if(GetName())
        fprintf(f,"%s=",GetName());
    if(IsVariable())
    {
        fprintf(f,"\"%s\"",GetValue());
    }else
    {
        if(IsNull())
            fprintf(f,"NULL");
        if(IsArray())
        {
            fprintf(f,"(%u)[",GetArrayLen());
            /*for(uint32_t i=0,len=GetArrayLen();i<len;i++)
            {
                fprintf(f,"%s%s",Values[i],i+1==len?"":",");
            }*/
            const cJSonVar *obj=GetInternal();
            while(obj)
            {
                obj->PrintSelf(f);
                obj=obj->GetNext();
                if(obj)
                    fprintf(f,",");
            }
            fprintf(f,"]");
        }
        if(IsHash())
        {
            fprintf(f,"(%u){",GetInternalCount());
            const cJSonVar *obj=GetInternal();
            while(obj)
            {
                obj->PrintSelf(f);
                obj=obj->GetNext();
                if(obj)
                    fprintf(f,",");
            }
            fprintf(f,"}");
        }
        if(IsBool())
        {
            fprintf(f,"%s",GetBoolValue()?"true":"false");
        }
        if(IsInteger())
        {
            fprintf(f,"%d",GetIntValue());
        }
    }
}
const char* cJSonVar::ParseString(char *&Buffer,const char *&Var)
{
    if(*Buffer!='"')
        return Buffer;
    Buffer++;
    Var=Buffer;
    while(*Buffer!='"' && *Buffer!='\0')
    {
        Buffer++;
    }
    if(*Buffer=='"')
    {
        *Buffer='\0';
        Buffer++;
    }
    else
        return Buffer;
    return nullptr;
}
const char* cJSonVar::ParseValue(char *&Buffer)
{
    const char *Var=Buffer;
    while(*Buffer!='\0')
    {
        if(*Buffer==',' || *Buffer==']' || *Buffer=='}')
        {
            char SaveVar=*Buffer;
            *Buffer='\0';
            if(sscanf(Var,"%d",&iValue)!=1)
                return Var;
            *Buffer=SaveVar;
            integerValue=true;
            break;
        }
        Buffer++;
    }
    if(*Buffer=='\0')
        return Buffer;
    return nullptr;
}
const char* cJSonVar::ParseVar(char *&Buffer)
{
    if(*Buffer!='"')
        return Buffer;
    const char* res;
    if((res=ParseString(Buffer,Name)))
        return res;
    Buffer=EatEmpty(Buffer);
    if(*Buffer!=':')
    {
        return Buffer;
    }
    Buffer++;
    Buffer=EatEmpty(Buffer);
    if(*Buffer=='{')
        return ParseHash(Buffer);
    if(*Buffer=='"')
    {
        if((res=ParseString(Buffer,Value)))
            return res;
        return nullptr;
    }
    if(*Buffer=='[')
    {
        if((res=ParseArray(Buffer)))
            return res;
        return nullptr;
    }
    if(strncmp(Buffer,"null",4)==0)
    {
        Buffer+=4;
        Buffer=EatEmpty(Buffer);
        NullValue=true;
        return nullptr;
    }
    if(strncmp(Buffer,"false",5)==0)
    {
        Buffer+=strlen("false");
        Buffer=EatEmpty(Buffer);
        boolValue=true;
        bValue=false;
        return nullptr;
    }
    if(strncmp(Buffer,"true",4)==0)
    {
        Buffer+=strlen("true");
        Buffer=EatEmpty(Buffer);
        boolValue=true;
        bValue=true;
        return nullptr;
    }
    if(*Buffer=='-' || *Buffer=='+' || (*Buffer>='0' && *Buffer<='9'))
    {
        if((res=ParseValue(Buffer)))
            return res;
        return nullptr;
    }
    return Buffer;
}
const char* cJSonVar::ParseArray(char *&Buffer)
{
    CleanInternalArray();
    if(*Buffer!='[')
        return Buffer;
    Buffer++;
    Array=true;
    const char* res;
    /*uint32_t TempValuesSize=16;
    const char **TempValues=nullptr;
    //new (const char*)[TempValuesLen];
    Values=new const char*[TempValuesSize];
    */
    Buffer=EatEmpty(Buffer);
    CleanInternalArray();
    cJSonVar *Tail=nullptr;
    while(*Buffer!=']' && *Buffer!='\0')
    {
        cJSonVar *obj=new cJSonVar();
        obj->SetArrayPos(ArrayLen);
        if(!Tail)
        {
            Tail=obj;
            ArrayLen=1;
            InternalVarHead=obj;
        }else
        {
            Tail->NextVar=obj;
            Tail=obj;
            ArrayLen++;
        }
        if((res=obj->ParseYourArrayValue(Buffer)))
            return res;
        Buffer=EatEmpty(Buffer);
        if(*Buffer==',')
        {
            Buffer++;
            Buffer=EatEmpty(Buffer);
            if(*Buffer=='\0')
                return Buffer;
            continue;
        }
        Buffer=EatEmpty(Buffer);
        if(*Buffer!=']')
            return Buffer;
    }
    Buffer=EatEmpty(Buffer);
    if(*Buffer==']')
    {
        Buffer++;
    }
    else
        return Buffer;
    return nullptr;
}
const char* cJSonVar::ParseHash(char *&Buffer)
{
    if(*Buffer!='{')
        return Buffer;
    Buffer++;
    Hash=true;
    const char* res;
    CleanInternalVars();
    cJSonVar *Tail=nullptr;
    while(*Buffer!='}' && *Buffer!='\0')
    {
        Buffer=EatEmpty(Buffer);
        if(*Buffer!='"')//name var expected
            return Buffer;
        cJSonVar *obj=new cJSonVar();
        if(!Tail)
        {
            Tail=obj;
            InternalVarHead=obj;
            InternalVarCount=1;
        }else
        {
            Tail->NextVar=obj;
            Tail=obj;
            InternalVarCount++;
        }
        if((res=obj->ParseYourVar(Buffer)))
            return res;
        Buffer=EatEmpty(Buffer);
        if(*Buffer==',')
        {
            Buffer++;
            Buffer=EatEmpty(Buffer);
            if(*Buffer=='\0')
                return Buffer;
            continue;
        }
        Buffer=EatEmpty(Buffer);
        if(*Buffer!='}')
            return Buffer;
    }
    Buffer=EatEmpty(Buffer);
    if(*Buffer=='}')
        Buffer++;
    else
        return Buffer;
    return nullptr;
}
const char* cJSonVar::ParseYourVar(char *&Buffer)
{
    Buffer=EatEmpty(Buffer);
    if(*Buffer=='{')
        return ParseHash(Buffer);
    if(*Buffer=='"')
        return ParseVar(Buffer);
    if(*Buffer=='[')
        return ParseArray(Buffer);
    return Buffer;
}
const char* cJSonVar::ParseYourArrayValue(char *&Buffer)
{
    Buffer=EatEmpty(Buffer);
    if(*Buffer=='{')
        return ParseHash(Buffer);
    if(*Buffer=='"')
        return ParseString(Buffer,Value);
    if(*Buffer=='[')
        return ParseArray(Buffer);
    if(strncmp(Buffer,"null",4)==0)
    {
        Buffer+=4;
        Buffer=EatEmpty(Buffer);
        NullValue=true;
        return nullptr;
    }
    if(strncmp(Buffer,"false",5)==0)
    {
        Buffer+=strlen("false");
        Buffer=EatEmpty(Buffer);
        boolValue=true;
        bValue=false;
        return nullptr;
    }
    if(strncmp(Buffer,"true",4)==0)
    {
        Buffer+=strlen("true");
        Buffer=EatEmpty(Buffer);
        boolValue=true;
        bValue=true;
        return nullptr;
    }
    if(*Buffer=='-' || *Buffer=='+' || (*Buffer>='0' && *Buffer<='9'))
    {
        const char *res;
        if((res=ParseValue(Buffer)))
            return res;
        return nullptr;
    }
    return Buffer;
}

cJSonVar::cJSonVar()
    :Name(nullptr),Value(nullptr),ArrayLen(0)
    ,NextVar(nullptr),InternalVarHead(nullptr),InternalVarCount(0)
    ,Array(false),Hash(false),NullValue(false),boolValue(false),bValue(false)
    ,integerValue(false),iValue(0),ArrayPosition(0)
{

}
void cJSonVar::CleanInternalVars()
{
    cJSonVar *obj;
    while(InternalVarHead)
    {
        obj=InternalVarHead;
        InternalVarHead=InternalVarHead->NextVar;
        delete obj;
    }
    InternalVarCount=0;
    ArrayLen=0;
}
void cJSonVar::CleanInternalArray()
{
    cJSonVar *obj;
    while(InternalVarHead)
    {
        obj=InternalVarHead;
        InternalVarHead=InternalVarHead->NextVar;
        delete obj;
    }
    InternalVarCount=0;
    ArrayLen=0;
}
cJSonVar::~cJSonVar()
{
    if(InternalVarHead)
    {
        CleanInternalVars();
    }
    CleanInternalArray();
}
const cJSonVar* cJSonVar::GetByIndex(uint32_t Index)const
{
    const cJSonVar* obj=GetInternal();
    while(obj)
    {
        if(obj->GetIndexPosition()==Index)
            return obj;
        obj=obj->GetNext();
    }
    return nullptr;
}
const cJSonVar* cJSonVar::Search(const char *Name)const
{
    const cJSonVar* obj=GetInternal();
    while(obj)
    {
        if(strcmp(obj->GetName(),Name)==0)
            return obj;
        obj=obj->GetNext();
    }
    return nullptr;
}

bool HexToBuff(const char *Str,uint8_t *res,uint32_t len)
{
    uint32_t Pos=0;
    while(Str[0]!='\0' && Pos<len)
    {
        uint8_t Val=0;
        res[Pos]=0;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val=Str[0]-'A'+10;
        }else
            return false;
        Str++;
        if(Str[0]=='\0')
        {
            res[Pos]=Val;
            return true;
        }
        Val<<=4;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val|=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val|=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val|=Str[0]-'A'+10;
        }else
            return false;
        res[Pos]=Val;
        Str++;
        Pos++;
    }
    return Pos==len;
}

int HexToUINT(const char *Str,uint32_t &Value)
{
    int Pos=1;
    Value=0;
    while(Str[0]!='\0')
    {
        uint8_t Val=0;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val=Str[0]-'A'+10;
        }else
            return -Pos;
        Str++;
        if(Str[0]=='\0')
        {
            Value|=Val;
            return Pos;
        }
        Val<<=4;
        Pos++;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val|=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val|=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val|=Str[0]-'A'+10;
        }else
            return -Pos;
        Value|=Val;
        if(Pos==8)
            return Pos;
        Str++;
        Pos++;
        Value<<=8;
    }
    return Pos;
}
inline uint32_t HexToUINT(const char *Str)
{
    uint32_t v=0;
    HexToUINT(Str,v);
    return v;
}

const char cBase64Encoder::encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
const uint8_t cBase64Encoder::mod_table[] = {0, 2, 1};

cBase64Encoder::cBase64Encoder(const char *Str):ResultStr(nullptr),ResultStrSize(0)
{
    if(Str)
        Encode(Str);
}
cBase64Encoder::~cBase64Encoder()
{
    if(ResultStr)
    {
        free(ResultStr);
        ResultStr=nullptr;
        ResultStrSize=0;
    }
}
const char *cBase64Encoder::Encode(const char *data,size_t input_length)
{
    if(input_length==0)
        input_length=strlen(data);
    size_t output_length = 4 * ((input_length + 2) / 3);
    if(output_length>=ResultStrSize)
    {
        if(ResultStr)
            free(ResultStr);
        ResultStrSize=output_length+1;
        ResultStr = (char*)malloc(ResultStrSize);
    }
    if (ResultStr == NULL) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        ResultStr[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        ResultStr[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        ResultStr[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        ResultStr[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (size_t i = 0; i < mod_table[input_length % 3]; i++)
        ResultStr[output_length - 1 - i] = '=';

    ResultStr[output_length]='\0';

    return ResultStr;
}



class cMineJob
{
public:
    cMineJob(const char *Host,const char *Port,const char *UserName=nullptr,const char *Passwd=nullptr);
    ~cMineJob(){}
    bool GetJob();
    bool GetJobTest();
    bool SendJob();
    bool SendJobTest();
    const uint8_t *GetMidState()const{return MidState;}
    const uint8_t *GetLeftData()const{return OtherData;}
    const char *GetData()const{return hexData;}
    bool SetFoundNonce(uint32_t Nonce);
    const char *GetAuthString()const{return AuthData.GetResult();}
protected:
    char Host[128];
    char Port[64];
    char UserName[64];
    char Passwd[64];
    uint8_t MidState[32];
    uint8_t OtherData[64];
    char hexData[256+4];
    uint32_t FoundNonse;
    cBase64Encoder AuthData;
};


cMineJob::cMineJob(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd)
{
    strncpy(Host,valHost,128);
    strncpy(Port,valPort,64);
    if(valUserName)
        strncpy(UserName,valUserName,64);
    else
        UserName[0]='\0';
    if(valPasswd)
        strncpy(Passwd,valPasswd,64);
    else
        Passwd[0]='\0';
    char Buff[128+8];
    snprintf(Buff,128+8,"%s:%s",UserName,Passwd);
    AuthData.Encode(Buff);
}
inline char GetHexChar(uint8_t v)
{
    if(v<10)
        return '0'+v;
    if(v<16)
        return 'a'+(v-10);
    return '?';
}
void DataToHex(const uint8_t *k,char *Str,uint32_t len,bool needend)
{
    uint32_t i;
    for(i=0;i<len;i++)
    {
        Str[i*2]=GetHexChar(k[i]>>4);
        Str[i*2+1]=GetHexChar(k[i]&0xF);
    }
    if(needend)
        Str[i*2]='\0';
}
//0x9a831da8, 0x2558b6c0, 0x0ab8a1a8, 0xdbe531f0, 0xbd3fa55c, 0xfcdd3acc, 0x02000000, 0x00000000
//0000000000000002cc3addfc5ca53fbdf031e5dba8a1b80ac0b65825a81d839a

bool cMineJob::SetFoundNonce(uint32_t Nonce)
{
    FoundNonse=Nonce;
    //Nonce=SwapEndian(Nonce);
    char *Str=&hexData[128+4*3];
    DataToHex((uint8_t*)&Nonce,Str,4);
    return true;
}
bool cMineJob::SendJob()
{
    cNetworkSocket S;
    if(!S.OpenTCPIPv4(Host,Port))
        return false;
    char Buff1[4096];
    snprintf(Buff1,4095,"{\"method\": \"getwork\", \"params\": [\"%s\"], \"id\":\"1\"}",hexData);

    char Buff[4096*4];
    snprintf(Buff,4096*4,"POST / HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
        "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
        "X-Mining-Hashrate: 7000000\n"
        "Content-Length: %zu\n"
        "User-Agent: testASICBitfuryMiner\n\n%s"
        ,AuthData.GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
        ,strlen(Buff1),Buff1
        );
    int res=S.SendStr(Buff);
    if(res<=0)
        return false;
    res=S.Read(Buff,4095*4);
    if(res<=0)
    {
        return false;
    }
    Buff[res]=0;
    uint32_t i=0;
    printf("Data[%s]\n",&Buff[i]);
    while(Buff[i]!='\0')
    {
        if(memcmp(&Buff[i],"\n\n",2)==0)
        {
            i+=2;
            break;
        }
        if(memcmp(&Buff[i],"\r\n\r\n",4)==0)
        {
            i+=4;
            break;
        }
        i++;
    }
    if(Buff[i]=='\0')
        return false;
    printf("[%s]\n",&Buff[i]);
    const cJSonVar* Head=ParseJSon(&Buff[i]);
    if(!Head)
    {
        return false;
    }
    printf("Result:");
    Head->PrintSelf(stdout);
    fflush(stdout);
    delete Head;
    return true;
}
bool cMineJob::GetJob()
{
    cNetworkSocket S;
    if(!S.OpenTCPIPv4(Host,Port))
        return false;
    const char *Content="{\"method\": \"getwork\", \"params\": [], \"id\":0}";

    char Buff[4096*4];
    snprintf(Buff,4096*4,"POST / HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
        "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
        "X-Mining-Hashrate: 7000000\n"
        "Content-Length: %zu\n"
        "User-Agent: testASICBitfuryMiner\n\n%s"
        ,AuthData.GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
        ,strlen(Content),Content
        );
    int res=S.SendStr(Buff);
    if(res<=0)
        return false;
    res=S.Read(Buff,4095*4);
    if(res<=0)
    {
        return false;
    }
    Buff[res]=0;
    uint32_t i=0;
    while(Buff[i]!='\0')
    {
        if(memcmp(&Buff[i],"\n\n",2)==0)
        {
            i+=2;
            break;
        }
        if(memcmp(&Buff[i],"\r\n\r\n",4)==0)
        {
            i+=4;
            break;
        }
        i++;
    }
    if(Buff[i]=='\0')
        return false;
    printf("[%s]\n",&Buff[i]);
    const cJSonVar* Head=ParseJSon(&Buff[i]);
    if(!Head)
    {
        return false;
    }
    printf("Work:");
    Head->PrintSelf(stdout);
    fflush(stdout);
    const cJSonVar* Result=Head->Search("result");
    if(!Result)
    {
        delete Head;
        return false;
    }
    const cJSonVar* midstate=Result->Search("midstate");
    const cJSonVar* data=Result->Search("data");
    if(!midstate || !midstate->IsVariable() || strlen(midstate->GetValue())!=64 || (!HexToBuff(midstate->GetValue(),MidState,32))
       || !data || !data->IsVariable() || strlen(data->GetValue())!=256 || (!HexToBuff(&data->GetValue()[128],OtherData,64))) //должен быть midstate и он должен быть 64 символа в длину
    {
        delete Head;
        return false;
    }


    //"00000002cd2567672bfcec438351f3f75fc450e118714a6ccb8559c80000006a00000000b0d308be5c6e453d3760599dd1798dc081f73c0234787f5aad4f39d5c9978bad51d6ce611a00c94e9c68e40d";
    //"00000002cd2567672bfcec438351f3f75fc450e118714a6ccb8559c80000006a00000000b0d308be5c6e453d3760599dd1798dc081f73c0234787f5aad4f39d5c9978bad51d6ce611a00c94e00000000";
    //uint32_t *v=(uint32_t*)OtherData;
    //for(uint8_t i=0;i<16;i++)
    //    v[i]=SwapEndian(v[i]);

    strncpy(hexData,data->GetValue(),256+4);

//    uint32_t ho[8];
    uint32_t dd[16];

    HexToBuff(&data->GetValue()[0],(uint8_t*)dd,16*4);

    for(uint8_t i=0;i<16;i++)
        dd[i]=SwapEndian(dd[i]);

    char str[256*4];

    //DataToHex((uint8_t*)dd,str,16*4);

    //SHA256_Full(ho, dd, sha_initial_state);

    memset(str,0,256*4);

    //DataToHex((uint8_t*)ho,str,8*4);

    SHA256_CTX sha256_pass1;

    SHA256_Init(&sha256_pass1);
    // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
    SHA256_Update(&sha256_pass1, dd, 64);

    //hexdump(&((uint8_t*)&sha256_pass1)[0], SHA256_DIGEST_LENGTH);
    DataToHex((uint8_t*)&sha256_pass1,str,8*4);

    printf("my midstate=(%s) their=(%s)\n",str,midstate->GetValue());

    //if(HexToBuff(midstate->GetValue(),MidState,32)
    //OtherData
    delete Head;
    return true;
}

bool cMineJob::SendJobTest()
{
    printf("{\"method\": \"getwork\", \"params\": [%s], \"id\":\"1\"}",hexData);
    return true;
}

bool cMineJob::GetJobTest()
{
    cNetworkSocket S;

                       //"00000002cd2567672bfcec438351f3f75fc450e118714a6ccb8559c80000006a00000000b0d308be5c6e453d3760599dd1798dc081f73c0234787f5aad4f39d5c9978bad51d6ce611a00c94e9c68e40d00000080000000000000000000000000000000000000000000000000000000000000000000000000000000008002000";
    //const char *SomeData="00000002cd2567672bfcec438351f3f75fc450e118714a6ccb8559c80000006a00000000b0d308be5c6e453d3760599dd1798dc081f73c0234787f5aad4f39d5c9978bad51d6ce611a00c94e0000000000000080000000000000000000000000000000000000000000000000000000000000000000000000000000008002000";
                       //"000000017910f60c72adce424cf31cb44bd00b030a076efee8ef1b8700000b6a00000000b9cc7b90a5c066425a650d2d513e92770e3696c14854fb818dca415613259f9f4f2b51461a0cd43f62a2dbbf00000080000000000000000000000000000000000000000000000000000000000000000000000000000000008002000";
    const char *SomeData="000000017910f60c72adce424cf31cb44bd00b030a076efee8ef1b8700000b6a00000000b9cc7b90a5c066425a650d2d513e92770e3696c14854fb818dca415613259f9f4f2b51461a0cd43f0000000000000080000000000000000000000000000000000000000000000000000000000000000000000000000000008002000";

    //uint32_t *v=(uint32_t*)OtherData;
    //for(uint8_t i=0;i<16;i++)
    //    v[i]=SwapEndian(v[i]);

    strncpy(hexData,SomeData,256+4);

    HexToBuff(&SomeData[128],OtherData,64);

//    uint32_t ho[8];
    uint32_t dd[16];

    HexToBuff(SomeData,(uint8_t*)dd,16*4);

    for(uint8_t i=0;i<16;i++)
        dd[i]=SwapEndian(dd[i]);

    char str[256*4];


    HexToBuff(&SomeData[128],OtherData,64);

    memset(str,0,256*4);

    DataToHex(OtherData,str,64);
    printf("my other=(%s)\n",str);

    //SHA256_Full(ho, dd, sha_initial_state);

    memset(str,0,256*4);

    //DataToHex((uint8_t*)ho,str,8*4);

    SHA256_CTX sha256_pass1;

    SHA256_Init(&sha256_pass1);
    // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
    SHA256_Update(&sha256_pass1, dd, 64);

    //hexdump(&((uint8_t*)&sha256_pass1)[0], SHA256_DIGEST_LENGTH);
    memcpy(MidState,(uint8_t*)&sha256_pass1,32);

    DataToHex(MidState,str,8*4);

    printf("my midstate=(%s)\n",str);

    return true;
}



void concat_hex_back(char *to,const char *from)
{
    size_t l1=strlen(to);
    size_t l2=strlen(from);
    if((l2%2)!=0)
    {
        printf("Error must be len div 2\n");
        return;
    }
    for(size_t i=l2;i>0;)
    {
        to[l1++]=from[i-2];
        to[l1++]=from[i-1];
        i-=2;
    }
    to[l1]=0;
}


typedef struct block_header {
        unsigned int    version;
        // dont let the "char" fool you, this is binary data not the human readable version
        unsigned char   prev_block[32];
        unsigned char   merkle_root[32];
        unsigned int    timestamp;
        unsigned int    bits;
        unsigned int    nonce;
} block_header;

// we need a helper function to convert hex to binary, this function is unsafe and slow, but very readable (write something better)
void hex2bin(unsigned char* dest, const char* src)
{
        unsigned int c, pos;
        char buf[3];

        pos=0;
        c=0;
        buf[2] = 0;
        while(c < strlen(src))
        {
                // read in 2 characaters at a time
                buf[0] = src[c++];
                buf[1] = src[c++];
                // convert them to a interger and recast to a char (uint8)
                dest[pos++] = (unsigned char)strtol(buf, NULL, 16);
        }

}

// this function is mostly useless in a real implementation, were only using it for demonstration purposes
void hexdump(unsigned char* data, int len)
{
        int c;

        c=0;
        while(c < len)
        {
                printf("%.2x", data[c++]);
        }
        printf("\n");
}

// this function swaps the byte ordering of binary data, this code is slow and bloated (write your own)
void byte_swap(unsigned char* data, int len) {
        int c;
        unsigned char tmp[len];

        c=0;
        while(c<len)
        {
                tmp[c] = data[len-(c+1)];
                c++;
        }

        c=0;
        while(c<len)
        {
                data[c] = tmp[c];
                c++;
        }
}


void SwapDWORDEndians(uint32_t *k,uint32_t len)
{
    for(uint32_t i=0;i<len;i++)
        k[i]=SwapEndian(k[i]);
}

void test2()
{
    // start with a block header struct
        block_header header;

        // we need a place to store the checksums
        unsigned char hash1[SHA256_DIGEST_LENGTH];
        unsigned char hash2[SHA256_DIGEST_LENGTH];

        // you should be able to reuse these, but openssl sha256 is slow, so your probbally not going to implement this anyway
        SHA256_CTX sha256_pass1, sha256_pass2;


        // we are going to supply the block header with the values from the generation block 0
        header.version =        1;
        hex2bin(header.prev_block,              "0000000000000b6ae8ef1b870a076efe4bd00b034cf31cb472adce427910f60c");
        hex2bin(header.merkle_root,             "13259f9f8dca41564854fb810e3696c1513e92775a650d2da5c06642b9cc7b90");
        header.timestamp =      1328238918;
        header.bits =           437048383;
        header.nonce =          1654840255;


        // the endianess of the checksums needs to be little, this swaps them form the big endian format you normally see in block explorer
        byte_swap(header.prev_block, 32);
        byte_swap(header.merkle_root, 32);

        printf("1:");hexdump((unsigned char*)&header, sizeof(block_header));

        //hex2bin((uint8_t*)&header,"00000002cd2567672bfcec438351f3f75fc450e118714a6ccb8559c80000006a00000000b0d308be5c6e453d3760599dd1798dc081f73c0234787f5aad4f39d5c9978bad51d6ce611a00c94e9c68e40d");
        //hex2bin((uint8_t*)&header,"000000017910f60c72adce424cf31cb44bd00b030a076efee8ef1b8700000b6a00000000b9cc7b90a5c066425a650d2d513e92770e3696c14854fb818dca415613259f9f4f2b51461a0cd43f4bec04d1");//swap(7950e001)//swap(5abb2e01)//swap(d104ec4b)
        //hex2bin((uint8_t*)&header,"00000002636f588c713914d7040e76889313eff89be3e37a90ba46bf0000004300000000c121aca48c224ce4159dc6fd95ccb150a2c195d26cec076fcba26de8f0b42c2e51d997761a00c94e4588a168");
        //hex2bin((uint8_t*)&header,"0000000256a462612f506a830b168b8232b87f45355aad796c6f5c3f0000007300000000476967a202569132d8659fb2c55a70fedc4d8a84693ffb40ffacd863a294293551e12f441a00a429f1072059");

        SwapDWORDEndians((uint32_t*)&header,80/4);

        printf("1:");hexdump((unsigned char*)&header, sizeof(block_header));


                    // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, 64);

        printf("midstate Checksum:\n");
        uint32_t k[8];
        memcpy(k,&sha256_pass1,8*4);
        for(uint8_t i=0;i<8;i++)
        {
            printf("0x%08x,",k[i]);
        }
        printf("\n");
        hexdump((uint8_t*)k, SHA256_DIGEST_LENGTH);

        SHA256_Update(&sha256_pass1, &((unsigned char*)&header)[64], 80-64);
        // this ends the sha256 session and writes the checksum to hash1
        SHA256_Final(hash1, &sha256_pass1);

        byte_swap(hash1, SHA256_DIGEST_LENGTH);
        printf("midstate2 Checksum: ");
        hexdump(hash1, SHA256_DIGEST_LENGTH);

        //char buff2[256];

        //printf("t=%s\n",SHA256_Data((unsigned char*)&header, 64,buff2));


        // dump out some debug data to the terminal
        printf("sizeof(block_header) = %d\n", (int) sizeof(block_header));
        printf("Block header (in human readable hexadecimal representation): ");
        hexdump((unsigned char*)&header, sizeof(block_header));

            // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, sizeof(block_header));
        // this ends the sha256 session and writes the checksum to hash1
        SHA256_Final(hash1, &sha256_pass1);

        // to display this, we want to swap the byte order to big endian
        byte_swap(hash1, SHA256_DIGEST_LENGTH);
        printf("Useless First Pass Checksum: ");
        hexdump(hash1, SHA256_DIGEST_LENGTH);

        // but to calculate the checksum again, we need it in little endian, so swap it back
        byte_swap(hash1, SHA256_DIGEST_LENGTH);

    //same as above
    SHA256_Init(&sha256_pass2);
    SHA256_Update(&sha256_pass2, hash1, SHA256_DIGEST_LENGTH);
    SHA256_Final(hash2, &sha256_pass2);

        byte_swap(hash2, SHA256_DIGEST_LENGTH);
        printf("Target Second Pass Checksum: ");
        hexdump(hash2, SHA256_DIGEST_LENGTH);
}


void test2_1()
{
    // start with a block header struct
        block_header header;

        // we need a place to store the checksums
        unsigned char hash1[SHA256_DIGEST_LENGTH];
        unsigned char hash2[SHA256_DIGEST_LENGTH];

        // you should be able to reuse these, but openssl sha256 is slow, so your probbally not going to implement this anyway
        SHA256_CTX sha256_pass1, sha256_pass2;


        // we are going to supply the block header with the values from the generation block 0
        header.version =        1;
        hex2bin(header.prev_block,              "0000000000000b6ae8ef1b870a076efe4bd00b034cf31cb472adce427910f60c");
        hex2bin(header.merkle_root,             "13259f9f8dca41564854fb810e3696c1513e92775a650d2da5c06642b9cc7b90");
        header.timestamp =      1328238918;
        header.bits =           437048383;
        header.nonce =          1654840255;


        // the endianess of the checksums needs to be little, this swaps them form the big endian format you normally see in block explorer
        byte_swap(header.prev_block, 32);
        byte_swap(header.merkle_root, 32);


        // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, 64);

        printf("midstate Checksum:\n");
        uint32_t k[8];
        memcpy(k,&sha256_pass1,8*4);
        hexdump((uint8_t*)k, SHA256_DIGEST_LENGTH);
        for(uint8_t i=0;i<8;i++)
        {
            printf("0x%08x, ",k[i]);
        }
        printf("\n");
        memcpy(k,&((uint8_t*)&header)[64],4*4);
        for(uint8_t i=0;i<4;i++)
        {
            printf("0x%08x,",k[i]);
        }
        printf("\n");

        SHA256_Update(&sha256_pass1, &((unsigned char*)&header)[64], 80-64);
        // this ends the sha256 session and writes the checksum to hash1
        SHA256_Final(hash1, &sha256_pass1);

        byte_swap(hash1, SHA256_DIGEST_LENGTH);
        printf("midstate2 Checksum: ");
        hexdump(hash1, SHA256_DIGEST_LENGTH);

        //char buff2[256];

        //printf("t=%s\n",SHA256_Data((unsigned char*)&header, 64,buff2));


        // dump out some debug data to the terminal
        printf("sizeof(block_header) = %d\n", (int) sizeof(block_header));
        printf("Block header (in human readable hexadecimal representation): ");
        hexdump((unsigned char*)&header, sizeof(block_header));

            // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, sizeof(block_header));
        // this ends the sha256 session and writes the checksum to hash1
        SHA256_Final(hash1, &sha256_pass1);

        // to display this, we want to swap the byte order to big endian
        byte_swap(hash1, SHA256_DIGEST_LENGTH);
        printf("Useless First Pass Checksum: ");
        hexdump(hash1, SHA256_DIGEST_LENGTH);

        // but to calculate the checksum again, we need it in little endian, so swap it back
        byte_swap(hash1, SHA256_DIGEST_LENGTH);

        //same as above
        SHA256_Init(&sha256_pass2);
        SHA256_Update(&sha256_pass2, hash1, SHA256_DIGEST_LENGTH);
        SHA256_Final(hash2, &sha256_pass2);

        byte_swap(hash2, SHA256_DIGEST_LENGTH);
        printf("Target Second Pass Checksum: ");
        hexdump(hash2, SHA256_DIGEST_LENGTH);

        SwapDWORDEndians((uint32_t*)&header,80/4);
        hexdump((unsigned char*)&header, sizeof(block_header));
}

void test()
{
    char str[256*4];
    const char* s="00000001";
    strcpy(str,s);
    concat_hex_back(str,"0000000000000b6ae8ef1b870a076efe4bd00b034cf31cb472adce427910f60c");
    concat_hex_back(str,"13259f9f8dca41564854fb810e3696c1513e92775a650d2da5c06642b9cc7b90");
    printf("begin=(%s)\n",str);
    uint32_t ho[8];
    uint32_t dd[16];

    HexToBuff(str,(uint8_t*)dd,16*4);

    /*for(uint8_t i=0;i<16;i++)
        dd[i]=SwapEndian(dd[i]);*/

    int fd=open("/root/test.bin",O_WRONLY);
    if(fd>0)
    {
        write(fd,dd,16*4);
        close(fd);
    }else
    {
        printf("create error\n");
    }

    /*for(uint8_t i=0;i<16;i++)
        dd[i]=SwapEndian(dd[i]);*/

    memset(str,0,256*4);

    DataToHex((uint8_t*)dd,str,16*4);
    printf("res  =(%s)\n",str);

    SHA256_Full(ho, dd, sha_initial_state);

    memset(str,0,256*4);

    DataToHex((uint8_t*)ho,str,8*4);

    printf("my midstate=(%s) their=(%s)\n",str,"");
}

int mainOld()
{
    const cJSonVar* Head=ParseJSon(TestBuffer);
    if(!Head)
    {
        getchar();
        return -1;
    }
    Head->PrintSelf(stdout);
    printf("MidStateLen=%zu\n",strlen(Head->Search("result")->Search("midstate")->GetValue()));
    const char *data=Head->Search("result")->Search("data")->GetValue();
    printf("data(%zu)=%s\n",strlen(data),data);
    uint32_t Version;
    if(HexToUINT(&data[0],Version)<0)
        printf("error get version\n");

    printf("Version=%08x\n",SwapEndian(Version));
    printf("PrevBlock=%08x%08x%08x%08x%08x%08x%08x%08x\n"
           ,SwapEndian(HexToUINT(&data[8+8*0])),SwapEndian(HexToUINT(&data[8+8*1])),SwapEndian(HexToUINT(&data[8+8*2])),SwapEndian(HexToUINT(&data[8+8*3]))
           ,SwapEndian(HexToUINT(&data[8+8*4])),SwapEndian(HexToUINT(&data[8+8*5])),SwapEndian(HexToUINT(&data[8+8*6])),SwapEndian(HexToUINT(&data[8+8*7])));
    printf("MerkleRoot=%08x%08x%08x%08x%08x%08x%08x%08x\n"
           ,SwapEndian(HexToUINT(&data[72+8*0])),SwapEndian(HexToUINT(&data[72+8*1])),SwapEndian(HexToUINT(&data[72+8*2])),SwapEndian(HexToUINT(&data[72+8*3]))
           ,SwapEndian(HexToUINT(&data[72+8*4])),SwapEndian(HexToUINT(&data[72+8*5])),SwapEndian(HexToUINT(&data[72+8*6])),SwapEndian(HexToUINT(&data[72+8*7])));

    delete Head;

    getchar();

    return 0;

	printf("Starting...");
	fflush(stdout);
    cNetworkSocket S;
    if(!S.OpenTCPIPv4("pit.deepbit.net","8332"))
        return 0;
    printf("Done.\n");
    const char *Content="{\"method\": \"getwork\", \"params\": [], \"id\":0}";
    char Buff[4096*4];
    snprintf(Buff,4096*4,"POST / HTTP/1.1\nAuthorization: Basic %s\nHost: pit.deepbit.net:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
        "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
        "X-Mining-Hashrate: 7000000\n"
        "Content-Length: %zu\n"
        "User-Agent: testASICBitfuryMiner\n\n%s","am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ==",strlen(Content),Content);
    S.SendStr(Buff);
    //printf("Send[%s]\n",Buff);
    int res=S.Read(Buff,4095*4);
    if(res>0)
    {
        Buff[res]=0;
        printf("[%s]\n",Buff);
    }else
    {
        printf("Nothing get\n");
    }
    getchar();
	return 0;
}


void TestKeepAlive()
{
    cHTTPConection con("pit.deepbit.net","8332","jman.box@gmail.com_test","1");
    //cHTTPConection con("195.74.67.253","80","jman.box@gmail.com_test","1");

    const char *Content="{\"method\": \"getwork\", \"params\": [], \"id\":0}";

    char Buff[4096*4];
    int i=0;
    while(true)
    {
        snprintf(Buff,4096*4,"POST /listenChannel HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
            "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
            "X-Mining-Hashrate: 7000000\n"
            "Content-Length: %zu\n"
            "User-Agent: testASICBitfuryMiner\n\n%s"
            ,con.GetAuthBase64Encoder().GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
            ,strlen(Content),Content
            );
        if(!con.SendNewRequest(Buff))
        {
            printf("error send request");
        }else
        {
            printf("Data sended.\n");
            while(con.IsConnectionPresent())
            {
                //some sleep or poll descriptor
                if(con.OnProcessData())
                    break;
            }
            printf("procesing finished\n");
            if(con.IsParsingFinished())
            {
                printf("Result - code:%u http:%u.%u [%s] Header vars:%u\n",con.GetHTTPResultCode(),con.GetHTTPVersionHi(),con.GetHTTPVersionLo(),con.GetHTTPResultText(),con.GetHeaderVarsCount());
                const cHTTPVar* Var=con.GetFirstHeaderVar();
                while(Var)
                {
                    printf("%s[%s]\n",Var->GetName(),Var->GetValue());
                    Var=Var->GetNext();
                }
                memcpy(Buff,con.GetResultData(),con.GetResultDataSize());
                Buff[con.GetResultDataSize()]='\0';
                printf("==[%s]\n",con.GetResultData());
            }else
            {
                printf("but parsing not finished\n");
            }
        }
        i++;
        getchar();
        if(i%4==0)
        {
            printf("Closing connection :)\n");
            con.GetSock().Close();
        }
    }
    exit(0);
}


cSearchNonceJob::cSearchNonceJob():FoundNoncesCount(0),Next(nullptr)
{

}
cSearchNonceJob::~cSearchNonceJob()
{
    Next=nullptr;
}
bool cSearchNonceJob::SetFoundNonce(uint32_t Nonce)
{
    if(FoundNoncesCount<MaxFoundNonces)
    {
        FoundNonces[FoundNoncesCount++]=Nonce;
    }
    return true;
}
cFoundNonceJob::cFoundNonceJob():Next(nullptr)
{

}
cFoundNonceJob::~cFoundNonceJob()
{
    Next=nullptr;
}

#include "test_cases.cpp"

class cTestToolSearchNonceJob: public cSearchNonceJob
{
public:
	static cSearchNonceJob* Create() {
		return new cTestToolSearchNonceJob();
	}
	
	cTestToolSearchNonceJob() {
		static volatile uint32_t dataIndexGlobal = 0;
		uint32_t offset = dataIndexGlobal * 12;
		dataIndexGlobal = (dataIndexGlobal + 1) % g_testToolDataNum;
		
		memcpy(&MidState[0],&g_testToolData[offset + 0],32);
		memcpy(&OtherData[0],&g_testToolData[offset + 8],16);
	}
	
	void OnJobFinished() {
		delete this;
	}
	void OnJobCanceled() {
		delete this;
	}
};

cNonceJobsPull::cNonceJobsPull():MaxJobCount(4),FirstJob(nullptr),LastJob(nullptr),JobCount(0)
    ,FirstNonce(nullptr),LastNonce(nullptr),NonceCount(0)
{

}
cNonceJobsPull::~cNonceJobsPull()
{
    ClearJobs();
}
void cNonceJobsPull::ClearJobs()
{
    {
        ENTER_ONE_THREAD(Mutex);
        while(FirstJob)
        {
            cSearchNonceJob *Job=FirstJob;
            FirstJob=FirstJob->GetNext();
            Job->SetNext(nullptr);
            JobCount--;
            if(!FirstJob)
                LastJob=nullptr;
            Mutex.UnLock();
            Job->OnJobCanceled();
            Mutex.Lock();
        }
    }
    OnNeedMoreJobs();
}
cSearchNonceJob* cNonceJobsPull::GetNextJob()
{
	if (isTestDataSource()) {
		return cTestToolSearchNonceJob::Create();
	}
	
    bool NeedMoreJobs=false;
    cSearchNonceJob* Obj=nullptr;
    {
        ENTER_ONE_THREAD(Mutex);
        Obj=FirstJob;
        if(FirstJob)
        {
            FirstJob=FirstJob->GetNext();
            Obj->SetNext(nullptr);
            JobCount--;
            if(!FirstJob)
                LastJob=nullptr;
        }
        NeedMoreJobs=JobCount<MaxJobCount;
    }
    if(NeedMoreJobs)
        OnNeedMoreJobs();
    return Obj;
}
void cNonceJobsPull::SetMaxJobCount(uint16_t NewCount)
{
	if (isTestDataSource()) return;
	
    ENTER_ONE_THREAD(Mutex);
    if(MaxJobCount!=NewCount)
    {
        MaxJobCount=NewCount;
        if(JobCount<MaxJobCount)
            OnNeedMoreJobs();
    }
}
bool cNonceJobsPull::AddJob(cSearchNonceJob* Job)
{
    ENTER_ONE_THREAD(Mutex);
    if(!LastJob)
    {
        FirstJob=LastJob=Job;
    }else
    {
        LastJob->SetNext(Job);
        LastJob=Job;
    }
    Job->SetNext(nullptr);
    JobCount++;
    //printf("Add new job now(%u)\n",JobCount);
    return true;
}

void cNonceJobsPull::ClearNonce()
{
    ENTER_ONE_THREAD(Mutex);
    while(FirstNonce)
    {
        cFoundNonceJob *Job=FirstNonce;
        FirstNonce=FirstNonce->GetNext();
        Job->SetNext(nullptr);
        NonceCount--;
        if(!FirstNonce)
            LastNonce=nullptr;
        Mutex.UnLock();
        Job->OnCanceled();
        Mutex.Lock();
    }
}
cFoundNonceJob* cNonceJobsPull::GetNextNonce()
{
    ENTER_ONE_THREAD(Mutex);
    cFoundNonceJob* Obj=FirstNonce;
    if(FirstNonce)
    {
        FirstNonce=FirstNonce->GetNext();
        Obj->SetNext(nullptr);
        NonceCount--;
        if(!FirstNonce)
            LastNonce=nullptr;
    }
    return Obj;
}
void cNonceJobsPull::OnNeedMoreJobs()
{
}
void cNonceJobsPull::OnHaveNewNonce()
{
}
bool cNonceJobsPull::AddNonce(cFoundNonceJob* Job)
{
    {
        ENTER_ONE_THREAD(Mutex);
        if(!LastNonce)
        {
            FirstNonce=LastNonce=Job;
        }else
        {
            LastNonce->SetNext(Job);
            LastNonce=Job;
        }
        Job->SetNext(nullptr);
        NonceCount++;
    }
    OnHaveNewNonce();
    return true;
}
bool cNonceJobsPull::AddNonceToHead(cFoundNonceJob* Job)
{
    {
        ENTER_ONE_THREAD(Mutex);
        if(!FirstNonce)
        {
            FirstNonce=LastNonce=Job;
            Job->SetNext(nullptr);
        }else
        {
            Job->SetNext(FirstNonce);
            FirstNonce=Job;
        }
        NonceCount++;
    }
    OnHaveNewNonce();
    return true;
}

void cGetWorkJobsPull::ClosePlayFile()
{
    if(PlayFile)
    {
        fclose(PlayFile);
        PlayFile=nullptr;
    }
}
bool cGetWorkJobsPull::SaveAllJobsToFile(FILE *fVal)
{
    ClosePlayFile();
    PlayFile=fVal;
    Save=true;
    return true;
}
bool cGetWorkJobsPull::PlayAllJobsFromFile(FILE *fVal)
{
    ClosePlayFile();
    PlayFile=fVal;
    Save=false;
    return true;
}

cGetWorkJobsPull::cGetWorkJobsPull(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd)
    :ListenConnection(nullptr),WorkConnections(nullptr),Host(valHost,true),Port(valPort?valPort:"80",true)
    ,UserName(valUserName?valUserName:"",true),UserPwd(valPasswd?valPasswd:"")
{
    PlayFile=nullptr;
    Save=true;
    MaxJobCount=8;
    Working=false;
    WorkConnectionCount=8;
    PoolAcceptedShares=0;
}
void cGetWorkJobsPull::OnHaveNewNonce()
{
    Thread.SendToThread('N');
}
void cGetWorkJobsPull::OnNeedMoreJobs()
{
    Thread.SendToThread('W');
}

cGetWorkJobsPull::~cGetWorkJobsPull()
{
    Stop();
    ClearAllHTTPConnection();
    ClosePlayFile();
}
cFoundGetWorkNonceJob::cFoundGetWorkNonceJob(const char *hexString)
{
    strncpy(hexData,hexString,256+4);
}
cFoundGetWorkNonceJob::~cFoundGetWorkNonceJob()
{

}
void cFoundGetWorkNonceJob::OnFinished()
{
    delete this;
}
void cFoundGetWorkNonceJob::OnCanceled()
{
    delete this;
}
bool cGetWorkJobsPull::Run()
{
    if (isTestDataSource()) return true;
	
    {
        ENTER_ONE_THREAD(HTTPMutex);
        if(Working)
            return false;
        Working=true;
    }
    Thread.RunThread(this);
    return true;
}
bool cGetWorkJobsPull::Stop()
{
    {
        ENTER_ONE_THREAD(HTTPMutex);
        if(!Working)
            return false;
    }
    Thread.StopThread();
    {
        ENTER_ONE_THREAD(HTTPMutex);
        Working=false;
    }
    return true;
}
bool cGetWorkJobsPull::CreateAllHTTPConnection()
{
    ENTER_ONE_THREAD(HTTPMutex);
    ClearAllHTTPConnection();
    ListenConnection=new cHTTPConection(Host,Port,UserName,UserPwd);
    WorkConnections=new cHTTPConection*[WorkConnectionCount];
    for(uint16_t i=0;i<WorkConnectionCount;i++)
        WorkConnections[i]=new cHTTPConection(Host,Port,UserName,UserPwd);
    return true;
}
bool cGetWorkJobsPull::ClearAllHTTPConnection()
{
    ENTER_ONE_THREAD(HTTPMutex);
    if(ListenConnection)
    {
        delete ListenConnection;
        ListenConnection=nullptr;
    }
    if(WorkConnections)
    {
        for(uint16_t i=0;i<WorkConnectionCount;i++)
            if(WorkConnections[i])
            {
                delete WorkConnections[i];
                WorkConnections[i]=nullptr;
            }
        delete []WorkConnections;
        WorkConnections=nullptr;
    }
    return true;
}
bool cGetWorkJobsPull::SendListenRequest(cHTTPConection &con)
{
    if(con.IsWaitingForResult())
        return true;
    if(LongPullChannel.IsEmpty())
        return false;
    const char *Content="{\"method\": \"getwork\", \"params\": [], \"id\":0}";

    char Buff[4096];
    snprintf(Buff,4096,"POST %s HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
            "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
            "X-Mining-Hashrate: 7000000\n"
            "Content-Length: %zu\n"
            "User-Agent: %s\n\n%s"
            ,LongPullChannel.GetText()
            ,con.GetAuthBase64Encoder().GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
            ,strlen(Content),"testASICBitfuryMiner",Content
            );
    if(!con.SendNewRequest(Buff))
    {
        printf("Send new listen request failed!!!\n");
        return false;
    }
    printf("Send new listen request\n");
    return true;
}
bool cGetWorkJobsPull::SendWorkSubmit(cHTTPConection &con,const char *hexData)
{
    if(PlayFile && Save==false)
    {
        return true;
    }
    if(con.IsWaitingForResult())
        return true;
    char Buff1[4096];
    //printf("hexData=[%s]\n",hexData);

    snprintf(Buff1,4095,"{\"method\": \"getwork\", \"params\": [\"%s\"], \"id\":\"1\"}",hexData);

    char Buff[4096*2];
    snprintf(Buff,4096*2,"POST / HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
            "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
            "X-Mining-Hashrate: 7000000\n"
            "Content-Length: %zu\n"
            "User-Agent: %s\n\n%s"
            ,con.GetAuthBase64Encoder().GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
            ,strlen(Buff1),"testASICBitfuryMiner",Buff1
            );
    //printf("Submit[%s]\n",Buff);
    if(!con.SendNewRequest(Buff))
    {
        printf("Send submit work failed\n");
        return false;
    }
    return true;
}
bool cGetWorkJobsPull::SendGetWork(cHTTPConection &con)
{
    if(PlayFile && !Save)//load from file
    {
        char Buff[4096];
        if(fgets(Buff,4096,PlayFile))
        {
            CreateNewJob(Buff);
        }


        return false;
    }

    if(con.IsWaitingForResult())
        return true;
    const char *Content="{\"method\": \"getwork\", \"params\": [], \"id\":0}";

    char Buff[4096];
    snprintf(Buff,4096,"POST / HTTP/1.1\nAuthorization: Basic %s\nHost: localhost:8332\nAccept: */*\nAccept-Encoding: deflate\nContent-type: application/json\n"
            "X-Mining-Extensions: longpoll midstate rollntime submitold\n"
            "X-Mining-Hashrate: 7000000\n"
            "Content-Length: %zu\n"
            "User-Agent: %s\n\n%s"
            ,con.GetAuthBase64Encoder().GetResult()  //"am1hbi5ib3hAZ21haWwuY29tX3Rlc3Q6MQ=="
            ,strlen(Content),"testASICBitfuryMiner",Content
            );
    if(!con.SendNewRequest(Buff))
    {
        printf("Send get work failed\n");
        return false;
    }
    return true;
}
#define CON_STATE_NONE 0
#define CON_STATE_SUBMIT_WORK 1
#define CON_STATE_GET_WORK 2
void cGetWorkJobsPull::ThreadFunc()
{
    CreateAllHTTPConnection();
    pollfd *p=new pollfd[WorkConnectionCount+2];
    cHTTPConection **con=new cHTTPConection*[WorkConnectionCount+1];
    //bool *WasGetWork=new bool[WorkConnectionCount+1];
    //memset(WasGetWork,0,sizeof(WasGetWork[0])*(WorkConnectionCount+1));
    uint16_t NowGetWorkinProgress=0;
    bool NeedExit=false;
    while(!NeedExit)
    {
        uint16_t ConCount=0;
        uint16_t i;
        memset(p,0,sizeof(p[0])*(WorkConnectionCount+1));
        memset(con,0,sizeof(con[0])*(WorkConnectionCount+1));
        {
            ENTER_ONE_THREAD(HTTPMutex);
            if(ListenConnection)
            {
                SendListenRequest(*ListenConnection);
                if(ListenConnection->IsConnectionPresent())
                {
                    con[ConCount]=ListenConnection;
                    p[ConCount].fd=ListenConnection->GetSock().GetSocketFD();
                    p[ConCount].events=POLLIN;
                    ConCount++;
                }
            }
            while(true)
            {
                cFoundNonceJob *Obj=GetNextNonce();
                if(!Obj)
                    break;
                cFoundGetWorkNonceJob *NonceData=static_cast<cFoundGetWorkNonceJob*>(Obj);
                for(i=0;i<WorkConnectionCount;i++)
                {
                    if(!WorkConnections[i]->IsWaitingForResult())
                    {
                        //WorkConnections[i]->GetSock().Close();
                        if(SendWorkSubmit(*WorkConnections[i],NonceData->GetHexDataString()))
                        {
                            WorkConnections[i]->SetUserStateValue(CON_STATE_SUBMIT_WORK);
                            Obj->OnFinished();
                            break;
                        }
                    }
                }
                if(i==WorkConnectionCount)
                {
                    AddNonceToHead(Obj);
                    break;
                }
            }
			for(i=0;i<WorkConnectionCount; i++)
			{
			    if(GetNeedJobCount()>NowGetWorkinProgress && !WorkConnections[i]->IsWaitingForResult())
                {
                    if(SendGetWork(*WorkConnections[i]))
                    {
                        WorkConnections[i]->SetUserStateValue(CON_STATE_GET_WORK);
                        NowGetWorkinProgress++;
                    }
                }
				if(WorkConnections[i]->IsConnectionPresent())
				{
					con[ConCount]=WorkConnections[i];
                    p[ConCount].fd=WorkConnections[i]->GetSock().GetSocketFD();
                    p[ConCount].events=POLLIN;
                    ConCount++;
				}
			}
        }
        p[ConCount].fd=Thread.GetThreadPartPipe().GetDescriptor();
        p[ConCount].events=POLLIN;
        ConCount++;
        //printf("Poll %u\n",ConCount);
        while(poll(p,ConCount,1000)<0){ }

        {
            ENTER_ONE_THREAD(HTTPMutex);
            for(i=0;i<ConCount;i++)
            {
                if(p[i].revents&POLLIN)
                {
                    //printf("input pipe event ");
                    if(p[i].fd==Thread.GetThreadPartPipe().GetDescriptor())
                    {
                        if(Thread.CheckNextThreadCommandForExit())
                        {
                            NeedExit=true;
                            Thread.SendThreadCommandExitAsk();
                        }
                    }else
                    {
                        //printf("On process data\n");
                        if(con[i]->OnProcessData())
                        {
                            if(con[i]==ListenConnection)
                            {
                                if(con[i]->IsParsingFinished())
                                    OnNewLongPollWorkCome(*con[i]);
                            }else
                            {
                                if(con[i]->GetUserStateValue()==CON_STATE_GET_WORK)
                                {
                                    NowGetWorkinProgress--;
                                    if(con[i]->IsParsingFinished())
                                    {
                                        //get new job
                                        OnNewWorkCome(*con[i]);
                                    }
                                }else if(con[i]->GetUserStateValue()==CON_STATE_SUBMIT_WORK)
                                {
                                    //was submit job
                                    OnSubmitWorkCome(*con[i]);
                                }
                            }
                            con[i]->SetUserStateValue(CON_STATE_NONE);
                        }
                    }
                }
            }
        }
    }
    delete []p;
    delete []con;
    //delete []WasGetWork;
}

cSearchGetWorkJob::cSearchGetWorkJob(cGetWorkJobsPull *OwnerObj,const uint8_t *valMidState,const uint8_t* Other,const char *hexString)
:Owner(OwnerObj)
{
    memcpy(MidState,valMidState,32);
    memcpy(OtherData,Other,64);
    strncpy(hexData,hexString,256+4);
}
cSearchGetWorkJob::~cSearchGetWorkJob()
{

}
bool cSearchGetWorkJob::SetFoundNonce(uint32_t Nonce)
{
    //printf("Found nonce 0x%08X\n",Nonce);

    uint32_t FoundNonse=Nonce;
    //Nonce=SwapEndian(Nonce);
    char *Str=&hexData[128+4*3*2];
    DataToHex((uint8_t*)&Nonce,Str,4);

    Owner->AddNonce(new cFoundGetWorkNonceJob(hexData));
    return cSearchNonceJob::SetFoundNonce(FoundNonse);
}
void cSearchGetWorkJob::OnJobFinished()
{
    delete this;
}
void cSearchGetWorkJob::OnJobCanceled()
{
    delete this;
}

bool cGetWorkJobsPull::CreateNewJob(char *Buff)
{
    if(PlayFile && Save)
    {
        fprintf(PlayFile,"%s\n",Buff);
    }
    const cJSonVar* Head=ParseJSon(Buff);
    if(!Head)
    {
        printf("Error parse JSON on left [%s]\n",Buff);
        return false;
    }
    //Head->PrintSelf(stdout);
    //fflush(stdout);
    const cJSonVar* Result=Head->Search("result");
    if(!Result)
    {
        delete Head;
        return false;
    }
    const cJSonVar* midstate=Result->Search("midstate");
    const cJSonVar* data=Result->Search("data");
    uint8_t MidState[32];
    uint8_t OtherData[64];
    if(!midstate || !midstate->IsVariable() || strlen(midstate->GetValue())!=64 || (!HexToBuff(midstate->GetValue(),MidState,32))
       || !data || !data->IsVariable() || strlen(data->GetValue())!=256 || (!HexToBuff(&data->GetValue()[128],OtherData,64))) //должен быть midstate и он должен быть 64 символа в длину
    {
        delete Head;
        return false;
    }

    uint32_t dd[16];
    HexToBuff(data->GetValue(),(uint8_t*)dd,16*4);
    for(uint8_t i=0;i<16;i++)
        dd[i]=SwapEndian(dd[i]);
    SHA256_CTX sha256_pass1;
    SHA256_Init(&sha256_pass1);
    // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
    SHA256_Update(&sha256_pass1, dd, 64);
    if(memcmp(MidState,(uint8_t*)&sha256_pass1,32)!=0)
    {
        char str[256+4];
        //hexdump(&((uint8_t*)&sha256_pass1)[0], SHA256_DIGEST_LENGTH);
        DataToHex((uint8_t*)&sha256_pass1,str,8*4);
        printf("my midstate=(%s) their=(%s)\n",str,midstate->GetValue());
        memcpy(MidState,(uint8_t*)&sha256_pass1,32);
    }

    cSearchGetWorkJob *Obj=new cSearchGetWorkJob(this,MidState,OtherData,data->GetValue());

    //printf("JOB:[%s]\n",Obj->GetHexString());

    if(!AddJob(Obj))
        Obj->OnJobCanceled();

    delete Head;
    return true;
}
bool cGetWorkJobsPull::OnNewLongPollWorkCome(cHTTPConection &con)
{
    ClearJobs();
    char buffer[4096*2];
    if(con.GetResultDataSize()>=4096*2)
    {
        printf("Come too long long-pull result\n");
        return false;
    }
    printf("long pull...\n");
    memcpy(buffer,con.GetResultData(),con.GetResultDataSize());
    buffer[con.GetResultDataSize()]=0;

    return CreateNewJob(buffer);
}
bool cGetWorkJobsPull::OnNewWorkCome(cHTTPConection &con)
{
    char buffer[4096*2];
    if(con.GetResultDataSize()>=4096*2)
    {
        printf("Come too long getwork\n");
        return false;
    }
    //con.DumpHeaderVars();
    const cHTTPVar* poll=con.SearchHeaderVar("X-Long-Polling");
    if(poll && poll->GetValue()[0]=='/' && LongPullChannel!=poll->GetValue())//accept only long poll url to current getwork server
    {
        LongPullChannel=poll->GetValue();
        printf("Select long pull url to [%s]\n",LongPullChannel.GetText());
    }
    //printf("new getwork...\n");
    memcpy(buffer,con.GetResultData(),con.GetResultDataSize());
    buffer[con.GetResultDataSize()]=0;

    //printf("Data size=%u [%s]\n",con.GetResultDataSize(),buffer);

    return CreateNewJob(buffer);
}
bool cGetWorkJobsPull::OnSubmitWorkCome(cHTTPConection &con)
{
    char buffer[4096*2];
    if(con.GetResultDataSize()>=4096*2)
    {
        printf("Come too long submit work\n");
        return false;
    }
    memcpy(buffer,con.GetResultData(),con.GetResultDataSize());
    buffer[con.GetResultDataSize()]=0;

    //"{\"result\":null,\"error\":{\"code\":-1,\"message\":\"error\"},\"id\":\"1\"}"
    char *Buff=buffer;
    //printf("Parse data:[%s]\n",Buff);
    const cJSonVar* Head=ParseJSon(Buff);
    if(!Head)
    {
        printf("Error parse submit JSON on left [%s]\n",Buff);
        return false;
    }
    //Head->PrintSelf(stdout);
    //fflush(stdout);
    const cJSonVar* Result=Head->Search("result");
    const cJSonVar* Error=Head->Search("error");
    if(!Result)
    {
        delete Head;
        return false;
    }
    const cJSonVar* ErrorMesasge=nullptr;
    if(Error)
    {
        ErrorMesasge=Error->Search("message");
    }
    if(Result->IsNull())
    {
        printf("Result is NULL for submit work (%s)\n",ErrorMesasge?ErrorMesasge->GetValue():"?no error val?");
        delete Head;
        return false;
    }
    if(!Result->IsBool())
    {
        printf("Result is not bool for submit work (%s)\n",ErrorMesasge?ErrorMesasge->GetValue():"?no error val?");
        delete Head;
        return false;
    }
    if(!Result->GetBoolValue())
        printf("Nonce submit to pool is %s\n",Result->GetBoolValue()?"ok":"failed");
    else
        PoolAcceptedShares++;

    delete Head;
    return true;
}

void TestPullJobObject()
{
    cGetWorkJobsPull obj("pit.deepbit.net","8332","jman.box@gmail.com_test","1");
    //cGetWorkJobsPull obj("api.bitcoin.cz","8332","ihor.worker3","1");
    obj.Run();
    sleep(1);
    bool exit=false;
    uint32_t c=1;
    while(!exit)
    {
        cSearchNonceJob *Obj=obj.GetNextJob();
        if(Obj)
        {
            /*if(c%10==0)
            {
                cSearchGetWorkJob *Obj1=static_cast<cSearchGetWorkJob*>(Obj);

                printf("hexData:[%s]\n",Obj1->GetHexString());
                Obj->SetFoundNonce(1456345);
            }*/
            Obj->SetFoundNonce(1456345);
            Obj->OnJobFinished();
        }
        printf("Job %s left %u\n",Obj?"present":"NONE",obj.GetJobCount());
        sleep(5);
        c++;
    }
    obj.Stop();
    printf("Finished\n");
    getchar();
//    exit(0);
}

void SaveGetWorkTestVectors()
{
    cGetWorkJobsPull obj("pit.deepbit.net","8332","jman.box@gmail.com_test","1");
    //cGetWorkJobsPull obj("api.bitcoin.cz","8332","ihor.worker3","1");
    FILE *f=fopen("testVectorsData.txt","w");
    if(!f)
    {
        printf("Error open file testVectorsData.txt!\n");
        exit(0);
    }
    obj.SaveAllJobsToFile(f);
    obj.Run();
    sleep(3);
    bool exit=false;
    uint32_t c=1;
    while(!exit)
    {
        cSearchNonceJob *Obj=obj.GetNextJob();
        if(Obj)
        {
            /*if(c%10==0)
            {
                cSearchGetWorkJob *Obj1=static_cast<cSearchGetWorkJob*>(Obj);

                printf("hexData:[%s]\n",Obj1->GetHexString());
                Obj->SetFoundNonce(1456345);
            }*/
            //Obj->SetFoundNonce(1456345);
            Obj->OnJobFinished();
        }
        printf("Job %s left %u\n",Obj?"present":"NONE",obj.GetJobCount());
        //sleep(1);
        usleep(1000*200);
        c++;
        if(c==100)
        {
            break;
        }
    }
    obj.Stop();
    printf("Finished\n");
    getchar();
//    exit(0);
}

void PlayGetWorkTestVectors()
{
    cGetWorkJobsPull obj("pit.deepbit.net","8332","jman.box@gmail.com_test","1");
    //cGetWorkJobsPull obj("api.bitcoin.cz","8332","ihor.worker3","1");
    FILE *f=fopen("testVectorsData.txt","r");
    if(!f)
    {
        printf("Error open file testVectorsData.txt!\n");
        exit(0);
    }
    obj.PlayAllJobsFromFile(f);
    obj.Run();
    sleep(3);
    bool exit=false;
    uint32_t c=1;
    while(!exit)
    {
        cSearchNonceJob *Obj=obj.GetNextJob();
        if(Obj)
        {
            /*if(c%10==0)
            {
                cSearchGetWorkJob *Obj1=static_cast<cSearchGetWorkJob*>(Obj);

                printf("hexData:[%s]\n",Obj1->GetHexString());
                Obj->SetFoundNonce(1456345);
            }*/
            //Obj->SetFoundNonce(1456345);
            Obj->OnJobFinished();
        }else
        {
            break;
        }
        printf("Job %s left %u\n",Obj?"present":"NONE",obj.GetJobCount());
        //sleep(1);
        usleep(1000*100);
        c++;
    }
    obj.Stop();
    printf("Finished %u\n",c);
    getchar();
//    exit(0);
}


inline cByteBufferType operator+(const char *str,const cByteBufferType& obj)
{
    cByteBufferType k(str);
    k+=obj;
    return k;
}
inline cByteBufferType operator+(const cByteBufferType& obj1,const cByteBufferType& obj2)
{
    cByteBufferType k(obj1);
    k+=obj2;
    return k;
}

void cByteBufferType::SwapDWORDEndians()
{
    if(Len%4)
    {
        printf("error length for SwapDWORDEndians\n");
        //for(uint32_t i=0,l=4-Len%4;i<l;i++)
        //    Buffer[Len++]=0;
        return;
    }
    uint32_t *v=(uint32_t*)Buffer;
    for(uint32_t i=0,l=Len/4;i<l;i++)
    {
        v[i]=SwapEndian(v[i]);
    }
}
void cByteBufferType::swap_array()
{
    uint8_t k;
    uint32_t l=Len/2;
    for(uint32_t i=0;i<l;i++)
    {
        k=Buffer[i];
        Buffer[i]=Buffer[Len-i-1];
        Buffer[Len-i-1]=k;
    }
}

uint32_t cByteBufferType::HexToBuff(const char *Str,uint8_t *res,uint32_t len)
{
    uint32_t Pos=0;
    while(Str[0]!='\0' && Pos<len)
    {
        uint8_t Val=0;
        res[Pos]=0;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val=Str[0]-'A'+10;
        }else
            return Pos;
        Str++;
        if(Str[0]=='\0')
        {
            res[Pos]=Val;
            return Pos;
        }
        Val<<=4;
        if(Str[0]>='0' && Str[0]<='9')
        {
            Val|=Str[0]-'0';
        }else if(Str[0]>='a' && Str[0]<='f')
        {
            Val|=Str[0]-'a'+10;
        }else if(Str[0]>='A' && Str[0]<='F')
        {
            Val|=Str[0]-'A'+10;
        }else
            return Pos;
        res[Pos]=Val;
        Str++;
        Pos++;
    }
    return Pos;
}

void cByteBufferType::PrepareBuffer(uint32_t S)
{
    if(S==0)//need same size like now
    {
        S=Len;
    }
    if(S==0)
    {
        FreeBuffer();
        return;
    }
    if(Size<S)
    {
        Size=CalculateBufferSize(S);
        if(Buffer)
        {
            Buffer=(uint8_t*)realloc(Buffer,Size*sizeof(Buffer[0]));
        }else
        {
            Buffer=(uint8_t*)malloc(Size*sizeof(Buffer[0]));
        }
    }
}

void TestStratumCalculation();

void test_bitcoin(const char *str)
{
    // start with a block header struct
        block_header header;

        // we need a place to store the checksums
        unsigned char hash1[SHA256_DIGEST_LENGTH];
        unsigned char hash2[SHA256_DIGEST_LENGTH];

        // you should be able to reuse these, but openssl sha256 is slow, so your probbally not going to implement this anyway
        SHA256_CTX sha256_pass1, sha256_pass2;


        // we are going to supply the block header with the values from the generation block 0
        header.version =        2;
        hex2bin(header.prev_block,              "00000000440b921e1b77c6c0487ae5616de67f788f44ae2a5af6e2194d16b6f8");
        hex2bin(header.merkle_root,             "ec9d69b1c30dd91529e2f5a5636354a310a79b9b83622cfd79c8da9daa4d4132");
        header.timestamp =      1347323629;
        header.bits =           472564911;
        header.nonce =          2996141058;


        // the endianess of the checksums needs to be little, this swaps them form the big endian format you normally see in block explorer
        byte_swap(header.prev_block, 32);
        byte_swap(header.merkle_root, 32);

        hex2bin((uint8_t*)&header,str);

        //SwapDWORDEndians((uint32_t*)&header,80/4);

        printf("1:");hexdump((unsigned char*)&header, sizeof(block_header));


                    // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, 64);


        // dump out some debug data to the terminal
        printf("sizeof(block_header) = %d\n", (int) sizeof(block_header));
        printf("Block header (in human readable hexadecimal representation): ");
        hexdump((unsigned char*)&header, sizeof(block_header));

            // Use SSL's sha256 functions, it needs to be initialized
        SHA256_Init(&sha256_pass1);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass1, (unsigned char*)&header, sizeof(block_header));
        // this ends the sha256 session and writes the checksum to hash1
        SHA256_Final(hash1, &sha256_pass1);

        // to display this, we want to swap the byte order to big endian
        byte_swap(hash1, SHA256_DIGEST_LENGTH);
        printf("Useless First Pass Checksum: ");
        hexdump(hash1, SHA256_DIGEST_LENGTH);

        // but to calculate the checksum again, we need it in little endian, so swap it back
        byte_swap(hash1, SHA256_DIGEST_LENGTH);

        //same as above
        SHA256_Init(&sha256_pass2);
        SHA256_Update(&sha256_pass2, hash1, SHA256_DIGEST_LENGTH);
        SHA256_Final(hash2, &sha256_pass2);

        byte_swap(hash2, SHA256_DIGEST_LENGTH);
        printf("Target Second Pass Checksum: ");
        hexdump(hash2, SHA256_DIGEST_LENGTH);
}

#ifndef MINER_FOR_INCLUDE

/*
void TestStratumCalculation()
{

    cStratumJobsPull pull("stratum.bitcoin.cz","3333","jmanbox.worker1","9H7kM3Xr");
    pull.TestMode=true;
    pull.ConnectionState=cStratumJobsPull::ProtocolState::WaitForSibscribe;
    char Buff[4096];
    strcpy(Buff,"{\"error\": null, \"id\": 1, \"result\": [[[\"mining.set_difficulty\", \"b4b6693b72a50c7116db18d6497cac52\"], [\"mining.notify\", \"ae6812eb4cd7735a302a8a9dd95cf71f\"]], \"08001412\", 4]}");
    pull.ParseOneCommand(Buff);
    pull.ConnectionState=cStratumJobsPull::ProtocolState::NormalWork;

    strcpy(Buff,"{\"params\": [\"e8\", \"b53df5e53510c401ee111e994140484c73d0c1589b6f7e4e000000a200000000\", \"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff25034bc203062f503253482f040139e15108\", \"0b653839642f736c7573682f0000000001607b3e95000000001976a9146be318f57ccd5b85d7ee9cc15a3c5da4f98064af88ac00000000\", [\"c35686d74948ac9494293ccf81de52cc10743294eaa9ab7833accdc34d36356e\", \"aa13a8e95c2655df1ca6572f3d97c18596f27a7f4fbf741c0596c456cdc463f1\", \"3e92b4a998ecadf5ad81bdadc75ee6d941856c9bd9fc4b886c024a9ab2cb50fe\", \"3a34eabca86cd3e6d8307e48230bbef2ec9aa9752cc5ab99cbe9136e4d708db8\", \"b740d09f0ab93cabd86918695b0d2176d3c6c16a2a39ef73daf3736098663179\", \"fc35686fd9d239efaf1a370a22515a6c88c24848224b7e70875d8658b0442da5\", \"539ef1255b91600ddd81a2f27e38bfa6fd571da0304321cbacbfbbcb33e1a76b\"], \"00000002\", \"1a00a429\", \"51e13900\", false], \"id\": null, \"method\": \"mining.notify\"}");
    pull.ParseOneCommand(Buff);
    pull.NowExtraNonce2=0x07000000;
    pull.GenerateNewJob();
    cSearchNonceJob *O=pull.GetNextJob();
    uint32_t Nonce=0x2c429612;//SwapEndian(0x2c429612);
    O->SetFoundNonce(Nonce);
    cFoundStratumNonceJob *R=static_cast<cFoundStratumNonceJob*>(pull.GetNextNonce());
    printf("[%s]\n",R->GetHexDataString());
    test_bitcoin(R->GetHexDataString());
    test_bitcoin("0200000037cceabe9b99ae7e6f4c671f4d3ce224e0ff1620847a5d8f9a00000000000000e632c9750ff3eb427861ae4bffa50bb5eb2b9d2bbc9db9039872b20ffda64c4051e15b6629a4001a60c1da26");//26dac160
    getchar();
}*/


unsigned test_nonce_chip(unsigned tnon, unsigned *ms, unsigned *w)
{
        unsigned dd[16], ho[8];
        //int i;

        memset(dd, 0, sizeof(dd));
        dd[0] = w[0]; dd[1] = w[1]; dd[2] = w[2]; dd[3] = tnon; dd[4] = 0x80000000; dd[15] = 0x280;
//      for (i = 0; i < 8; i++) st[i] = ms[7-i];
        SHA256_Full(ho, dd, ms);
        memset(dd, 0, sizeof(dd));
        memcpy(dd, ho, 4*8);
        dd[8] = 0x80000000; dd[15] = 0x100;
        SHA256_Full(ho, dd, sha_initial_state);

        if (ho[7] == 0) return 1;

        return 0;
}

void TestStratumCalculation()
{
    cStratumJobsPull pull("stratum.bitcoin.cz","3333","jmanbox.worker1","9H7kM3Xr");
    pull.TestMode=true;
    pull.ConnectionState=cStratumJobsPull::ProtocolState::WaitForSibscribe;
    char Buff[4096];
    strcpy(Buff,"{\"id\": 1, \"result\": [[[\"mining.set_difficulty\", \"b4b6693b72a50c7116db18d6497cac52\"], [\"mining.notify\", \"ae6812eb4cd7735a302a8a9dd95cf71f\"]], \"08000002\", 4], \"error\": null}");
    pull.ParseOneCommand(Buff);
    pull.ConnectionState=cStratumJobsPull::ProtocolState::NormalWork;

    strcpy(Buff,"{\"params\": [\"bf\", \"4d16b6f85af6e2198f44ae2a6de67f78487ae5611b77c6c0440b921e00000000\",\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008\",\"072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000\", [],\"00000002\", \"1c2ac4af\", \"504e86b9\", false], \"id\": null, \"method\": \"mining.notify\"}");
    pull.ParseOneCommand(Buff);
    //pull.NowExtraNonce2=1;
    pull.GenerateNewJob();
    //pull.GenerateNewJob();
    cSearchStratumWorkJob *O=(cSearchStratumWorkJob *)pull.GetNextJob();
    //O->OnJobFinished();
    //O=(cSearchStratumWorkJob *)pull.GetNextJob();
    uint32_t Nonce=0xb2957c02;//SwapEndian(0xb2957c02);
    //0x504e86ed-504e86b9
    while(O->nTimeValue!=0x504e86ed)
    {
        printf("%08x ",O->nTimeValue);
        cSearchStratumWorkJob *Old=O;
        O=(cSearchStratumWorkJob *)O->GenerateFromSelfNextJob();
        Old->OnJobFinished();
    }
    //O->SetTime(0x504e86ed);
    printf("OK go!\n");
    O->SetFoundNonce(Nonce);
    cFoundStratumNonceJob *R=static_cast<cFoundStratumNonceJob*>(pull.GetNextNonce());
    test_bitcoin(R->GetHexDataString());
    pull.SendNonceToPull(R);
    printf("[%s]\n",R->GetHexDataString());
    cByteBufferType buff(R->GetHexDataString());
    printf("1 (%u)[%s]\n",buff.GetLength(),buff.ToText().GetText());
    cByteBufferType buff1;
    buff1.Assign(O->GetLeftData(),16);
    printf("2 (%u)[%s]\n",buff1.GetLength(),buff1.ToText().GetText());
    Nonce=SwapEndian(Nonce);
    printf("test_nonce=%d\n",test_nonce_chip(Nonce,(uint32_t*)O->GetMidState(),(uint32_t*)O->GetLeftData()));
    //buff.SwapDWORDEndians();

    {
        cByteBufferType MidState;

        /*
        uint32_t dd[16];
        memcpy(dd,job.GetBuffer(),64);
        for(uint8_t i=0;i<16;i++)
            dd[i]=SwapEndian(dd[i]);
        */

        SHA256_CTX sha256_pass;
        SHA256_Init(&sha256_pass);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass, buff.GetBuffer(), 64);
        MidState.Assign((uint8_t*)&sha256_pass,32);

        cByteBufferType K(O->GetMidState(),32);

        printf("MidSate1[%s]\nMidSate2[%s]\n"
            ,K.ToText().GetText()
            ,MidState.ToText().GetText());
        //Obj=new cSearchStratumWorkJob(this,MidState.GetBuffer(),&(job.GetBuffer()[64]),job.ToText().GetText());
        //uint32_t *RoteValue=(uint32_t*)Obj->OtherData;
        //for(uint8_t i=0;i<16;i++)
        //    RoteValue[i]=SwapEndian(RoteValue[i]);
        cByteBufferType Other(&(buff.ToText().GetText()[128]));
        Other.SwapDWORDEndians();
        printf("Other1[%s]\nOther2[%s]\n"
               ,buff1.ToText().GetText()
               ,Other.ToText().GetText());

        printf("test_nonce=%d\n",test_nonce_chip(Nonce,(uint32_t*)MidState.GetBuffer(),(uint32_t*)Other.GetBuffer()));
    }
    test_bitcoin("0200000043196bdaa9e5e2815f16c2fb810cb612a755a40b1f55d4ed9200000000000000ee354d646ebaad0a032940c41c0219516b8f204db1be82e98ec8a84dc09b90026474e15129a4001a6ac131bf");//bf31c16a
    getchar();
}


#endif

uint16_t cSearchStratumWorkJob::GetCurrentDifficulty()
{
    return Owner->GetCurrentDifficulty();
}
cFoundStratumNonceJob::cFoundStratumNonceJob(const cSearchStratumWorkJob &obj,uint32_t NonceVal)
{
    JobName=obj.JobName;
    nTime=obj.nTime;
    nTime.swap_array();
    ExtraNonce=obj.ExtraNonce;
    Nonce.Assign((uint8_t*)&NonceVal,4);
    //Nonce.swap_array();
    strncpy(hexData,obj.hexData,256+4);
}
cFoundStratumNonceJob::~cFoundStratumNonceJob()
{

}
void cFoundStratumNonceJob::OnFinished()
{
    delete this;
}
void cFoundStratumNonceJob::OnCanceled()
{
    delete this;
}

cSearchStratumWorkJob::cSearchStratumWorkJob(cStratumJobsPull *OwnerObj,const uint8_t *valMidState,const uint8_t* Other,const char *hexString)
:Owner(OwnerObj),NextJobGenerated(false)
{
    JobCounter=Owner->GetCurrentJobNumber();
    memcpy(MidState,valMidState,32);
    memcpy(OtherData,Other,64);
    strncpy(hexData,hexString,256+4);
}
cSearchStratumWorkJob::~cSearchStratumWorkJob()
{

}
cSearchStratumWorkJob::cSearchStratumWorkJob(const cSearchStratumWorkJob& obj)
:Owner(obj.Owner),NextJobGenerated(false)
{
    JobCounter=obj.JobCounter;
    memcpy(MidState,obj.MidState,32);
    memcpy(OtherData,obj.OtherData,64);
    strncpy(hexData,obj.hexData,256+4);
    JobName=obj.JobName;
    ExtraNonce=obj.ExtraNonce;
    nTime=obj.nTime;
    nTimeValue=obj.nTimeValue;
    nTimeValueRepresentIn=obj.nTimeValueRepresentIn;
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    //uint32_t now=nTimeValue+ts.tv_sec-nTimeValueRepresentIn;
    nTimeValueRepresentIn=ts.tv_sec;
    /*if(now<=nTimeValue)
    {
        nTimeValue++;
    }else
    {
        nTimeValue=now;
    }*/
    nTimeValue++;
    nTime.Assign((uint8_t*)&nTimeValue,4);
    nTime.SetLength(4);
    //nTime.swap_array();
    memcpy(&OtherData[4+32+32-64],nTime.GetBuffer(),nTime.GetLength());
    *((uint32_t*)&OtherData[4+32+32-64])=SwapEndian(*((uint32_t*)&OtherData[4+32+32-64]));//swap for right endians for chip

    char *Str=&hexData[(4+32+32)*2];
    DataToHex(nTime.GetBuffer(),Str,4,false);

    //printf("Regenerate job was[%s] now[%s] & %u now %u hex part [%.24s]\n",obj.nTime.ToText().GetText(),nTime.ToText().GetText(),obj.nTimeValue,nTimeValue,&hexData[(32+32)*2]);
}
cSearchNonceJob *cSearchStratumWorkJob::GenerateFromSelfNextJob()
{
    if(!IsCanGenerateNextFromSelf())
        return nullptr;
    NextJobGenerated=true;
    return new cSearchStratumWorkJob(*this);
}
bool cSearchStratumWorkJob::SetTime(uint32_t Time)
{
    //Nonce=SwapEndian(Nonce);
    nTime.Assign((uint8_t*)&Time,4);
    char *Str=&hexData[(4+32+32)*2];
    DataToHex((uint8_t*)&Time,Str,4,false);
    return true;
}
bool cSearchStratumWorkJob::SetFoundNonce(uint32_t Nonce)
{
    //printf("Found nonce 0x%08X\n",Nonce);

    uint32_t FoundNonse=Nonce;
    //Nonce=SwapEndian(Nonce);
    char *Str=&hexData[128+4*3*2];
    DataToHex((uint8_t*)&Nonce,Str,4);

    Owner->AddNonce(new cFoundStratumNonceJob(*this,FoundNonse));
    return cSearchNonceJob::SetFoundNonce(FoundNonse);
}
void cSearchStratumWorkJob::OnJobFinished()
{
    delete this;
}
void cSearchStratumWorkJob::OnJobCanceled()
{
    delete this;
}

bool cStratumJobsPull::GenerateNewJob()
{
    cSearchStratumWorkJob *Obj=nullptr;
    {
        ENTER_ONE_THREAD(MineDataMutex);
        if(PrevHash.GetLength()!=32)
        {
            return false;
        }
        cByteBufferType coinbase_hash_bin;
        coinbase_hash_bin=CoinBase1;
        coinbase_hash_bin+=ExtraNonce1;

        cByteBufferType  ExtraNonce2((uint8_t*)&NowExtraNonce2,ExtraNonce2Size);
        ExtraNonce2.swap_array();
        coinbase_hash_bin+=ExtraNonce2;
        coinbase_hash_bin+=CoinBase2;

        //printf("%u %u %u %u (%u) %u\n",CoinBase1.GetLength(),ExtraNonce1.GetLength(),ExtraNonce2.GetLength(),CoinBase2.GetLength()
        //       ,CoinBase1.GetLength()+ExtraNonce1.GetLength()+ExtraNonce2.GetLength()+CoinBase2.GetLength(),coinbase_hash_bin.GetLength());


        NowExtraNonce2++;

        //coinbase_hash_bin.SwapDWORDEndians();
        //printf("first transaction[%s]\n",coinbase_hash_bin.ToText().GetText());

        CalculateMerkleRoot(coinbase_hash_bin,coinbase_hash_bin);
        cByteBufferType job;
        //coinbase_hash_bin.swap_array();

        //coinbase_hash_bin.SwapDWORDEndians();

        job=Version;
        job+=PrevHash;
        job+=coinbase_hash_bin;

        timespec ts;
        clock_gettime(CLOCK_MONOTONIC,&ts);
        uint32_t now=nTime+ts.tv_sec-nTimeRepresentIn;
        //printf("WasTime:%u %08X %u",nTime,nTime,now);
        cByteBufferType nTimeBuffer((uint8_t*)&now,4);
        //nTimeBuffer.swap_array();

        job+=nTimeBuffer;
        job+=nBits;
        job+="00000000";
        job+="000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000";
        if(job.GetLength()!=128)
        {
            printf("Error result length wrong %u (%s)\n",job.GetLength(),job.ToText().GetText());
            return false;
        }
        cByteBufferType MidState;

        uint32_t dd[16];
        memcpy(dd,job.GetBuffer(),64);
        //no need rotate becouse job connected in right endiand
        //for(uint8_t i=0;i<16;i++)
        //    dd[i]=SwapEndian(dd[i]);

        SHA256_CTX sha256_pass;
        SHA256_Init(&sha256_pass);
        // then you 'can' feed data to it in chuncks, but here were just making one pass cause the data is so small
        SHA256_Update(&sha256_pass, dd, 64);
        MidState.Assign((uint8_t*)&sha256_pass,32);
        //printf("Preapared from [%s] job [%s] for ExNonce2[%s] Time[%s] MidSate[%s]\n"
        //    ,JobName.GetText(),job.ToText().GetText(),ExtraNonce2.ToText().GetText()
        //    ,nTimeBuffer.ToText().GetText(),MidState.ToText().GetText());
        Obj=new cSearchStratumWorkJob(this,MidState.GetBuffer(),&(job.GetBuffer()[64]),job.ToText().GetText());
        //need to change Endian in OtherData
        uint32_t *RoteValue=(uint32_t*)Obj->OtherData;
        for(uint8_t i=0;i<16;i++)
            RoteValue[i]=SwapEndian(RoteValue[i]);
        Obj->ExtraNonce=ExtraNonce2;
        Obj->JobName=JobName;
        Obj->nTime=nTimeBuffer;
        Obj->nTimeValue=now;
        Obj->nTimeValueRepresentIn=ts.tv_sec;
    }
    //now job is ready
    if(Obj)
    {
        return AddJob(Obj);
    }else
        return false;
}

void cStratumJobsPull::CalculateMerkleRoot(cByteBufferType &FirstTransaction,cByteBufferType &result)
{
    DoubleSHA256(FirstTransaction,result);
    /*SHA256_Full(ho, dd, ms);
    memset(dd, 0, sizeof(dd));
    memcpy(dd, ho, 4*8);
    dd[8] = 0x80000000; dd[15] = 0x100;
    SHA256_Full(ho, dd, sha_initial_state);
    */
    for(uint32_t i=0;i<MerkleRootLen;i++)
    {
        result+=GetMerkleRoot(i);
        DoubleSHA256(result,result);
    }
    //printf("Merkle root(%u):[%s]\n",MerkleRootLen,result.ToText().GetText());
}

void cStratumJobsPull::DoubleSHA256(cByteBufferType &obj,cByteBufferType &result)
{
    SHA256_CTX sha256_pass;
    //unsigned char hash1[SHA256_DIGEST_LENGTH];
    //unsigned char hash2[SHA256_DIGEST_LENGTH];

    SHA256_Init(&sha256_pass);
    SHA256_Update(&sha256_pass, obj.GetBuffer(), obj.GetLength());
    SHA256_Final(result.GetBuffer(32), &sha256_pass);
    result.SetLength(32);
    //pass 2
    SHA256_Init(&sha256_pass);
    SHA256_Update(&sha256_pass, result.GetBuffer(), result.GetLength());
    SHA256_Final(result.GetBuffer(32), &sha256_pass);
    result.SetLength(32);
}
cByteBufferType* cStratumJobsPull::AddMerkleRoot()
{
    ENTER_ONE_THREAD(MineDataMutex);
    if(MerkleRootLen>=MerkleRootSize)
    {
        MerkleRootSize+=16;
        cByteBufferType *Save=MerkleRoot;
        MerkleRoot=new cByteBufferType[MerkleRootSize];
        if(Save)
        {
            for(uint32_t i=0;i<MerkleRootLen;i++)
                MerkleRoot[i].move(Save[i]);
            delete[] Save;
        }
    }
    return &MerkleRoot[MerkleRootLen++];
}

uint32_t cStratumJobsPull::PrepareOneLineCommand()
{
    uint32_t i;
    for(i=InputBufferLastLineTestedPos;i<InputBufferUsed;i++)
    {
        if(InputBuffer[i]=='\n')
        {
            InputBuffer[i]='\0';
            i++;
            return i;
        }
    }
    InputBufferLastLineTestedPos=i;
    return 0;
}
uint32_t cStratumJobsPull::InputBufferShift(uint32_t FromPos)
{
    if(FromPos>=InputBufferUsed)
    {
        InputBufferUsed=InputBufferLastLineTestedPos=0;
        return InputBufferUsed;
    }else
    {
        memmove(InputBuffer,&InputBuffer[FromPos],InputBufferUsed-FromPos);
        InputBufferUsed-=FromPos;
        if(InputBufferLastLineTestedPos>FromPos)
            InputBufferLastLineTestedPos-=FromPos;
        else
            InputBufferLastLineTestedPos=0;
    }
    return InputBufferUsed;
}

cStratumJobsPull::cStratumJobsPull(const char *valHost,const char *valPort,const char *valUserName,const char *valPasswd)
    :Host(valHost,true),Port(valPort?valPort:"80",true),UserName(valUserName?valUserName:"",true),UserPwd(valPasswd?valPasswd:"")

{
    TestMode=false;
    PoolShareDiff=1;
    Working=false;
    PoolAcceptedShares=0;
    ConnectionState=NotConnected;
    JobCounter=0;
    ClearInputBuffer();
    MerkleRoot=nullptr;
    MerkleRootLen=MerkleRootSize=0;
    NowExtraNonce2=0;
    NowDifficulty=1;
    SignalNewNonceSended=SignalNeedJobSended=false;
    doTrace=true;
    SetMaxJobCount(1);
}
void cStratumJobsPull::ClearSignalNewNonce()
{
    ENTER_ONE_THREAD(SignalMutex);
    SignalNewNonceSended=false;
}
void cStratumJobsPull::ClearSignalNeedJob()
{
    ENTER_ONE_THREAD(SignalMutex);
    SignalNeedJobSended=false;
}
void cStratumJobsPull::SignalNewNonce()
{
    {
        ENTER_ONE_THREAD(SignalMutex);
        if(SignalNewNonceSended)
            return;
        SignalNewNonceSended=true;
    }
    Thread.SendToThread('N');
}
void cStratumJobsPull::SignalNeedJob()
{
    {
        ENTER_ONE_THREAD(SignalMutex);
        if(SignalNeedJobSended)
            return;
        SignalNeedJobSended=true;
    }
    Thread.SendToThread('W');
}
void cStratumJobsPull::OnHaveNewNonce()
{
    SignalNewNonce();
}
void cStratumJobsPull::OnNeedMoreJobs()
{
    SignalNeedJob();
}
cStratumJobsPull::~cStratumJobsPull()
{
    Stop();
    FreeMerkleRoot();
}
bool cStratumJobsPull::Run()
{
    if (isTestDataSource()) return true;
    {
        ENTER_ONE_THREAD(SockMutex);
        if(Working)
            return false;
        Working=true;
    }
    Thread.RunThread(this);
    return true;
}
bool cStratumJobsPull::Stop()
{
    {
        ENTER_ONE_THREAD(SockMutex);
        if(!Working)
            return false;
    }
    Thread.StopThread();
    {
        ENTER_ONE_THREAD(SockMutex);
        Working=false;
    }
    return true;
}
void cStratumJobsPull::OnConnectedToServer()
{
    if(sock.IsPresent())
    {
        ClearInputBuffer();
        NowDifficulty=1;
        LastPoolCommandId=5;
        ConnectionState=Start;
        const char *str="{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n";
        if (doTrace) printf("Send to server mining.subscribe!\n");
        if(sock.SendStr(str)<=0)
        {
            CloseSocket();
            return;
        }
        ConnectionState=WaitForSibscribe;
    }
}
bool cStratumJobsPull::SendNonceToPull(cFoundStratumNonceJob *NonceData)
{
    if((!sock.IsPresent() && !IsTestModeOn()) || ConnectionState!=NormalWork)
    {
        return false;
    }
    uint32_t command_id=GetNextPoolCommandId();
    cStringType Str;
    //NonceData->nTime.swap_array();
    Str.printf("{\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],\"id\":%u,\"method\":\"mining.submit\"}\n"
            ,UserName.GetText(),NonceData->JobName.GetText(),NonceData->ExtraNonce.ToText().GetText()
            ,NonceData->nTime.ToText().GetText(),NonceData->Nonce.ToText().GetText()
            ,command_id);
    //NonceData->nTime.swap_array();
    //printf("Send:(%s)\n[%s]\n",Str.GetText(),NonceData->GetHexDataString());
    if(IsTestModeOn())
    {
        printf("Virtual to socket:(%s)\n",Str.GetText());
    }else
    {
        if(sock.SendStr(Str)<=0)
        {
            CloseSocket();
            return false;
        }
    }
    return true;
}
void cStratumJobsPull::OnSubscribedToServer(const cJSonVar *Head)
{
    if((sock.IsPresent() || IsTestModeOn()) && ConnectionState==WaitForSibscribe)
    {
        const cJSonVar *result=(*Head)["result"];
        if(!result)
        {
            printf("No result on \"mining.subscribe\" from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        const cJSonVar *valExtraNonce1=(*result)[1];
        const cJSonVar *valExtraNonce2Size=(*result)[2];
        if(!valExtraNonce1 || !valExtraNonce2Size || !valExtraNonce1->IsVariable() || !valExtraNonce2Size->IsInteger() || valExtraNonce2Size->GetIntValue()<=0)
        {
            printf("error in result array on \"mining.subscribe\" from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        if(valExtraNonce2Size->GetIntValue()>8)
        {
            printf("We not support ExtraNonce2 more then 8 bytes. Closing connection!\n");
            CloseSocket();
            return;
        }
        ExtraNonce1=valExtraNonce1->GetValue();
        ExtraNonce2Size=valExtraNonce2Size->GetIntValue();
        if (doTrace) printf("Server setup for us extranonce1=[%s] and size of extranonce2=%u.\n",ExtraNonce1.ToText().GetText(),ExtraNonce2Size);
        cStringType str;
        str.printf("{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",UserName.GetText(),UserPwd.GetText());
        if(IsTestModeOn())
        {
            printf("Virtual to socket:(%s)\n",str.GetText());
        }else
        {
            if(sock.SendStr(str)<=0)
            {
                CloseSocket();
                return;
            }
        }
        if (doTrace) printf("Send to server mining.authorize!\n");
        ConnectionState=WaitMinerAuth;
    }
}
void cStratumJobsPull::OnNewMineJob(const cJSonVar *Head)
{
    if(sock.IsPresent() || IsTestModeOn())
    {
        const cJSonVar *method=(*Head)["method"];
        const cJSonVar *params=(*Head)["params"];
        if(!method || !method->IsVariable() || !params || !params->IsArray() || params->GetArrayLen()<9)
        {
            printf("No params in mining.notify from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        if(strcmp(method->GetValue(),"mining.notify")!=0)
        {
            return;
        }

        const cJSonVar *v=params->GetByIndex(0);
        if(!v || !v->IsVariable())
        {
            printf("in params[0] job_id error.Closing connection!\n");
            CloseSocket();
            return;
        }
        bool NeedTotalClean=false;
        ENTER_ONE_THREAD(MineDataMutex);
        {
            JobName=v->GetValue();

            v=params->GetByIndex(1);
            if(!v || !v->IsVariable())
            {
                printf("in params[1] prev_hash error.Closing connection!\n");
                CloseSocket();
                return;
            }
            PrevHash=v->GetValue();
            PrevHash.SwapDWORDEndians();

            v=params->GetByIndex(2);
            if(!v || !v->IsVariable())
            {
                printf("in params[2] coinbase1 error.Closing connection!\n");
                CloseSocket();
                return;
            }
            CoinBase1=v->GetValue();

            v=params->GetByIndex(3);
            if(!v || !v->IsVariable())
            {
                printf("in params[3] coinbase2 error.Closing connection!\n");
                CloseSocket();
                return;
            }
            CoinBase2=v->GetValue();

            v=params->GetByIndex(5);
            if(!v || !v->IsVariable())
            {
                printf("in params[5] version error.Closing connection!\n");
                CloseSocket();
                return;
            }
            Version=v->GetValue();
            Version.SwapDWORDEndians();

            v=params->GetByIndex(6);
            if(!v || !v->IsVariable())
            {
                printf("in params[6] nbits error.Closing connection!\n");
                CloseSocket();
                return;
            }
            nBits=v->GetValue();
            nBits.SwapDWORDEndians();

            v=params->GetByIndex(7);
            if(!v || !v->IsVariable())
            {
                printf("in params[7] ntime error.Closing connection!\n");
                CloseSocket();
                return;
            }
            {
                cByteBufferType nTimeBuff(v->GetValue());
                nTimeBuff.swap_array();
                memcpy(&nTime,nTimeBuff.GetBuffer(),4);
                timespec t;
                clock_gettime(CLOCK_MONOTONIC,&t);
                nTimeRepresentIn=t.tv_sec;
                //printf("new job nTime[%s][%s]=%u %08X %jd\n",v->GetValue(),nTimeBuff.ToText().GetText(),nTime,nTime,nTimeRepresentIn);
            }
            v=params->GetByIndex(8);
            if(!v || !v->IsBool())
            {
                printf("in params[8] clean_jobs error.Closing connection!\n");
                CloseSocket();
                return;
            }
            NeedTotalClean=v->GetBoolValue();
            CleanMerkleRoot();
            v=params->GetByIndex(4);
            if(!v || !v->IsArray())
            {
                printf("in params[4] merkle root error.Closing connection!\n");
                CloseSocket();
                return;
            }
            const cJSonVar *Element=v->GetInternal();
            uint32_t counter=0;
            while(Element)
            {
                if(!Element->IsVariable())
                {
                    printf("in merkle root [%u] error.Closing connection!\n",counter);
                    CloseSocket();
                    return;
                }
                cByteBufferType *Obj=AddMerkleRoot();
                Obj->Assign(Element->GetValue());
                Element=Element->GetNext();
                counter++;
            }
            //NowExtraNonce2=0;
            NowExtraNonce2++;
            JobCounter++;
        }
        if(NeedTotalClean)
        {
            //new block started need to clear all
            ClearJobs();
            ClearNonce();
            if (doTrace) printf("New block info come (Merkle=%u items)!\n",MerkleRootLen);
        }else
        {
            //ClearJobs();
            if (doTrace) printf("Change in current block come (Merkle=%u items)!\n",MerkleRootLen);
        }
    }
}
void cStratumJobsPull::OnSetDifficulty(const cJSonVar *Head)
{
    if(sock.IsPresent() || IsTestModeOn())
    {
        const cJSonVar *method=(*Head)["method"];
        const cJSonVar *params=(*Head)["params"];
        if(!method || !method->IsVariable() || !params || !params->IsArray())
        {
            printf("No method or params in Difficulty Notify from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        const cJSonVar *diff=params->GetByIndex(0);

        int val=diff->GetIntValue();
        if(val>=1)
        {
            NowDifficulty=val;
            if (doTrace) printf("Pool change difficulty to %u\n",NowDifficulty);
        }
    }
}
void cStratumJobsPull::OnNotifyFromPool(const cJSonVar *Head)
{
    if(sock.IsPresent() || IsTestModeOn())
    {
        const cJSonVar *method=(*Head)["method"];
        const cJSonVar *params=(*Head)["params"];
        if(!method || !method->IsVariable() || !params)
        {
            printf("No method or params in Notify from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        if(strcmp(method->GetValue(),"mining.notify")==0)
        {
            OnNewMineJob(Head);
            return;
        }
        if(strcmp(method->GetValue(),"mining.set_difficulty")==0)
        {
            OnSetDifficulty(Head);
            return;
        }
        Head->PrintSelf(stdout);
        printf("Unknown notity (%s)\n",method->GetValue());
    }
}
void cStratumJobsPull::OnAuthorizedToServer(const cJSonVar *Head)
{
    if(sock.IsPresent() && ConnectionState==WaitMinerAuth)
    {
        const cJSonVar *result=(*Head)["result"];
        if(!result || !result->IsBool())
        {
            printf("No result on \"mining.authorize\" from server. Closing connection!\n");
            CloseSocket();
            return;
        }
        if(result->GetBoolValue())
            if (doTrace) printf("Authorization is done, we can send shares!\n");
        else
        {
            if (doTrace) printf("Authorization is failed, we can`t send shares!\n");
            if (doTrace) printf("Please check your login password.\n");
        }
        /*ConnectionState=Start;
        cStringType str;
        str.printf("{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",UserName.GetText(),UserPwd.GetText());
        if(sock.SendStr(str)<=0)
        {
            CloseSocket();
            return;
        }*/
        ConnectionState=NormalWork;
    }
}
void cStratumJobsPull::OnAnswerFromServer(const cJSonVar *Head)
{
    if(sock.IsPresent() && ConnectionState==NormalWork)
    {
        const cJSonVar *result=(*Head)["result"];
        const cJSonVar *id=(*Head)["id"];
        const cJSonVar *error=(*Head)["error"];
        if(!result || !id || !id->IsInteger() ||!error)
        {
            Head->PrintSelf(stdout);
            printf(" No result from server!\n");
            //CloseSocket();
            return;
        }
        if(result->IsBool())
        {
            if(result->GetBoolValue())
            {
                //printf("Share accepted %d!\n",id->GetIntValue());
            }
            else
            {
                error->PrintSelf(stdout);
                printf(" Share NOT accepted %d!\n",id->GetIntValue());
            }
        }else
        {
            error->PrintSelf(stdout);
            printf(" id=%d\n",id->GetIntValue());
        }
    }
}
void cStratumJobsPull::ParseOneCommand(char *buff)
{
    const cJSonVar* Head=ParseJSon(buff);
    if(!Head)
    {
        return;
    }
    //Head->PrintSelf(stdout);printf("\n");fflush(stdout);
    const cJSonVar* id=Head->Search("id");
    /*
    const cJSonVar* result=Head->Search("result");
    const cJSonVar* method=Head->Search("method");
    const cJSonVar* params=Head->Search("params");
    const cJSonVar* error=Head->Search("error");
    */
    if(!id || (!id->IsInteger() && !id->IsNull()))
    {
        printf("\"id\" must be integer or null in json string\n");
        Head->PrintSelf(stdout);
        fflush(stdout);
        delete Head;
        return;
    }
    uint32_t CommandId=id->GetIntValue();
    if(id->IsNull())
    {
        OnNotifyFromPool(Head);
    }else
    {
        switch(CommandId)
        {
            case 1:OnSubscribedToServer(Head);break;
            case 2:OnAuthorizedToServer(Head);break;
            default:OnAnswerFromServer(Head);break;
        }
    }

    delete Head;
}
void cStratumJobsPull::OnProcessInputData()
{
    int res=sock.Read(GetInputBufferFreeSpaceBegin(),GetInputBufferFreeSpace());
    if(res<=0)
    {
        CloseSocket();
        return;
    }
    InputBufferUsed+=res;
    while(true)
    {
        uint32_t pos=PrepareOneLineCommand();
        if(pos>0)
        {
            ParseOneCommand(InputBuffer);
            InputBufferShift(pos);
        }else
            break;
    }
}
void cStratumJobsPull::ThreadFunc()
{
    pollfd p[2];
    memset(p,0,sizeof(p[0])*(2));
    p[0].fd=Thread.GetThreadPartPipe().GetDescriptor();
    p[0].events=POLLIN;
    bool NeedExit=false;
    while(!NeedExit)
    {
        {
            ENTER_ONE_THREAD(SockMutex);
            if(!sock.IsPresent())//need to open connection ?
            {
                if(!sock.OpenTCPIPv4(Host,Port))
                {
                    printf("Enable connect to stratum server\n");
                }else
                {
                    OnConnectedToServer();
                }
            }
            ClearSignalNewNonce();
            while(true)
            {
                if(!sock.IsPresent())//not opened connection
                    break;
                cFoundNonceJob *Obj=GetNextNonce();
                if(!Obj)
                    break;
                cFoundStratumNonceJob *NonceData=static_cast<cFoundStratumNonceJob*>(Obj);
                //need send data to pool
                if(!SendNonceToPull(NonceData))
                    Obj->OnCanceled();
                else
                    Obj->OnFinished();
            }
            ClearSignalNeedJob();
            uint32_t Iterations=0;
			while(GetNeedJobCount()>0 && Iterations<50)
            {
                if(!sock.IsPresent())//not opened connection
                    break;
                //need to generate next jobs and put it to wait order
                if(!GenerateNewJob())
                    break;
                Iterations++;
            }
        }
        p[1].fd=sock.GetSocketFD();
        p[1].events=POLLIN;
        //p[0].revents=0;
        //p[1].revents=0;
        while(poll(p,p[1].fd>0?2:1,1000)<0){ }

        {
            ENTER_ONE_THREAD(SockMutex);
            if(p[0].revents&POLLIN)
            {
                if(Thread.CheckNextThreadCommandForExit())
                {
                    NeedExit=true;
                    Thread.SendThreadCommandExitAsk();
                }
            }
            if(p[1].fd>0 && p[1].revents&POLLIN)
            {
                OnProcessInputData();
            }
        }
    }
}


void TestStratumPullJobObject()
{
    cStratumJobsPull obj("stratum.bitcoin.cz","3333","jmanbox.worker1","9H7kM3Xr");
    //cStratumJobsPull obj("195.74.67.253","3333","jmanbox.worker1","9H7kM3Xr");
    //cGetWorkJobsPull obj("api.bitcoin.cz","8332","ihor.worker3","1");
    obj.Run();
    sleep(1);
    bool exit=false;
    uint32_t c=1;
    cSearchNonceJob *Obj=nullptr;
    while(!exit)
    {
        if(!Obj)
            Obj=obj.GetNextJob();
        printf("Job %s left %u\n",Obj?"present":"NONE",obj.GetJobCount());
        if(Obj)
        {
            /*if(c%10==0)
            {
                cSearchGetWorkJob *Obj1=static_cast<cSearchGetWorkJob*>(Obj);

                printf("hexData:[%s]\n",Obj1->GetHexString());
                Obj->SetFoundNonce(1456345);
            }*/
            //Obj->SetFoundNonce(1456345);
            cSearchNonceJob *NextObj=Obj->GenerateFromSelfNextJob();
            Obj->OnJobFinished();
            Obj=NextObj;
        }
        sleep(5);
        c++;
    }
    obj.Stop();
    printf("Finished\n");
    getchar();
//    exit(0);
}

void TestMerkle()
{
    uint8_t Data1[SHA256_DIGEST_LENGTH];
    uint8_t Data2[SHA256_DIGEST_LENGTH];
    //hex2bin(Data1,"4c52fb6a780c3a1cce637b421de2b850938db966a981fcafd122be6996d3f6ae");
    //hex2bin(Data2,"de4a8968c142ca71dbb34974f28abef9842480cf013ef62323e5d724d39a045f");
    hex2bin(Data1,"942e6276e6e5798fa42c8536d6af00afdead757def493a812bc5cbb9c36b6bc3");
    hex2bin(Data2,"629e953d9b4d0d198cc940cbf42a9acc86414f9ef9165a8127f10bee496b7e66");

    byte_swap(Data1, SHA256_DIGEST_LENGTH);
    byte_swap(Data2, SHA256_DIGEST_LENGTH);

    hexdump(Data1, SHA256_DIGEST_LENGTH);
    hexdump(Data2, SHA256_DIGEST_LENGTH);


    /*SwapDWORDEndians((uint32_t*)Data1,SHA256_DIGEST_LENGTH/4);
    SwapDWORDEndians((uint32_t*)Data2,SHA256_DIGEST_LENGTH/4);

        unsigned dd[16], ho[8];
        //int i;

        //memset(dd, 0, sizeof(dd));
        memcpy(dd,Data1,32);
        memcpy(&dd[8],Data2,32);
        //dd[0] = w[0]; dd[1] = w[1]; dd[2] = w[2]; dd[3] = tnon; dd[4] = 0x80000000; dd[15] = 0x280;
//      for (i = 0; i < 8; i++) st[i] = ms[7-i];
        SHA256_Full(ho, dd, sha_initial_state);
        memset(dd, 0, sizeof(dd));
        memcpy(dd, ho, 4*8);
        dd[8] = 0x80000000; dd[15] = 0x100;
        SHA256_Full(ho, dd, sha_initial_state);

        hexdump((uint8_t*)ho, SHA256_DIGEST_LENGTH);

    SwapDWORDEndians((uint32_t*)Data1,SHA256_DIGEST_LENGTH/4);
    SwapDWORDEndians((uint32_t*)Data2,SHA256_DIGEST_LENGTH/4);*/


    SHA256_CTX sha256_pass1, sha256_pass2;
    unsigned char hash1[SHA256_DIGEST_LENGTH];
    unsigned char hash2[SHA256_DIGEST_LENGTH];

    SHA256_Init(&sha256_pass1);
    SHA256_Update(&sha256_pass1, (unsigned char*)&Data1, 32);
    SHA256_Update(&sha256_pass1, (unsigned char*)&Data2, 32);
    SHA256_Final(hash1, &sha256_pass1);

    //hexdump(hash1, SHA256_DIGEST_LENGTH);

    // to display this, we want to swap the byte order to big endian
    /*byte_swap(hash1, SHA256_DIGEST_LENGTH);
    printf("Useless First Pass Checksum: ");
    hexdump(hash1, SHA256_DIGEST_LENGTH);
    byte_swap(hash1, SHA256_DIGEST_LENGTH);*/

    //same as above
    SHA256_Init(&sha256_pass2);
    SHA256_Update(&sha256_pass2, hash1, SHA256_DIGEST_LENGTH);
    SHA256_Final(hash2, &sha256_pass2);

    byte_swap(hash2, SHA256_DIGEST_LENGTH);
    printf("Target Second Pass Checksum: ");
    hexdump(hash2, SHA256_DIGEST_LENGTH);
    getchar();
}

#ifndef MINER_FOR_INCLUDE

unsigned decnonce(unsigned in)
{
        unsigned out;

        /* First part load */
        out = (in & 0xFF) << 24; in >>= 8;

        /* Byte reversal */
        in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
        in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
        in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

        out |= (in >> 2)&0x3FFFFF;

        /* Extraction */
        if (in & 1) out |= (1 << 23);
        if (in & 2) out |= (1 << 22);

        out -= 0x800004;
        return out;
}

/*
tnon = non;
tnon = non-0x400000;
tnon = non-0x800000;
tnon = non+0x2800000;
tnon = non+0x2C00000;
tnon = non+0x400000;

*/
void results(uint32_t non)
{
    uint32_t tnon;
    tnon = non;printf("%08X ",tnon);
    tnon = non-0x0400000;printf("%08X ",tnon);
    tnon = non-0x0800000;printf("%08X ",tnon);
    tnon = non+0x2800000;printf("%08X ",tnon);
    tnon = non+0x2C00000;printf("%08X ",tnon);
    tnon = non+0x0400000;printf("%08X ",tnon);
}

void test_nonce_decode()
{
    uint8_t i;
    uint8_t i1;
    uint8_t res[32];
    memset(res,99,32);

    /*{
        uint32_t in=(1<<16)|(1);
        printf("%08X ",in);
        in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
        in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
        in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));
        printf("%08X\n",in);
    }*/

    for(i=0;i<32;i++)
    {
        uint32_t nonce=(1<<i);
        uint32_t result=decnonce(nonce);
        printf("%08X=>%08X  ",nonce,result);results(result);
        printf("\n");
        for(i1=0;i1<32;i1++)
        {
            if(result&(1<<i1))
            {
                res[i]=i1;
                break;
            }
        }
    }
    for(i=0;i<32;i++)
        printf("%02u ",i);
    printf("\n");
    for(i=0;i<32;i++)
        printf("%02u ",res[i]);
    printf("\n\n");
    printf("***");
    for(i=0;i<32;i++)
        printf("%02u ",i);
    printf("\n");
    for(i1=0;i1<32;i1++)
    {
        printf("%02u:",i1);
        for(i=0;i<32;i++)
        {
            if(res[i1]==i)
                printf(" * ");
            else
                printf(" - ");
        }
        printf("\n");
    }
    getchar();
}

#endif


#ifndef MINER_FOR_INCLUDE

#include <iostream>
#include <cmath>

inline float fast_log(float val)
{
   int * const exp_ptr = reinterpret_cast <int *>(&val);
   int x = *exp_ptr;
   const int log_2 = ((x >> 23) & 255) - 128;
   x &= ~(255 << 23);
   x += 127 << 23;
   *exp_ptr = x;

   val = ((-1.0f/3) * val + 2) * val - 2.0f/3;
   return ((val + log_2) * 0.69314718f);
}

float difficulty(unsigned int bits)
{
    static double max_body = fast_log(0x00ffff), scaland = fast_log(256);
    return exp(max_body - fast_log(bits & 0x00ffffff) + scaland * (0x1d - ((bits & 0xff000000) >> 24)));
}

int t()
{
    std::cout << difficulty(0x1d00ffff) << std::endl;
    return 0;
}

void WorkWithSTMBoard()
{
    int f=open("/dev/ttyACM0",O_RDWR);
    if(f>0)
    {
        uint8_t ping[36];
        ping[0]=3;
        ping[1]=2;
        ping[2]=0;
        ping[3]=0;
        ping[4]=0xBB;
        printf("preapare write\n");
        ssize_t res=write(f,ping,3+2);
        printf("writed = %zd bytes\n",res);
        res=read(f,ping,36);
        printf("readed = %zd bytes\n",res);
        close(f);
    }else
    {
        printf("Error open USB com port!\n");
    }
    getchar();
}

int main()
{
    WorkWithSTMBoard();
    /*char str[20];
    memset(str,0,20);
    uint32_t k=0x29abf63e;
    DataToHex((uint8_t*)&k,str,4);
    printf("Was %08X str [%s]\n",k,str);
    getchar();
    return 0;*/

    //test2();
    //getchar();
    //TestKeepAlive();
    //SaveGetWorkTestVectors();
    //PlayGetWorkTestVectors();
    //getchar();
    //test_nonce_decode();
    //TestMerkle();
    TestStratumCalculation();
    //TestStratumPullJobObject();

    //test2_1();

    //getchar();

//    return 0;

    cMineJob Job("pit.deepbit.net","8332","jman.box@gmail.com_test","1");
    printf("Using auth str [%s]\n",Job.GetAuthString());
    while(true)
    {
        if(!Job.GetJobTest())
        {
            printf("Error get job\n");
            break;
        }

        uint32_t atrvec[20];

        memcpy(&atrvec[0],Job.GetMidState(),32);
        //SwapDWORDEndians(&atrvec[0],32/4);
        memcpy(&atrvec[16],Job.GetLeftData(),16);
        //SwapDWORDEndians(&atrvec[16],16/4);

        printf("\n0xbfeb8d06, 0xcf910b9b, 0x21a1e237, 0x3cf6f7b7, 0xdf848cd2, 0x967c5c06, 0x9545411c, 0x499b2c3f, "
                "0x9f9f2513, 0x46512b4f, 0x3fd40c1a, 0xbfdba262, \n");

        for(uint8_t i=0;i<8;i++)
            printf("0x%08x, ",atrvec[i]);
        for(uint8_t i=0;i<4;i++)
            printf("0x%08x, ",atrvec[16+i]);

        printf("\n");

        getchar();
        return 0;
        printf("Begin search for job\n");
        for(uint32_t i=0;i<0xFFFFFFFF;i++)
        {
            if(test_nonce_chip(i,(uint32_t*)Job.GetMidState(),(uint32_t*)Job.GetLeftData())==1)
            {
                Job.SetFoundNonce(i);
                printf("Found some nonce:%08x\n",i);
                Job.SendJob();
                break;
            }
            if((i%0x100000)==0)
            {
                printf("Searching %08x\n",i);
            }
        }
        break;
    }
}

#endif

class cMultyChipBoard
{
public:
    cMultyChipBoard(const char *poolHost,const char *poolPort,const char *poolUserName,const char *poolPasswd);
    void InitAll(const char *DevChanelName=nullptr);
    void RunLoop();
    inline uint32_t GetTotalSolutions()const{uint32_t t=0;for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){t+=Chips[i][j].GetFoundSolutions();}return t;}
    inline uint32_t GetTotalSolutionsByDifficulty()const{uint32_t t=0;for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){t+=Chips[i][j].GetFoundSolutionsByDifficulty();}return t;}
    inline uint32_t GetTotalJobsDone()const{uint32_t t=0;for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){t+=Chips[i][j].GetJobsDone();}return t;}

    inline void AllChipsInfoEnablePrint(bool Val=true){for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){Chips[i][j].EnablePrintChipInfo(Val);}}
    inline void AllChipsInfoEnableOncePrint(bool Val=true){for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){Chips[i][j].EnablePrintChipInfoOnce(Val);}}

    // Attention: changed order of iterations for user frendly output!!!
    inline void PrintAllChipsInfo(bool Val=true){for(uint8_t j=0;j<SPI_COUNT;j++)for(uint8_t i=0;i<CHIPS_PER_SPI;i++)if(Chips[i][j].IsEnabled()){Chips[i][j].PrintChipInfo();}}
    
    inline void SendAllChipsOSC(bool Val=true){for(uint8_t i=0;i<CHIPS_PER_SPI;i++){for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){Chips[i][j].SendOSCValue();}SendAllBuffers(i);}}

    cStratumJobsPull Poll;
    inline bool IsUSB()const{return WorkByUSB;}
    inline bool IsUSBConnected()const{return WorkByUSB && USBCom.IsConnectionOk();}
    void CheckStallChips(bool printMessages);
    inline const cUSBComChipExchange& GetUSBBusObject()const{return USBCom;}
    
    cBitfuryChip& GetChip(uint8_t spi, uint8_t seq) { return Chips[seq][spi]; }
    // TODO: refactor this
    uint32_t GetChipsCount() { uint32_t t=0; for(uint8_t i=0;i<CHIPS_PER_SPI;i++)for(uint8_t j=0;j<SPI_COUNT;j++)if(Chips[i][j].IsEnabled()){t++;}return t; }
    
    // TestTool (tt) staff
    bool ttExtStat;
    void testToolLoop();
	void testUnitLoop();
    bool isTestBoardConnected();
protected:
    bool WorkByUSB;
    cStringType DevName;
    cUSBComChipExchange USBCom;
    cMultyBufferSPIExchange spi;
    cBitfuryChip Chips[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];
    uint8_t *SendBuffs[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];
    uint8_t *RecvBuffs[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];
    uint32_t LastChipJobsDone[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];
    uint32_t LastChipFoundSolutions[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];
    uint32_t LastChipFoundSolutionsCount[MAX_CHIPS_PER_SPI][MAX_SPI_COUNT];

    bool SendAllBuffers(uint8_t spi_sequence);
    void SetAllRecvBuffersSize(uint8_t spi_sequence, uint16_t Size);
    void InitChips();
    void InitPB();
};


cMultyChipBoard::cMultyChipBoard(const char *poolHost,const char *poolPort,const char *poolUserName,const char *poolPasswd)
  :Poll(poolHost,poolPort,poolUserName,poolPasswd)
{
    WorkByUSB=false;
    memset(SendBuffs, 0, sizeof(SendBuffs));
    memset(RecvBuffs, 0, sizeof(RecvBuffs));
    ttExtStat = false;
}
void cMultyChipBoard::CheckStallChips(bool printMessages)
{
    if(WorkByUSB)
    {
        if(!USBCom.IsConnectionOk())
            return;
    }
    for(uint8_t i=0;i<CHIPS_PER_SPI;i++)
    for(uint8_t j=0;j<SPI_COUNT;j++) 
    if (Chips[i][j].IsEnabled())
    {
        bool restartChip = false;
        if(Chips[i][j].GetJobsDone()==LastChipJobsDone[i][j])
        {
            //chip is stall
            if (printMessages) printf("Restart chip spi %u seq %u\n",j,i);
            restartChip = true;
        }
        
        if (!restartChip)
        LastChipJobsDone[i][j]=Chips[i][j].GetJobsDone();
        if(Chips[i][j].GetLastJobTakeTime()>2000)
        {
            if (printMessages)  printf("Restart chip spi %u seq %u low speed\n",j,i);
            restartChip = true;
        }
        if(Chips[i][j].GetLastJobTakeTime()<800 && Chips[i][j].GetFoundSolutions()==LastChipFoundSolutions[i][j])
        {
            if(LastChipFoundSolutionsCount[i][j]>2)
            {
                if (printMessages) printf("Restart chip spi %u seq %u hight speed\n",j,i);
                restartChip = true;
                LastChipFoundSolutionsCount[i][j]=0;
            }else
            {
                LastChipFoundSolutionsCount[i][j]++;
            }
        }else
            LastChipFoundSolutionsCount[i][j]=0;
        LastChipFoundSolutions[i][j]=Chips[i][j].GetFoundSolutions();
        
        if (Chips[i][j].GetJobsWithoutSolutions() > 15)
        {
            // there are no correct solutions from the chip for the last few jobs
            if (printMessages) printf("Restart chip spi %u seq %u no correct solutions\n",j,i);
            restartChip = true;
            Chips[i][j].ResetJobsWithoutSolutions();
        }
        
        if (restartChip)
        {
            Chips[i][j].InitChip();
            if(WorkByUSB)
                USBCom.TxRx(j,SendBuffs[i][j],RecvBuffs[i][j],Chips[i][j].GetSendBuffer().GetLength());
            else
                spi.TxRx(j,SendBuffs[i][j],RecvBuffs[i][j],Chips[i][j].GetSendBuffer().GetLength());
        }
    }
}
void cMultyChipBoard::RunLoop()
{
    if(WorkByUSB)
    {
        if(!USBCom.IsConnectionOk())
        {
            if(USBCom.Init(DevName))
            {
                if(!USBCom.Ping())
                {
                    printf("Ping to dev OK!\n");
                }else
                    printf("Ping to dev FALSE!\n");
            }else
                return;
            InitChips();
        }
    }
    for(uint8_t i=0;i<CHIPS_PER_SPI;i++)
    {
        for(uint8_t j=0;j<SPI_COUNT;j++) if (Chips[i][j].IsEnabled())
        {
            if(!Chips[i][j].SendNextWorkBuffer())
                return;
        }
        if(!SendAllBuffers(i))
            return;
        for(uint8_t j=0;j<SPI_COUNT;j++) if (Chips[i][j].IsEnabled())
        {
            Chips[i][j].OnNewResultFromChip();
        }
    }
}
void cMultyChipBoard::InitChips()
{
/*
col / row mapping for 6x13 board (numbering from 1):
    12       11       10        9        8        7        6        5        4        3        2        1        0
5  [66:  ]  [67:  ]  [68:  ]  [69:  ]  [70:  ]  [71:  ]  [72:  ]  [73:  ]  [74:  ]  [75:  ]  [76:  ]  [77:  ]  [78:  ]
4  [65:  ]  [64:  ]  [63:  ]  [62:  ]  [61:  ]  [60:  ]  [59:  ]  [58:  ]  [57:  ]  [56:  ]  [55:  ]  [54:  ]  [53:  ]
3  [40:  ]  [41:  ]  [42:  ]  [43:  ]  [44:  ]  [45:  ]  [46:  ]  [47:  ]  [48:  ]  [49:  ]  [50:  ]  [51:  ]  [52:  ]
2  [39:  ]  [38:  ]  [37:  ]  [36:  ]  [35:  ]  [34:  ]  [33:  ]  [32:  ]  [31:  ]  [30:  ]  [29:  ]  [28:  ]  [27:  ]
1  [14:  ]  [15:  ]  [16:  ]  [17:  ]  [18:  ]  [19:  ]  [20:  ]  [21:  ]  [22:  ]  [23:  ]  [24:  ]  [25:  ]  [26:  ]
0  [13:  ]  [12:  ]  [11:  ]  [10:  ]  [09:  ]  [08:  ]  [07:  ]  [06:  ]  [05:  ]  [04:  ]  [03:  ]  [02:  ]  [01:  ]

chipProcessingOrder for this example (numbering from 0):
 0, 25, 26, 51, 52, 77, 1,  24, 27, 50, 53, 76,  2, 23, 28, 49, 54, 75,
 3, 22, 29, 48, 55, 74, 4,  21, 30, 47, 56, 73,  5, 20, 31, 46, 57, 72,
 6, 19, 32, 45, 58, 71, 7,  18, 33, 44, 59, 70,  8, 17, 34, 43, 60, 69,
 9, 16, 35, 42, 61, 68, 10, 15, 36, 41, 62, 67, 11, 14, 37, 40, 63, 66,
12, 13, 38, 39, 64, 65
*/
	int cols = 13;
	
	int rows = CHIPS_PER_SPI / cols;
	if (CHIPS_PER_SPI % cols != 0) rows++;
	
	uint32_t chipProcessingOrder[MAX_CHIPS_PER_SPI];
	
	int index=0;
	for (int col=0; col<cols; col++)
	{
		for (int row=0; row<rows; row++)
		{
			int seq = row*cols + (row%2==0? col : (cols-1)-col);
			chipProcessingOrder[index] = seq;
			index++;
		}
	}
	
    for(uint32_t index=0;index<CHIPS_PER_SPI;index++)
    {
		uint32_t i = (g_spiSmartInit ? chipProcessingOrder[index] : index);

        for(uint32_t j=0;j<SPI_COUNT;j++) if (Chips[i][j].IsEnabled())
        {
            Chips[i][j].SetSpiSeqId(i);
            Chips[i][j].SetSpiId(j);
            Chips[i][j].SetWorkManager(&Poll);
            Chips[i][j].SetChipFreqCode(54);
            Chips[i][j].InitChip();
            SendBuffs[i][j]=Chips[i][j].GetSendBuffer().GetBuffer();
            RecvBuffs[i][j]=Chips[i][j].GetRecvBuffer().GetBuffer();
        }
        SendAllBuffers(i);
    }
}
void cMultyChipBoard::InitAll(const char *DevChanelName)
{
    if(DevChanelName)
    {
        WorkByUSB=true;
        DevName=DevChanelName;
        if(USBCom.Init(DevChanelName))
        {
            if(USBCom.Ping())
            {
                printf("Ping to dev OK!\n");
            }else
                printf("Ping to dev FALSE!\n");
            //return false;
        }else
        {
            printf("Open USBPort failed!\n");
        }
    }
    Poll.SetMaxJobCount(GetChipsCount() * 2);
    Poll.Run();
    if(!WorkByUSB)
        spi.Init();
    InitChips();
    InitPB();
}
void cMultyChipBoard::SetAllRecvBuffersSize(uint8_t spi_sequence, uint16_t Size)
{
    for(uint32_t j=0;j<SPI_COUNT;j++) if (Chips[spi_sequence][j].IsEnabled())
    {
        Chips[spi_sequence][j].GetRecvBuffer().SetLength(Size);
    }
}
bool cMultyChipBoard::SendAllBuffers(uint8_t spi_sequence)
{
    //printf("::SendAllBuffers: spi_sequence = %d\n", spi_sequence);
    uint16_t len = 0;
    for(uint32_t j=0;j<SPI_COUNT;j++)
    {
        if (!Chips[spi_sequence][j].IsEnabled())
            continue;
        
        uint16_t lenCurrr = Chips[spi_sequence][j].GetSendBuffer().GetLength();
        if (len == 0) {
            len = lenCurrr; continue;
        }
        
        if (len != lenCurrr)
        {
            printf("Error buffer len for chip spi %u seq %u is %u, but must be %u!\n",j,spi_sequence,lenCurrr,len);
            return false;
        }
    }
    
    uint32_t spi_mask = 0;
    for(uint32_t j=0;j<SPI_COUNT;j++)
    {
        if (Chips[spi_sequence][j].IsEnabled())
            spi_mask |= (1 << j);
    }
    
    bool result=false;
    if(WorkByUSB)
    {
        result=USBCom.TxRx(spi_mask,SendBuffs[spi_sequence],RecvBuffs[spi_sequence],len);
    }else
        result=spi.TxRx(spi_mask,SendBuffs[spi_sequence],RecvBuffs[spi_sequence],len);
    if(result)
    {
        SetAllRecvBuffersSize(spi_sequence, len);
    }
    return result;
}
void cMultyChipBoard::InitPB()
{
	//  TODO!
}

#ifdef MINER_FOR_INCLUDE

bool cMultyChipBoard::isTestBoardConnected()
{
	//return true; // DEBUG-TT: emulate board present
    for(uint8_t seq=0;seq<CHIPS_PER_SPI;seq++)if (GetChip(0,seq).IsEnabled())
    {
        bool chipFound = false;
		int l = GetChip(0,seq).GetSendBuffer().GetLength();
        for (int i=0; i<l; i++)
			if (RecvBuffs[seq][0][i] != 0x00 && RecvBuffs[seq][0][i] != 0xff)
				chipFound = true;
        
        /*if (ttExtStat) {
            if (seq == 0 || seq == 40) {
                int ll = GetChip(0,seq).GetSendBuffer().GetLength();
                printf("seq = %d, buffer length: %d\n", seq, ll);
                for (int i = 0; i < ll; i++) {
                    printf("%02x ", RecvBuffs[seq][0][i]);
                }
                printf("\n");
            }
        }*/
        
        if (chipFound) {
            //if (ttExtStat) printf("isTestBoardConnected: [chip %d found]\n", seq);
            return true;
        }
    }
    return false;
}

void cMultyChipBoard::testToolLoop()
{
    uint32_t prevSolBoard[MAX_CHIPS_PER_SPI];
    uint32_t prevErrBoard[MAX_CHIPS_PER_SPI];
    
    while (true)
    {
        printf("*** Searching for Board...");
        while (!isTestBoardConnected())
        {
            printf("."); fflush(stdout);
			RunLoop();
        }
        printf(" [found]\n");
      
        printf("\n");
        printf("---------------------------------------\n");
        printf("-----         BOARD TEST          -----\n");
        printf("---------------------------------------\n");
        printf("\n");
		
        for(uint8_t seq=0;seq<CHIPS_PER_SPI;seq++)if(GetChip(0,seq).IsEnabled())
        {
            prevSolBoard[seq] = GetChip(0,seq).FoundSolutions;
            prevErrBoard[seq] = GetChip(0,seq).FoundErrors;
        }
        
        while (true)
        {
            printf("*** Running tests (%d seconds)...", g_testtoolTestTime);
        
			// Restart chips
			InitChips();
			
            time_t startTime = time(NULL);
            time_t now = startTime;
            while (now < startTime + g_testtoolTestTime &&
                   isTestBoardConnected())
            {
                printf("."); fflush(stdout);
                
                RunLoop();
				if (!isTestBoardConnected()) break;
            
                usleep(1000 * g_polingDelay);
                now = time(NULL);
            }
            printf(" [done]\n");
            
            //DEBUG
            if (ttExtStat) PrintAllChipsInfo();
            
            
            printf("\n");
            printf("****** TEST RESULTS ******\n");

			uint32_t sol[MAX_CHIPS_PER_SPI];
			uint32_t err[MAX_CHIPS_PER_SPI];
			int lastGoodChip = -1;
            for(uint8_t seq=0;seq<CHIPS_PER_SPI;seq++)if(GetChip(0,seq).IsEnabled())
            {
				sol[seq] = GetChip(0,seq).FoundSolutions - prevSolBoard[seq];
				err[seq] = GetChip(0,seq).FoundErrors    - prevErrBoard[seq];
				//if ((seq > 10 && seq < 30) || (seq == 77)) sol[seq] = 5; // DEBUG-TT: emulate working chips 10..30
				if (sol[seq] > 0) lastGoodChip = seq;
            }
			
            printf("Chip status [CHIP: STATUS]:\n");
            for (int row=5;row>=0;row--)
            {
                for (int col=0;col<13;col++)
                {
                    int seq = row*13 + (row%2==0? 12-col : col);
                    printf("[%02d:%2s]  ", seq+1, (sol[seq] > 1 ? "OK" : "  "));
                }
                printf("\n");
            }
            printf("\n");
            
            
            bool noChips = true;
            printf("PROBLEM CHIPS: ");
            for(uint8_t seq=0;seq<CHIPS_PER_SPI;seq++)if(GetChip(0,seq).IsEnabled())
            {
                if (sol[seq] == 0) {
                    printf("%u ", seq+1);
                    noChips = false;
                }
            }
            if (noChips) {
                printf("NONE");
            }
            printf("\n");
			
			if (lastGoodChip < CHIPS_PER_SPI-5) {
				printf("\n");
				printf("ERROR CHIP (broke rest of chain): %d\n", lastGoodChip+1+1);
				printf("!!! FIX OR REPLACE THIS CHIP !!!\n");
			}
			
            printf("\n");
			
			if (!isTestBoardConnected()) break;
			//break; // DEBUG-TT: emulate board disconnection after one test
        }
		printf("Board disconnected - test completed\n");
    }
}

void cMultyChipBoard::testUnitLoop()
{
    while (true)
    {
        printf("\n");
        printf("---------------------------------------\n");
        printf("-----          UNIT TEST          -----\n");
        printf("---------------------------------------\n");
        printf("\n");
		
		printf("*** Restart chips...\n");
        InitChips();
		
		printf("*** Running tests (%d seconds)...", g_testtoolTestTime);
	
		time_t startTime = time(NULL);
		time_t now = startTime;
		while (now < startTime + g_testtoolTestTime)
		{
			printf("."); fflush(stdout);
			
			RunLoop();
		
			usleep(1000 * g_polingDelay);
			now = time(NULL);
		}
		printf(" [done]\n");
		
		//DEBUG
		if (ttExtStat) PrintAllChipsInfo();
		
		printf("\n");
		printf("****** TEST RESULTS ******\n");
		for (int spi=0; spi<SPI_COUNT; spi++)
		{
			printf("BOARD %d (chip statuses):\n", spi+1);
			for (int row=5;row>=0;row--)
			{
				for (int col=0;col<13;col++)
				{
					int seq = row*13 + (row%2==0? 12-col : col);
					int sol = GetChip(spi,seq).FoundSolutions;
					int err = GetChip(spi,seq).FoundErrors;
					printf("[%02d:%2s]  ", seq+1, (sol > 1 ? "OK" : "  "));
				}
				printf("\n");
			}
			printf("\n");
		}
	}
}

int main(int argc,const char* argv[])
{
    SPI_COUNT = 1; CHIPS_PER_SPI = 10;

    // Parse command line arguments - pre processing
	// (changing some defaults per run mode)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-test") == 0) {
			g_runMode = RUN_MODE_TEST_BOARD;
            SPI_COUNT = 1; CHIPS_PER_SPI = 78;
		}
        if (strcmp(argv[i], "-test_unit") == 0) {
			g_runMode = RUN_MODE_TEST_UNIT;
            SPI_COUNT = 6; CHIPS_PER_SPI = 78;
			// full speed osc in unit test mode
			g_testtoolOsc = 54;
		}
    }
	
	if (g_runMode == RUN_MODE_TEST_BOARD || g_runMode == RUN_MODE_TEST_UNIT) {
		g_poolHost=""; g_poolPort=""; g_poolUser=""; g_poolPswd="";
	}

    printf("Usage (expected arguments):\n");
    printf("pool:\n");
    printf("  -host <hostname>          Set pool hostname (default: [%s])\n", g_poolHost);
    printf("  -port <port>              Set pool port     (default: [%s])\n", g_poolPort);
    printf("  -user <username>          Set pool username (default: [%s])\n", g_poolUser);
    printf("  -pswd <password>          Set pool password (default: [%s])\n", g_poolPswd);
    printf("grid:\n");
    printf("  -spi_cnt <num_of_spi>     Set number of SPI (default: %d)\n", SPI_COUNT);
    printf("  -chain_len <chain_len>    Set max length of the chip chain (default: %d)\n", CHIPS_PER_SPI);
    printf("  -chain_x_len <spi> <len>  Set spi <spi> length <len> (num of chips in chain)\n");
    printf("  -ds <spi>                 Disable all chips on <spi>\n");
    printf("  -dc <spi> <seq>           Disable chip <spi>/<seq>\n");
    printf("other:\n");
    printf("  -log_delay <sec>          Logging delay in seconds (default: %d)\n", g_logDelay);
    printf("  -poling_delay <ms>        Poling delay in mili-seconds (default: %d)\n", g_polingDelay);
    printf("  -spi_f <cycles>           Set SPI_CYCLES_PER_TAKT in clycles (default: %d)\n", SPI_CYCLES_PER_TAKT);
    printf("  -spi_f2 <cycles>          Set SPI_CYCLES_PER_TAKT_2 in clycles (default: %d)\n", SPI_CYCLES_PER_TAKT_2);
    printf("  -spi_reset <cycles>       Set SPI_RESET_CYCLES (default: %d)\n", SPI_RESET_CYCLES);
    printf("  -spi_r <cycles>           Set SPI_READ_CYCLES (default: %d)\n", SPI_READ_CYCLES);
    printf("  -spi_seq_init             Use sequential order init (default: smart order)\n");
    printf("  -power_reset              Send power reset signal to MC and exit (default: %d)\n", g_powerResetCycles);
    printf("  -power_reset_cycles <num> Number of power reset cycles (default: %d)\n", g_powerResetCycles);
    printf("test board mode:\n");
    printf("  -test                     Test Board mode: 1x78 board test\n");
    printf("  -test_unit                Test Unit mode: 6x78 unit test\n");
    printf("  -tt_osc                   OSC (default: %d)\n", g_testtoolOsc);
    printf("  -tt_test_t                Test time in seconds (default: %d)\n", g_testtoolTestTime);
    printf("  -tt_ext_stat              print extended statistic for each chip\n");
    printf("\n");
    
    // Parse command line arguments - pre processing
    for (int i = 1; i < argc; i++) {
        // pool
        if (strcmp(argv[i], "-host") == 0)    g_poolHost = argv[++i];
        if (strcmp(argv[i], "-port") == 0)    g_poolPort = argv[++i];
        if (strcmp(argv[i], "-user") == 0)    g_poolUser = argv[++i];
        if (strcmp(argv[i], "-pswd") == 0)    g_poolPswd = argv[++i];
        
        // grid
        if (strcmp(argv[i], "-spi_cnt") == 0)   SPI_COUNT = atoi(argv[++i]);
        if (strcmp(argv[i], "-chain_len") == 0) CHIPS_PER_SPI = atoi(argv[++i]);
        
        // other
        if (strcmp(argv[i], "-log_delay") == 0)		  g_logDelay = atoi(argv[++i]);
        if (strcmp(argv[i], "-poling_delay") == 0)    g_polingDelay = atoi(argv[++i]);
        if (strcmp(argv[i], "-spi_f") == 0)  		  SPI_CYCLES_PER_TAKT = atoi(argv[++i]);
        if (strcmp(argv[i], "-spi_f2") == 0)  		  SPI_CYCLES_PER_TAKT_2 = atoi(argv[++i]);
        if (strcmp(argv[i], "-spi_reset") == 0)		  SPI_RESET_CYCLES = atoi(argv[++i]);
        if (strcmp(argv[i], "-spi_r") == 0)		      SPI_READ_CYCLES = atoi(argv[++i]);
        if (strcmp(argv[i], "-spi_seq_init") == 0)	  g_spiSmartInit = false;
        if (strcmp(argv[i], "-power_reset_cycles") == 0) g_powerResetCycles = atoi(argv[++i]);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-power_reset") == 0) {
			cMultyBufferSPIExchange spi;
			spi.Init();
			spi.doPowerReset(g_powerResetCycles);
			return 0;
		}
	}

    printf("Pool: %s:%s (user %s)\n", g_poolHost, g_poolPort, g_poolUser);
    printf("Grid size: SPI_COUNT = %d, CHIPS_PER_SPI = %d\n", SPI_COUNT, CHIPS_PER_SPI);
    printf("\n");

    cMultyChipBoard Board(g_poolHost, g_poolPort, g_poolUser, g_poolPswd);

    // Enable all chips in the grid
    for (uint8_t seq = 0; seq < CHIPS_PER_SPI; seq++)
      for (uint8_t spi = 0; spi < SPI_COUNT; spi++)
        Board.GetChip(spi, seq).SetEnabled(true);
    
    // Parse command line arguments - post processing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-chain_x_len") == 0) {
            int spi = atoi(argv[++i]);
            int len = atoi(argv[++i]);
            for (uint8_t seq = len; seq < CHIPS_PER_SPI; seq++)
              Board.GetChip(spi, seq).SetEnabled(false);
        }
        if (strcmp(argv[i], "-ds") == 0) {
            int spi = atoi(argv[++i]);
            for (uint8_t seq = 0; seq < CHIPS_PER_SPI; seq++)
              Board.GetChip(spi, seq).SetEnabled(false);
        }
        if (strcmp(argv[i], "-dc") == 0) {
            int spi = atoi(argv[++i]);
            int seq = atoi(argv[++i]);
            Board.GetChip(spi, seq).SetEnabled(false);
        }
        
        if (strcmp(argv[i], "-tt_osc") == 0)        g_testtoolOsc = atoi(argv[++i]);
        if (strcmp(argv[i], "-tt_test_t") == 0)   	g_testtoolTestTime = atoi(argv[++i]);
        if (strcmp(argv[i], "-tt_ext_stat") == 0)   Board.ttExtStat = true;
    }
    
    
    // We have no USB yet!!!
    Board.InitAll(/*"dev/ttyACM0"*/);
    //if(!Board.IsUSBConnected())
    //{
    //   return 0;
    //}
    
    time_t StartWorkTime=time(NULL);
    time_t LastPrintWorkTime=time(NULL);
//    uint32_t SetupOSCValue=0;
    //bool NeedDisableOutput=false;
    Board.AllChipsInfoEnablePrint(false);
    
    if (g_runMode == RUN_MODE_TEST_BOARD)
    {
        Board.testToolLoop();
		return 0;
    }
    if (g_runMode == RUN_MODE_TEST_UNIT)
    {
        Board.testUnitLoop();
		return 0;
    }
    
    while(true)
    //for (int loop = 0; loop < 500; loop++) // debug go normal exit for profiler
    {
        Board.RunLoop();
        
        time_t now=time(NULL);
        if (now > LastPrintWorkTime + g_logDelay)
        {
            struct tm *timeinfo = localtime(&now);

            printf("%d.%d.%d %d:%d:%d: * Total * %.2fGH/s By diff:%.2fGH/s Diff=%u CS:%.2fGH/s(RJ=%u RN=%u)\n"
                   ,timeinfo->tm_mday, timeinfo->tm_mon+1, timeinfo->tm_year-100
                   ,timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
                   ,(double)Board.GetTotalSolutions()*60/(now-StartWorkTime)/60*4.295
                   ,(double)Board.GetTotalSolutionsByDifficulty()*60/(now-StartWorkTime)/60*4.295
                   ,Board.Poll.GetCurrentDifficulty()
                   ,(double)Board.GetTotalJobsDone()*3.17089382326/(now-StartWorkTime)
                   ,Board.Poll.GetJobCount()
                   ,Board.Poll.GetNonceCount());
            LastPrintWorkTime=now;
            Board.PrintAllChipsInfo();
            if(Board.IsUSB())
            {
                if(!Board.IsUSBConnected())
                {
                    printf("Erro no connection to board!\n");
                }
                printf("USB vcp errors:T:%u L:%u E:%u\n"
                       ,Board.GetUSBBusObject().GetFrameTimeOut()
                       ,Board.GetUSBBusObject().GetLenError()
                       ,Board.GetUSBBusObject().GetFrameError()
                       );
            }
            Board.CheckStallChips(true);
        }
        
        usleep(1000 * g_polingDelay);
    }
}

#endif

