/*
  File:PCMBlitterLibPPC.h

  Cut down version of original from Apple's PhantomAudioDriver example.
  See: http://www.opendarwin.org/cgi-bin/cvsweb.cgi/src/IOAudioFamily/Examples/PhantomAudioDriver/

*/

#ifndef __PCMBlitterLibPPC_h__
#define __PCMBlitterLibPPC_h__

#ifdef __cplusplus
extern "C" {
#endif

void Float32ToNativeInt16(const float *src, signed short *dst, unsigned int count);

#ifdef __cplusplus
};
#endif


#endif // __PCMBlitterLibPPC_h__
