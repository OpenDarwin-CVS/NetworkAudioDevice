/*==============================================================================
 $Id$ 
================================================================================
 
 NetworkAudioEngine implementation.
 IOAudioEngine subclass for sending audio to an esound server.
 See: http://www.tux.org/~ricdude/EsounD.html
      http://cvs.gnome.org/lxr/source/esound/
      http://asd.sourceforge.net/

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

#include "NetworkAudioEngine.h"

#include "PCMBlitterLibPPC.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>

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
#include <kern/clock.h>
}

#define ESD_HOST 0x0A000167 // 10.0.1.103
#define ESD_PORT 16001

// See: http://cvs.gnome.org/lxr/source/esound/esd.h
#define ESD_PROTO_STREAM_PLAY 0x03
#define ESD_BITS16 0x0001
#define ESD_STEREO 0x0020
#define ESD_STREAM 0x0000
#define ESD_PLAY   0x1000
#define ESD_NAME_LEN 128 

#define SAMPLE_RATE         44100
#define BLOCK_SIZE          256
#define NUM_BLOCKS          16
#define NUM_CHANNELS        2
#define BIT_DEPTH           16
#define NUM_SAMPLE_FRAMES   (NUM_BLOCKS * BLOCK_SIZE)
#define FRAME_BYTES         (NUM_CHANNELS * BIT_DEPTH / 8)
#define BLOCK_BYTES         (BLOCK_SIZE * FRAME_BYTES)
#define BUFFER_BYTES        (NUM_BLOCKS * BLOCK_BYTES)
#define NSEC_PER_BLOCK      ((1000000000 / SAMPLE_RATE) * BLOCK_SIZE)

static unsigned int wait_timeout_event;


static void _tcpSendThread (void* o)
{
    ((NetworkAudioEngine*)o)->tcpSendThread();
}


static IOReturn _takeTimeStamp (
    OSObject* o,
    void*, void*, void*, void*
) {
    NetworkAudioEngine* ae = OSDynamicCast(NetworkAudioEngine, o);
    if (ae) {
        ae->takeTimeStamp();
    }
    return kIOReturnSuccess;
}


IOReturn handleSleep (
	void *target,
	void *refCon,
	UInt32 messageType,
	IOService *service,
    	void *messageArgument,
	vm_size_t argSize
) {
    sleepWakeNote *swNote = (sleepWakeNote *)messageArgument;

    switch (messageType) {
        case kIOMessageSystemWillSleep:
            IOLog ("about to sleep\n");
            ((NetworkAudioEngine*)target)->performAudioEngineDisconnect();
            break;
        case kIOMessageSystemHasPoweredOn:
            IOLog ("just woke up\n");
            break;
        default:
            return kIOReturnUnsupported;
    }

    swNote->returnValue = 0;
    acknowledgeSleepWakeNotification(swNote->powerRef);
  
    return kIOReturnSuccess;
}


OSDefineMetaClassAndStructors(NetworkAudioEngine, IOAudioEngine)


bool NetworkAudioEngine::init()
{
    IOLog ("NetworkAudioEngine::init()\n");

    if (!IOAudioEngine::init(NULL)) {
        return false;
    }

    lock = IOLockAlloc();
    sock = NULL;
    disconnect = 0;

    memset (&sockaddr, 0, sizeof (sockaddr));
    sockaddr.sin_len = sizeof (sockaddr);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(ESD_PORT);
    sockaddr.sin_addr.s_addr = htonl (ESD_HOST);
        // FIXME load the address and port form somewhere sensible.

    sleep_callback = registerSleepWakeInterest(&handleSleep, this);

    return true;
}


bool NetworkAudioEngine::initHardware(IOService *provider)
{
    IOAudioSampleRate initialSampleRate;
    IOWorkLoop *wl;
    IOAudioStream *audioStream;
    IOAudioSampleRate rate;

    IOLog ("NetworkAudioEngine::initHardware(%X)\n", (unsigned) provider);
    IOLog ("SAMPLE_RATE %d\n", SAMPLE_RATE);
    IOLog ("BLOCK_SIZE %d\n", BLOCK_SIZE);
    IOLog ("NUM_BLOCKS %d\n", NUM_BLOCKS);
    IOLog ("NUM_CHANNELS %d\n", NUM_CHANNELS);
    IOLog ("BIT_DEPTH %d\n", BIT_DEPTH);
    IOLog ("NUM_SAMPLE_FRAMES %d\n", NUM_SAMPLE_FRAMES);
    IOLog ("FRAME_BYTES %d\n", FRAME_BYTES);
    IOLog ("BLOCK_BYTES %d\n", BLOCK_BYTES);
    IOLog ("BUFFER_BYTES %d\n", BUFFER_BYTES);
    IOLog ("NSEC_PER_BLOCK %d\n", NSEC_PER_BLOCK);
    
    if (!IOAudioEngine::initHardware(provider)) {
        return false;
    }

    initialSampleRate.whole = SAMPLE_RATE;
    initialSampleRate.fraction = 0;
    setSampleRate(&initialSampleRate);
    setDescription("Network Audio Driver");
    setNumSampleFramesPerBuffer(NUM_SAMPLE_FRAMES);
//    setSampleOffset(BLOCK_SIZE);

    if (!(outputBuffer = (SInt16 *)IOMalloc(BUFFER_BYTES))) {
        return false;
    }
    if (!(audioStream = new IOAudioStream)) {
        return false;
    }
    if (!audioStream->initWithAudioEngine(
        this, kIOAudioStreamDirectionOutput, 1
    )) {
        audioStream->release();
        return false;
    }
    IOAudioStreamFormat format = {
        NUM_CHANNELS,
        kIOAudioStreamSampleFormatLinearPCM,
        kIOAudioStreamNumericRepresentationSignedInt,
        BIT_DEPTH,
        BIT_DEPTH,
        kIOAudioStreamAlignmentHighByte,
        kIOAudioStreamByteOrderBigEndian,
        true,
        0
    };
    audioStream->setSampleBuffer(outputBuffer, BUFFER_BYTES);
    rate.fraction = 0;
    rate.whole = SAMPLE_RATE;
    audioStream->addAvailableFormat(&format, &rate, &rate);
    audioStream->setFormat(&format);
    addAudioStream(audioStream);
    audioStream->release();

    if (!(wl = getWorkLoop())) {
        return false;
    }
     
    doTakeTimeStamp = IOCommandGate::commandGate(this, _takeTimeStamp);
    if (!doTakeTimeStamp) {
        return false;
    }
    workLoop->addEventSource(doTakeTimeStamp);

    return true;
}


void NetworkAudioEngine::free()
{
    IOLog ("NetworkAudioEngine::free()\n");

    sleep_callback->remove();

    disconnect = 1;
    engine_running = 0; // Causes tcpSendThread to exit
    IOLockLock(lock);
    IOLockUnlock(lock);
    IOLockFree(lock);

    if (outputBuffer != NULL) {
        IOFree(outputBuffer, outputBufferSize);
        outputBuffer = NULL;
    }
    
    IOAudioEngine::free();
}

 
void NetworkAudioEngine::stop(IOService *provider)
{
    IOLog ("NetworkAudioEngine::stop(%X)\n", (unsigned) provider);

    disconnect = 1;
    engine_running = 0; // Causes tcpSendThread to exit
    IOLockLock(lock);
    IOLockUnlock(lock);

    IOAudioEngine::stop(provider);
}

 
IOReturn NetworkAudioEngine::performAudioEngineStart()
{
    IOThread thread;   

    IOLog ("NetworkAudioEngine::performAudioEngineStart()\n");

    takeTimeStamp(false);
    currentBlock = 0;
    engine_running = 1;
    thread = IOCreateThread (_tcpSendThread, this);
        // Start a thread to send data from the sample buffer

    return kIOReturnSuccess;
}

 
IOReturn NetworkAudioEngine::performAudioEngineStop()
{
    IOLog ("NetworkAudioEngine::performAudioEngineStop()\n");

    engine_running = 0; // Causes tcpSendThread to exit
    IOLockLock(lock);
    IOLockUnlock(lock);

    return kIOReturnSuccess;
}


void NetworkAudioEngine::performAudioEngineDisconnect()
{
    IOLog ("NetworkAudioEngine::performAudioEngineDisconnect()\n");

    disconnect = 1;
    performAudioEngineStop();
}

 
UInt32 NetworkAudioEngine::getCurrentSampleFrame()
{
//    IOLog ("NetworkAudioEngine::getCurrentSampleFrame()\n");

    return currentBlock * BLOCK_SIZE;
}


IOReturn NetworkAudioEngine::clipOutputSamples(
    const void *mixBuf,
    void *sampleBuf,
    UInt32 firstSampleFrame,
    UInt32 numSampleFrames,
    const IOAudioStreamFormat *streamFormat,
    IOAudioStream *audioStream
) {
    UInt32 i = firstSampleFrame * streamFormat->fNumChannels;

    Float32ToNativeInt16(
        &(((const float *)mixBuf)[i]),
        &(((int16_t *)sampleBuf)[i]),
        numSampleFrames * streamFormat->fNumChannels
    );

    return kIOReturnSuccess;
}


void NetworkAudioEngine::tcpSendThread (void) {
    int error;
    struct iovec iovec[6];
    struct uio uio;
    AbsoluteTime ticks = {0,0};
    AbsoluteTime ticks_per_block;
    char key [] = "abababababababab";
    unsigned int endian =
        (unsigned int)(('E' << 24) + ('N' << 16) + ('D' << 8) + ('N'));
    int proto = ESD_PROTO_STREAM_PLAY;
    int format = ESD_BITS16 | ESD_STEREO | ESD_STREAM | ESD_PLAY;
    int rate = SAMPLE_RATE;
    char name[ESD_NAME_LEN];

    IOLog ("TCP Send thread started\n");
    IOLockLock(lock);

    nanoseconds_to_absolutetime(NSEC_PER_BLOCK, &ticks_per_block);   

    clock_get_uptime(&ticks);

    while (engine_running) {
        thread_funnel_set (network_flock, TRUE);

        // Try to connect to the server if needed
        if (sock == NULL) {
            IOLog ("Connecting to esound server...\n");

            error = socreate (AF_INET, &sock, SOCK_STREAM, IPPROTO_TCP);
            if (error) {
                IOLog ("socreate failed!\n");
            } else {
                // Connect to the esound daemon
                error = soconnect (sock, (struct sockaddr*) &sockaddr);
                if (error) {
                    IOLog ("socconnect returned %d\n", error);
                    soclose (sock);
                    sock = NULL;
                } else {
                    // Prepare an ESD header: key, endian, proto, format, rate, name

                    strcpy (name, "mac");
                    iovec[0].iov_base = (char*) key;
                    iovec[0].iov_len = strlen (key);
                    iovec[1].iov_base = (char*) &endian;
                    iovec[1].iov_len = sizeof (endian);
                    iovec[2].iov_base = (char*) &proto;
                    iovec[2].iov_len = sizeof (proto);
                    iovec[3].iov_base = (char*) &format;
                    iovec[3].iov_len = sizeof (format);
                    iovec[4].iov_base = (char*) &rate;
                    iovec[4].iov_len = sizeof (rate);
                    iovec[5].iov_base = (char*) name;
                    iovec[5].iov_len = ESD_NAME_LEN;

                    uio.uio_iov = iovec;
                    uio.uio_iovcnt = 6;
                    uio.uio_offset = 0;
                    uio.uio_resid = 0;
                    for (int i = 0 ; i < uio.uio_iovcnt ; i++) {
                        uio.uio_resid += iovec[i].iov_len;
                    }
                    uio.uio_segflg = UIO_SYSSPACE;
                    uio.uio_rw = UIO_WRITE;
                    uio.uio_procp = NULL;

                    // Wait for the socket to settle down after creation
                    sbwait((sockbuf*)&(sock->so_snd));

                    // Send the ESD header
                    error = sosend (sock, NULL, &uio, NULL, NULL, 0);
                    if (error) {
                        IOLog ("sosend (header) returned %d\n", error);
                        soclose (sock);
                        sock = NULL;
                    }
                }
            }
        }

        // If connected send a block
        if (sock != NULL) {
            iovec[0].iov_base =
                (char*) outputBuffer
                + (currentBlock * BLOCK_BYTES);
            iovec[0].iov_len = BLOCK_BYTES;
            uio.uio_iov = iovec;
            uio.uio_resid = iovec[0].iov_len;
            uio.uio_iovcnt = 1;
            uio.uio_offset = 0;
            uio.uio_segflg = UIO_SYSSPACE;
            uio.uio_rw = UIO_WRITE;
            uio.uio_procp = NULL;

            error = sosend (sock, NULL, &uio, NULL, NULL, 0);
            if (error) {
                IOLog ("sosend (data) returned %d\n", error);
                soshutdown (sock, 2);
                soclose (sock);
                sock = NULL;
            }
        } else {
            IOLog ("no socket!\n");
        }

        thread_funnel_set (network_flock, FALSE);

        // Update the block count
        if (currentBlock == NUM_BLOCKS-1) {
            // If we wrapped arround, tell the engine
            currentBlock = 0;
            doTakeTimeStamp->runCommand();
        } else {
            currentBlock++;
        }

        if  (engine_running) {
            // Go to sleep until the next block is ready.
            ADD_ABSOLUTETIME(&ticks, &ticks_per_block);
            assert_wait((event_t)&wait_timeout_event, THREAD_UNINT);
            thread_set_timer_deadline (ticks);
            thread_block(THREAD_CONTINUE_NULL);
        }
    }
    if  (disconnect) {
        thread_funnel_set (network_flock, TRUE);
        soshutdown (sock, 2);
        soclose (sock);
        thread_funnel_set (network_flock, FALSE);
        sock = NULL;
    }
    IOLog ("TCP Send thread exiting...\n");
    IOLockUnlock(lock);
    IOExitThread();
    IOLog ("!!This message shold never appear!!\n");
}


/*==============================================================================
 End of file
==============================================================================*/
