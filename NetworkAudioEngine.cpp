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

#define SAMPLE_RATE         44100
#define BLOCK_SIZE          1024
#define NUM_BLOCKS          32
#define NUM_CHANNELS        2
#define BIT_DEPTH           16
#define NUM_SAMPLE_FRAMES   (NUM_BLOCKS * BLOCK_SIZE)
#define FRAME_BYTES         (NUM_CHANNELS * BIT_DEPTH / 8)
#define BLOCK_BYTES         (BLOCK_SIZE * FRAME_BYTES)
#define BUFFER_BYTES        (NUM_BLOCKS * BLOCK_BYTES)

OSDefineMetaClassAndStructors(NetworkAudioEngine, IOAudioEngine)


bool NetworkAudioEngine::init()
{
    if (!IOAudioEngine::init(NULL)) {
        return false;
    }

    sock = NULL;

    memset (&sockaddr, 0, sizeof (sockaddr));
    sockaddr.sin_len = sizeof (sockaddr);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(ESD_PORT);
    sockaddr.sin_addr.s_addr = htonl (ESD_HOST);
        // FIXME load the address and port form somewhere sensible.

    return true;
}


bool NetworkAudioEngine::initHardware(IOService *provider)
{
    IOAudioSampleRate initialSampleRate;
    IOWorkLoop *wl;
    IOAudioStream *audioStream;
    IOAudioSampleRate rate;
    
    if (!IOAudioEngine::initHardware(provider)) {
        return false;
    }

    initialSampleRate.whole = SAMPLE_RATE;
    initialSampleRate.fraction = 0;
    setSampleRate(&initialSampleRate);
    setDescription("Network Audio Driver");
    setNumSampleFramesPerBuffer(NUM_SAMPLE_FRAMES);
    setSampleOffset(BLOCK_SIZE);

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

    blockTimeoutUS = 1000000 * BLOCK_SIZE / initialSampleRate.whole;
    
    if (!(wl = getWorkLoop())) {
        return false;
    }
     
    // command gate used by tcpSendThread to signal buffer wrap-around.
    commandGate = IOCommandGate::commandGate(
        this,
        (IOReturn(*)(OSObject*, void*, void*, void*, void*))doTimeStamp
    );
    if (!commandGate) {
        return false;
    }
    workLoop->addEventSource(commandGate);

    return true;
}


void NetworkAudioEngine::free()
{
    if (outputBuffer != NULL) {
        IOFree(outputBuffer, outputBufferSize);
        outputBuffer = NULL;
    }
    
    IOAudioEngine::free();
}

 
void NetworkAudioEngine::stop(IOService *provider)
{
    int error;
    int fstate;
    
    if (sock) {
        fstate = thread_funnel_set (network_flock, TRUE);
        soshutdown (sock, 2);
        error = soclose (sock);
        thread_funnel_set (network_flock, fstate);
        sock = NULL;
    }

    IOAudioEngine::stop(provider);
}

// See: http://cvs.gnome.org/lxr/source/esound/esd.h
#define ESD_PROTO_STREAM_PLAY 0x03
#define ESD_BITS16 0x0001
#define ESD_STEREO 0x0020
#define ESD_STREAM 0x0000
#define ESD_PLAY   0x1000
#define ESD_NAME_LEN 128 

