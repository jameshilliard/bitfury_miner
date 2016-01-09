#ifndef MYSYSTEMUTILS_H_INCLUDED
#define MYSYSTEMUTILS_H_INCLUDED
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/sockios.h>

class CFileDescriptor
{
protected:
	int fd;
public:
	CFileDescriptor(){fd=-1;}
	CFileDescriptor(int NewFd){fd=NewFd;}
	~CFileDescriptor(){Close();}

	void Close(){if(fd>0){close(fd);fd=-1;}}
	void Clear(){fd=-1;}
	bool IsPresent()const{return fd>0;}
	int GetDescriptor()const{return fd;}

	operator int()const{return fd;}
	CFileDescriptor& operator=(int NewFd){Close();fd=NewFd;return *this;}
	inline ssize_t Send(const void *Buff,size_t len){return send(fd,Buff,len,0);}
	int GetSendBufferUsed(){int val=-1;ioctl(fd,SIOCOUTQ,&val);return val;}
	int GetSendBufferSize(){int val=-1;socklen_t len=sizeof(val);getsockopt(fd,SOL_SOCKET,SO_RCVBUF,&val,&len);return val;}
	int GetSendBufferFree(){return GetSendBufferSize()-GetSendBufferUsed();}
};

//!threads templates
template <class ClassType,void (ClassType::*NeedToCallFunction)(),bool NeedCreateControllPipes=false>
class ThreadRunnerWithControlPipes
{
//typedef void (ClassType::*NeedToCallFunction)();
protected:
	//ClassType *CallObj;
	//NeedToCallFunction RunFunction;
	CFileDescriptor SocketPipe[2];

	static void* StartThreadProc(void *Ptr)
	{
		pthread_detach(pthread_self());
		//ThreadRunner<ClassType,NeedCreateControllPipes> *Obj=static_cast<ThreadRunner<ClassType,NeedCreateControllPipes>*>(Ptr);
		//((*Obj->CallObj).*(Obj->RunFunction))();
		ClassType *Obj=static_cast<ClassType*>(Ptr);
		((*Obj).*NeedToCallFunction)();
		return NULL;
	}
public:
	CFileDescriptor& GetThreadPartPipe(){return SocketPipe[1];}
	CFileDescriptor& GetControllPartPipe(){return SocketPipe[0];}
	//ThreadRunner(ClassType *NeedCallObj,NeedToCallFunction NeedRunFunction):CallObj(NeedCallObj),RunFunction(NeedRunFunction)
	ThreadRunnerWithControlPipes()
	{
		if(NeedCreateControllPipes)
		{
			int k[2],res;
			if((res=socketpair(AF_LOCAL, SOCK_STREAM, 0,k))==0)
			{
				SocketPipe[0]=k[0];
				SocketPipe[1]=k[1];
			}
		}
	}
	bool IsHaveControllPipes(){return SocketPipe[0].IsPresent() && SocketPipe[1].IsPresent();}
	bool RunThread(ClassType *NeedCallObj,size_t StackSize=1024*1024) //1Mb
	{
		pthread_attr_t thread_attr;
		pthread_attr_init(&thread_attr);
		pthread_attr_setstacksize(&thread_attr,StackSize);
		pthread_t th_id;
		pthread_create(&th_id,&thread_attr,StartThreadProc,NeedCallObj);
		pthread_attr_destroy(&thread_attr);
		return true;
	}
	char inline GetCharForExitCommand() {return 'S';}
	bool inline isExitCommand(char Cmd) {return Cmd==GetCharForExitCommand();}
	int CheckNextThreadCommandForExit()
	{
		char Command=0;
		recv(SocketPipe[1],&Command,1,0);
		if(Command==GetCharForExitCommand())
		{
			send(SocketPipe[1],&Command,1,0);
			return 1;
		}
		return 0;
	}
	int GetNextThreadCommand()
	{
		char Command=0;
		recv(SocketPipe[1],&Command,1,0);
		return Command;
	}
	void SendThreadCommandExitAsk()
	{
		char Command=GetCharForExitCommand();
		send(SocketPipe[1],&Command,1,0);
	}
	int SendToThread(char Command)
	{
	    if(!IsHaveControllPipes()) return -10;
	    return SocketPipe[0].Send(&Command,1);
	}
	int StopThread(int WaitTime=-2)
	{
		if(!IsHaveControllPipes()) return -10;
		char Command=GetCharForExitCommand();
		SocketPipe[0].Send(&Command,1);
		if(WaitTime>-2)
		{
			pollfd Events[1];
			Events[0].fd=SocketPipe[0];
			Events[0].events=POLLIN;
			while(poll(Events,1,WaitTime)<0);
			if(Events[0].revents&POLLIN)
			{
				recv(SocketPipe[0],&Command,1,0);
				return 1;
			}
			return 0;
		}
		return 0;
	}
};

//!threads templates
template <class ClassType,void (ClassType::*NeedToCallFunction)()>
class CSimpleThreadRunner
{
protected:
	static void* StartThreadProc(void *Ptr)
	{
		pthread_detach(pthread_self());
		//ThreadRunner<ClassType,NeedCreateControllPipes> *Obj=static_cast<ThreadRunner<ClassType,NeedCreateControllPipes>*>(Ptr);
		//((*Obj->CallObj).*(Obj->RunFunction))();
		ClassType *Obj=static_cast<ClassType*>(Ptr);
		((*Obj).*NeedToCallFunction)();
		return NULL;
	}
public:
	CSimpleThreadRunner()
	{
	}
	bool RunThread(ClassType *NeedCallObj,size_t StackSize=1024*1024) //1Mb
	{
		pthread_attr_t thread_attr;
		pthread_attr_init(&thread_attr);
		pthread_attr_setstacksize(&thread_attr,StackSize);
		pthread_t th_id;
		pthread_create(&th_id,&thread_attr,StartThreadProc,NeedCallObj);
		pthread_attr_destroy(&thread_attr);
		return true;
	}
};


#endif // MYSYSTEMUTILS_H_INCLUDED
