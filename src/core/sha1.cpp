/*
Copyright (c) 2009, Micael Hildenborg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Micael Hildenborg nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY Micael Hildenborg ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Micael Hildenborg BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Contributors:
Gustav
Several members in the gamedev.se forum.
*/
 
#include "sha1.h"
#include <string.h>
#include <cstdio>
#include <tchar.h>

inline const unsigned int rol(const unsigned int num, const unsigned int cnt)
{
        return((num << cnt) | (num >> (32-cnt)));
}
 
void innerHash(unsigned int *result, unsigned int *w)
{
        unsigned int save[5];
        save[0]=result[0];
        save[1]=result[1];
        save[2]=result[2];
        save[3]=result[3];
        save[4]=result[4];

        #define a result[0]
        #define b result[1]
        #define c result[2]
        #define d result[3]
        #define e result[4]
 
        int j=0;
        #define sha1macro(func,val) \
                {const unsigned int t = rol(a, 5)+(func)+e+val+w[j]; \
                e = d; d = c; \
                c = rol(b, 30); \
                b = a; a = t;}
        while(j<16)
        {
                sha1macro((b&c)|(~b&d),0x5A827999)
                j++;
        }
        while(j<20)
        {
                w[j] = rol((w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16]), 1);
                sha1macro((b&c)|(~b&d),0x5A827999)
                j++;
        }
        while(j<40)
        {
                w[j] = rol((w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16]), 1);
                sha1macro(b^c^d,0x6ED9EBA1)
                j++;
        }
        while(j<60)
        {
                w[j] = rol((w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16]), 1);
                sha1macro((b&c)|(b&d)|(c&d),0x8F1BBCDC)
                j++;
        }
        while(j<80)
        {
                w[j] = rol((w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16]), 1);
                sha1macro(b^c^d,0xCA62C1D6)
                j++;
        }
        #undef sha1macro
        #undef a
        #undef b
        #undef c
        #undef d
        #undef e

        result[0]+=save[0];
        result[1]+=save[1];
        result[2]+=save[2];
        result[3]+=save[3];
        result[4]+=save[4];
}

void sha1(const void *src, const int bytelength, unsigned char *hash)
{
        // Init the result array, and create references to the five unsigned integers for better readabillity.
        unsigned int result[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};

        const unsigned char *sarray=(const unsigned char*)src;
        // The variables
        unsigned int w[80];
        int j,i,i1;
        j=0;
        // Loop through all complete 64byte blocks.
        for(i=0,i1=64; i<=(bytelength-64); i=i1,i1+=64) 
        {
                int k=0;
                for(j=i;j<i1;j+=4)
                {
                        // This line will swap endian on big endian and keep endian on little endian.
                        w[k++]=(unsigned int)sarray[j+3]|(((unsigned int)sarray[j+2])<<8)|(((unsigned int)sarray[j+1])<<16)|(((unsigned int)sarray[j])<<24);
                }
                innerHash(result,w);
        }
        // fill in reminder
        i1=bytelength-i;
        memset(w,0,sizeof(unsigned int)*16);
        for(j=0;j<i1;j++)
        {
                w[j>>2]|=(unsigned int)sarray[j+i]<<((3-(j&3))<<3);
        }
        w[j>>2]|=0x80<<((3-(j&3))<<3);
        if(i1>=56)
        {
                innerHash(result,w);
                memset(w,0,sizeof(unsigned int)*16);
        }
        w[15]=bytelength<<3;
        innerHash(result,w);
        // Store hash in result pointer, and make sure we get in in the correct order on both endian models.
        for(i=20;--i>=0;) 
        {
                hash[i]=(result[i>>2]>>(((3-i)&0x3)<<3))&0xFF;
        }
}

void sha1toHexString(const unsigned char* hash, char* hexstring)
{
    const char hexDigits[] = { "0123456789abcdef" };

    for (int hashByte = 20; --hashByte >= 0;)
    {
        hexstring[hashByte << 1] = hexDigits[(hash[hashByte] >> 4) & 0xf];
        hexstring[(hashByte << 1) + 1] = hexDigits[hash[hashByte] & 0xf];
    }
    hexstring[40] = 0;
}

