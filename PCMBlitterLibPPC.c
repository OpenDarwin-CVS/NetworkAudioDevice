/*
  File:PCMBlitterLibPPC.c

  Cut down version of original from Apple's PhantomAudioDriver example.
  See: http://www.opendarwin.org/cgi-bin/cvsweb.cgi/src/IOAudioFamily/Examples/PhantomAudioDriver/

*/

#include "PCMBlitterLibPPC.h"

#define __stfiwx( value, offset, addr )			\
	asm( "stfiwx %0, %1, %2" : /*no result*/ : "f" (value), "b%" (offset), "r" (addr) : "memory" )

static inline double __fctiw( register double B )
{
	register double result;
	asm( "fctiw %0, %1" : "=f" (result) : "f" (B)  );
	return result;
}

void Float32ToNativeInt16( float *src, signed short *dst, unsigned int count )
{
	register double		scale = 2147483648.0;
	register double		round = 32768.0;
	unsigned long		loopCount = count / 4;
	long				buffer[2];
	register float		startingFloat;
	register double scaled;
	register double converted;
	register short		copy;

	if( count >= 6 )
	{
		startingFloat = (src++)[0];
		scaled = startingFloat * scale + round;
		startingFloat = (src++)[0];
		converted = __fctiw( scaled );
		scaled = startingFloat * scale + round;
		startingFloat = (src++)[0];
		__stfiwx( converted, 0, buffer );
		converted = __fctiw( scaled );
		scaled = startingFloat * scale + round;
		startingFloat = (src++)[0];
		__stfiwx( converted, sizeof(float), buffer );
		converted = __fctiw( scaled );
		scaled = startingFloat * scale + round;
		startingFloat = (src++)[0];
		copy = ((short*) buffer)[0];
		__stfiwx( converted, 0, buffer );
		converted = __fctiw( scaled );
		scaled = startingFloat * scale + round;
		startingFloat = (src++)[0];
		
		count -= 6;
		loopCount = count / 2;
		count &= 1;
		while( loopCount-- )
		{
			register float	startingFloat2;
			register double scaled2;
			register double converted2;
			register short	copy2;
			
			(dst++)[0] = copy;
			__asm__ __volatile__ ( "fctiw %0, %1" : "=f" (converted2) : "f" ( scaled ) );
			copy2 = ((short*) buffer)[2];
			__asm__ __volatile__ ( "fmadd %0, %1, %2, %3" : "=f" (scaled2) : "f" ( startingFloat), "f" (scale), "f" (round) );
			__asm__ __volatile__ ( "stfiwx %0, %1, %2" : : "f" (converted), "b%" (sizeof(float)), "r" (buffer) : "memory" );
			startingFloat2 = (src++)[0];

			(dst++)[0] = copy2;
			__asm__ __volatile__ ( "fctiw %0, %1" : "=f" (converted) : "f" ( scaled2 ) );
			copy = ((short*) buffer)[0];
			__asm__ __volatile__ ( "fmadd %0, %1, %2, %3" : "=f" (scaled) : "f" ( startingFloat2), "f" (scale), "f" (round) );
			__asm__ __volatile__ ( "stfiwx %0, %1, %2" : : "f" (converted2), "b%" (0), "r" (buffer) : "memory" );
			startingFloat = (src++)[0];
		}
		
		(dst++)[0] = copy;
		copy = ((short*) buffer)[2];
		__stfiwx( converted, sizeof(float), buffer );
		converted = __fctiw( scaled );
		scaled = startingFloat * scale + round;
		(dst++)[0] = copy;
		copy = ((short*) buffer)[0];
		__stfiwx( converted, 0, buffer );
		converted = __fctiw( scaled );
		(dst++)[0] = copy;
		copy = ((short*) buffer)[2];
		__stfiwx( converted, sizeof(float), buffer );
		(dst++)[0] = copy;
		copy = ((short*) buffer)[0];
		(dst++)[0] = copy;
		copy = ((short*) buffer)[2];
		(dst++)[0] = copy;
	}

	while( count-- )
	{
		double scaled = src[0] * scale + round;
		double converted = __fctiw( scaled );
		__stfiwx( converted, 0, buffer );
		dst[0] = buffer[0] >> 16;
		src++;
		dst++;
	}
}
