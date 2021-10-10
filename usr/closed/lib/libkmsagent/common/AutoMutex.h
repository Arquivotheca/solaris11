/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*------------------------------------------------------------------------------
 * Module           : AutoMutex.h
 -----------------------------------------------------------------------------*/

#ifndef AutoMutex_h
#define AutoMutex_h

#include "SYSCommon.h"

class CAutoMutex 
{
public:

    /*---------------------------------------------------------------------------
     * Constructor:
     *  Locks the given mutex handle.
     *
     * Input
     * -----
     *    i_hMutex        Mutex handle
     *
     * Output
     * ------
     *    (none)
     *
     * Return value       (none)
     *
     *--------------------------------------------------------------------------*/

    CAutoMutex( K_MUTEX_HANDLE i_hMutex ) 
        : m_hMutex( 0 ),
          m_bLocked( false )
    {
        if ( i_hMutex )
        {
            Lock( i_hMutex );
        }
    }


    /*---------------------------------------------------------------------------
     * Destructor:
     *  Unlocks this mutex.
     *
     * Input
     * -----
     *    (none)
     *
     * Output
     * ------
     *    (none)
     *
     * Return value       (none)
     *
     *--------------------------------------------------------------------------*/

    virtual ~CAutoMutex() 
    { 
        if ( m_bLocked )
        {
            Unlock();
        }
    }

    /*---------------------------------------------------------------------------
     * Function: Lock
     *
     * Description:
     *  Locks this mutex handle.  If i_hMutex is null, the handle passed to the
     *  constructor will be used.  Fatals if there is no valid handle.
     *
     * Input
     * -----
     *    i_hMutex        Mutex handle to lock
     *
     * Output
     * ------
     *    (none)
     *
     * Return value       (none)
     *
     *--------------------------------------------------------------------------*/

    void Lock( K_MUTEX_HANDLE i_hMutex = 0 )
    {
        FATAL_ASSERT( !m_bLocked );

        if ( i_hMutex )
        {
            m_hMutex = i_hMutex;
        }

        FATAL_ASSERT( m_hMutex );        
        K_LockMutex( m_hMutex );
        m_bLocked = true;
    }


    /*---------------------------------------------------------------------------
     * Function: Unlock
     *
     * Description:
     *  Unlocks the mutex handle passed to the constructor or to a previous 
     *  Lock call.  Fatals if the mutex is not locked.
     *
     * Input
     * -----
     *    (none)
     *
     * Output
     * ------
     *    (none)
     *
     * Return value       (none)
     *
     *--------------------------------------------------------------------------*/

    void Unlock()
    {
        FATAL_ASSERT( m_bLocked );
        FATAL_ASSERT( m_hMutex );
        K_UnlockMutex( m_hMutex );
        m_bLocked = false;
    }

private:
    K_MUTEX_HANDLE m_hMutex;
    bool m_bLocked;
};


#endif // AutoMutex_h
