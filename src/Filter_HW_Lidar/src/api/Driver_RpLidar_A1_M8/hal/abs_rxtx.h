// Copyright (c) 2025 CyberCortex Robotics SRL. All rights reserved
// Author: Sorin Mihai Grigorescu

#pragma once 

#include "types.h"

namespace rp{ namespace hal{

class serial_rxtx
{
public:
    enum{
        ANS_OK      = 0,
        ANS_TIMEOUT = -1,
        ANS_DEV_ERR = -2,
    };

    static serial_rxtx * CreateRxTx();
    static void ReleaseRxTx( serial_rxtx * );

    serial_rxtx():_is_serial_opened(false){}
    virtual ~serial_rxtx(){}

    virtual void flush( _u32 flags) = 0;

    virtual bool bind(const char * portname, _u32 baudrate, _u32 flags = 0) = 0;
    virtual bool open() = 0;
    virtual void close()  = 0;
    
    virtual int waitfordata(size_t data_count,_u32 timeout = -1, size_t * returned_size = NULL) = 0;

    virtual int senddata(const unsigned char * data, size_t size) = 0;
    virtual int recvdata(unsigned char * data, size_t size) = 0;

    virtual int waitforsent(_u32 timeout = -1, size_t * returned_size = NULL) = 0;
    virtual int waitforrecv(_u32 timeout = -1, size_t * returned_size = NULL) = 0;

    virtual size_t rxqueue_count() = 0;

    virtual void setDTR() = 0;
    virtual void clearDTR() = 0;
    virtual void cancelOperation() {}

    virtual bool isOpened()
    {
        return _is_serial_opened;
    }

protected:
    volatile bool   _is_serial_opened;
};

}}



