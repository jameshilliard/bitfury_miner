#ifndef _SINHRO_OBJECTS_LIB_
#define _SINHRO_OBJECTS_LIB_

#include <pthread.h>

//! механизм обеспечения доступа к данным только одного потока построен на базе библиотеки pthread примитива pthread_mutex_t
class CMutex
{
protected:
	//! sinhro object
	pthread_mutex_t Mutex;
public:
	//! constructor
	inline CMutex()
	{
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&Mutex,&attr);
		pthread_mutexattr_destroy(&attr);
	}
	inline ~CMutex()
	{
		pthread_mutex_destroy(&Mutex);
	}
	//! \brief Lock object
	//! \return true - if object is locked; false - object locking failed (critical error)
	inline bool Lock()const
	{
		return pthread_mutex_lock((pthread_mutex_t*)&Mutex)==0;
	}
	//! \brief Try to Lock object
	//! \return true - if object is locked; false - object is busy by other thread
	inline bool TryLock()const
	{
		return pthread_mutex_trylock((pthread_mutex_t*)&Mutex)==0;
	}
	//! UnLock object
	inline bool UnLock()const
	{
		return pthread_mutex_unlock((pthread_mutex_t*)&Mutex)==0;
	}
	inline pthread_mutex_t* GetSystemMutexObj(){return &Mutex;}
	inline operator pthread_mutex_t*(){return &Mutex;}
};


//! auto unlock attached mutex object on delete itself
class CAutoMutexHolder
{
protected:
	const CMutex *lpObject;//!< ptr to Sinhro object (if NULL not locked)
public:
	//! constructor, initialize object with no attached mutex object
	CAutoMutexHolder(){ lpObject=NULL;}
	//! destructor, auto free object if attached
	~CAutoMutexHolder()
	{
		if(lpObject)// is attached object
		{
			lpObject->UnLock();// so unlock it
			lpObject=NULL;//and dettach from itself
		}
	}
	//! constructor, initialize object with free object
	//! \param lpObj object to auto free on delete
	CAutoMutexHolder(const CMutex *lpObj,bool NeedLock=false){ lpObject=lpObj;if(NeedLock && lpObject) lpObject->Lock();}
	//! attach object
	//! \param lpObj object to auto free on delete
	inline void Attach(const CMutex *lpObj){ lpObject=lpObj;}
	//! dettach object
	inline void Dettach(){ lpObject=NULL;}
	//! return attached object, or NULL if no attached object
	inline const CMutex* GetAttachedObject(){return lpObject;}
};

#define ENTER_ONE_THREAD(name) \
	CAutoMutexHolder _cs_UnloadMutex_(&name,true)
#define ENTER_ONE_THREAD_LEAVE(name) { _cs_UnloadMutex_.Dettach();(name).UnLock();}
#define ENTER_ONE_THREAD_ENTER(name) { (name).Lock();_cs_UnloadMutex_.Attach(&name);}

#define AUTO_UNLOCK_CMUTEX(name) \
	CAutoMutexHolder _cs_UnloadMutex_(&name)

#define ENTER_ONE_THREAD_IF(name,k) \
	CAutoMutexHolder _cs_UnloadMutex_((k)?&name:NULL,true)

