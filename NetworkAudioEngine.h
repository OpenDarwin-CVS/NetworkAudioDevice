/*==============================================================================
 $Id$ 
================================================================================
 
 NetworkAudioEngine.
 IOAudioEngine subclass for sending audio to an esound server.

================================================================================

 Copyright Sam O'Connor <samoconnor@mac.com>

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 The name of the author may not be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================

 If you make use of this software I would be interested to hear about it,
 this is just a request not a condition of use. mailto:samoconnor@mac.com

==============================================================================*/

#ifndef _NETWORKAUDIOENGINE_H
#define _NETWORKAUDIOENGINE_H

#include <IOKit/audio/IOAudioEngine.h>
extern "C" {
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#undef max(a,b)
#undef min(a,b)
#include <sys/systm.h>
}

#include "NetworkAudioDevice.h"

#define NetworkAudioEngine org_samoconnor_driver_NetworkAudioEngine

class NetworkAudioEngine : public IOAudioEngine
{
    OSDeclareDefaultStructors(NetworkAudioEngine)

public:
    
    UInt32 outputBufferSize;
    void *outputBuffer;
    
    UInt32 blockSize;
    UInt32 numBlocks;
    UInt32 currentBlock;
    UInt32 blockTimeoutUS;

    IOCommandGate* commandGate;

    UInt32 engine_running;
    
    struct socket* sock;
    struct sockaddr_in sockaddr;

    virtual bool init();
    virtual void free();
    
    virtual bool initHardware(IOService *provider);
    virtual void stop(IOService *provider);
    
    virtual IOReturn performAudioEngineStart();
    virtual IOReturn performAudioEngineStop();
    
    virtual UInt32 getCurrentSampleFrame();
    
    virtual IOReturn clipOutputSamples(
        const void *mixBuf,
        void *sampleBuf,
        UInt32 firstSampleFrame,
        UInt32 numSampleFrames,
        const IOAudioStreamFormat *streamFormat,
        IOAudioStream *audioStream
    );
    
    virtual IOReturn esoundConnect(void);
    
    static IOReturn doTimeStamp
        (NetworkAudioEngine *audioEngine, void*, void*, void*, void*);

    static void NetworkAudioEngine::tcpSendThread
        (NetworkAudioEngine *audioEngine);

};

#endif /* _NETWORKAUDIOENGINE_H */

/*==============================================================================
 End of file
==============================================================================*/