IOReturn NetworkAudioEngine::esoundConnect()
{
    int error;
    int fstate;
    struct iovec iovec[6];
    struct uio uio;
    char key [] = "abababababababab";
    unsigned int endian =
        (unsigned int)(('E' << 24) + ('N' << 16) + ('D' << 8) + ('N'));
    int proto = ESD_PROTO_STREAM_PLAY;
    int format = ESD_BITS16 | ESD_STEREO | ESD_STREAM | ESD_PLAY;
    int rate = SAMPLE_RATE;
    char name[ESD_NAME_LEN];

    fstate = thread_funnel_set (network_flock, TRUE);

    error = socreate (AF_INET, &sock, SOCK_STREAM, IPPROTO_TCP);
    if (error) {
        thread_funnel_set (network_flock, fstate);
        return kIOReturnError;
    }

    // Connect to the esound daemon

    error = soconnect (sock, (struct sockaddr*) &sockaddr);
    if (error) {
        IOLog ("socconnect returned %d\n", error);
        soclose (sock);
        thread_funnel_set (network_flock, fstate);
        sock = NULL;
        return kIOReturnError;
    }

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
    thread_funnel_set (network_flock, fstate);

    return sock ? kIOReturnSuccess : kIOReturnError;
}

 
IOReturn NetworkAudioEngine::performAudioEngineStart()
{
    takeTimeStamp(false);
    currentBlock = 0;
    engine_running = 1;
    IOCreateThread ((void(*)(void*))tcpSendThread, this);
        // Start a thread to send data from the sample buffer

    return kIOReturnSuccess;
}

 
IOReturn NetworkAudioEngine::performAudioEngineStop()
{
    engine_running = 1; // Causes tcpSendThread to exit

    return kIOReturnSuccess;
}

 
UInt32 NetworkAudioEngine::getCurrentSampleFrame()
{
    return currentBlock * BLOCK_SIZE;
        // FIXME Does this need mutex protection from reading while
        //  being updated by tcpSendThread ?
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
        &(((float *)mixBuf)[i]),
        &(((int16_t *)sampleBuf)[i]),
        numSampleFrames * streamFormat->fNumChannels
    );

    return kIOReturnSuccess;
}

IOReturn NetworkAudioEngine::doTimeStamp (
    NetworkAudioEngine *audioEngine,
    void*, void*, void*, void*
) {
    audioEngine->takeTimeStamp();

    return kIOReturnSuccess;
}

static unsigned int wait_timeout_event;

void NetworkAudioEngine::tcpSendThread (NetworkAudioEngine *audioEngine)
{
    int error;
    int fstate;
    struct iovec iovec;
    struct uio uio;
    int newblock;
    AbsoluteTime ts, t;
    uint64_t uts, ut;

    uio.uio_iov = &iovec;
    uio.uio_iovcnt = 1;
    uio.uio_offset = 0;
    uio.uio_segflg = UIO_SYSSPACE;
    uio.uio_rw = UIO_WRITE;
    uio.uio_procp = NULL;

    while (audioEngine->engine_running) {
        clock_get_uptime(&ts);

        // Try to connect to the server if needed
        if (audioEngine->sock == NULL) {
            IOLog ("Connecting to esound server...\n");
            audioEngine->esoundConnect();
        }

        // If connected send a block
        if (audioEngine->sock != NULL) {
            iovec.iov_base =
                (char*) audioEngine->outputBuffer
                + (audioEngine->currentBlock * BLOCK_BYTES);
            iovec.iov_len = BLOCK_BYTES;
            uio.uio_resid = iovec.iov_len;
            uio.uio_iovcnt = 1;
            uio.uio_offset = 0;

            fstate = thread_funnel_set (network_flock, TRUE);
            error = sosend (audioEngine->sock, NULL, &uio, NULL, NULL, 0);
            if (error) {
                IOLog ("sosend (data) returned %d\n", error);
                soshutdown (audioEngine->sock, 2);
                soclose (audioEngine->sock);
                audioEngine->sock = NULL;
            }
            thread_funnel_set (network_flock, fstate);
        }

        // Go to sleep until the next block is ready.
        clock_get_uptime(&t);
        absolutetime_to_nanoseconds(ts, &uts);
        absolutetime_to_nanoseconds(t, &ut);
        assert_wait((event_t)&wait_timeout_event, THREAD_UNINT);
        thread_set_timer(
            (int)audioEngine->blockTimeoutUS - ((int)((ut - uts) / 1000)),
            NSEC_PER_USEC
        );
        thread_block(THREAD_CONTINUE_NULL);

        // Update the block count
        newblock = audioEngine->currentBlock + 1;
        if (newblock >= NUM_BLOCKS) {
            newblock = 0;
        }
        audioEngine->currentBlock = newblock;

        // If we wrapped arround, tell the engine
        if (newblock == 0) {
            audioEngine->commandGate->runCommand();
                // calls audioEngine->takeTimeStamp();
        }
    }
    IOExitThread();
}


/*==============================================================================
 End of file
==============================================================================*/