//! механизм обеспечения доступа множетсвенного на чтения и только ексклюзивного на запись к данным построен на базе библиотеки pthread примитива pthread_rwlock_t
class CReadWriteLock
{
protected:
    //! sinhro object
    pthread_rwlock_t rwlock;
public:
    CReadWriteLock()
    {
        //pthread_rwlockattr_t attr;
        //pthread_rwlockattr_init(&attr);
        pthread_rwlock_init(&rwlock,NULL);
        //pthread_rwlockattr_destroy(&attr);
    }
    ~CReadWriteLock()
    {
        pthread_rwlock_destroy(&rwlock);
    }
    //! Получить доступ как читатель
    inline bool ReadLock()
    {
        return pthread_rwlock_rdlock(&rwlock)==0;
    }
    //! попробовать получить доступ как читатель, елси не успешно просто вернутся
    inline bool ReadLockTry()
    {
        return pthread_rwlock_tryrdlock(&rwlock)==0;
    }
    //! Получить доступ как писатель
    inline bool WriteLock()
    {
        return pthread_rwlock_wrlock(&rwlock)==0;
    }
    //! попробовать получить доступ как писатель, елси не успешно просто вернутся
    inline bool WriteLockTry()
    {
        return pthread_rwlock_trywrlock(&rwlock)==0;
    }
    //! освободить занятый ресурс
    inline bool UnLock()
    {
        return pthread_rwlock_unlock(&rwlock);
    }
    //! Получить доступ как читатель, с указнием абсолютного времени когда наступит "время вышло" и функция вернется не получив доступа
    inline bool ReadLockTimed(const timespec *abs_timeout)
    {
        return pthread_rwlock_timedrdlock(&rwlock,abs_timeout);
    }
    //! Получить доступ как писатель, с указнием абсолютного времени когда наступит "время вышло" и функция вернется не получив доступа
    inline bool WriteLockTimed(const timespec *abs_timeout)
    {
        return pthread_rwlock_timedwrlock(&rwlock,abs_timeout);
    }
};

//! auto unlock attached mutex object on delete itself
class CAutoReadWriteLockHolder
{
protected:
	CReadWriteLock *lpObject;//!< ptr to Sinhro object (if NULL not locked)
public:
	//! constructor, initialize object with no attached mutex object
	CAutoReadWriteLockHolder(){ lpObject=NULL;}
	//! destructor, auto free object if attached
	~CAutoReadWriteLockHolder()
	{
		if(lpObject)// is attached object
		{
			lpObject->UnLock();// so unlock it
			lpObject=NULL;//and dettach from itself
		}
	}
	//! constructor, initialize object with free object
	//! \param lpObj object to auto free on delete
	CAutoReadWriteLockHolder(CReadWriteLock *lpObj,bool NeedLock=false,bool Writer=false)
	{
	    lpObject=lpObj;
	    if(NeedLock && lpObject)
        {
            if(Writer)
                lpObject->WriteLock();
            else
                lpObject->ReadLock();
        }
    }
	//! attach object
	//! \param lpObj object to auto free on delete
	inline void Attach(CReadWriteLock *lpObj){ lpObject=lpObj;}
	//! dettach object
	inline void Dettach(){ lpObject=NULL;}
	//! return attached object, or NULL if no attached object
	inline CReadWriteLock* GetAttachedObject(){return lpObject;}
};

#define ENTER_THREAD_READ_LOCK(name) \
	CAutoReadWriteLockHolder _cs_UnloadRWLock_(&name,true,false)
#define ENTER_THREAD_WRITE_LOCK(name) \
	CAutoReadWriteLockHolder _cs_UnloadRWLock_(&name,true,true)

#define AUTO_UNLOCK_RW(name) \
	CAutoReadWriteLockHolder _cs_UnloadRWLock_(&name)

#define ENTER_THREAD_READ_LOCK_IF(name,k) \
	CAutoReadWriteLockHolder _cs_UnloadRWLock_((k)?&name:NULL,true,false)
#define ENTER_THREAD_WRITE_LOCK_IF(name,k) \
	CAutoReadWriteLockHolder _cs_UnloadRWLock_((k)?&name:NULL,true,true)

//! механизм обеспечения сигнализации через условные сигналы
class CConditionalVariable
{
protected:
    //! sinhro object
    pthread_cond_t cond;
public:
    CConditionalVariable()
    {
        //pthread_rwlockattr_t attr;
        //pthread_rwlockattr_init(&attr);
        pthread_cond_init(&cond,NULL);
        //pthread_rwlockattr_destroy(&attr);
    }
    ~CConditionalVariable()
    {
        pthread_cond_destroy(&cond);
    }
    //! Получить доступ как читатель
    inline bool LockedWait(CMutex &M)
    {
        return pthread_cond_wait(&cond,M)==0;
    }
    inline bool LockedWaitTimed(CMutex &M,const timespec *abs_timeout)
    {
        return pthread_cond_timedwait(&cond,M,abs_timeout)==0;
    }
    inline void Signal()
    {
        pthread_cond_signal(&cond);
    }
};

#endif
