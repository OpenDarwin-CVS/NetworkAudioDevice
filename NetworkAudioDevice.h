/*==============================================================================
 $Id$ 
================================================================================
 
 NetworkAudioDevice.
 IOAudioDevice subclass for sending audio to an esound server.

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

#ifndef _NETWORKAUDIODEVICE_H
#define _NETWORKAUDIODEVICE_H

#include <IOKit/audio/IOAudioDevice.h>

#define NetworkAudioDevice org_samoconnor_driver_NetworkAudioDevice

class NetworkAudioDevice : public IOAudioDevice
{
    friend class org_samoconnor_driver_NetworkAudioEngine;
    
    OSDeclareDefaultStructors(NetworkAudioDevice)
    
    virtual bool initHardware(IOService *provider);
    
};

#endif /* _NETWORKAUDIODEVICE_H */

/*==============================================================================
 End of file
==============================================================================*/
