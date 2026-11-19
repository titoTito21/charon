/* Exact translation of Titan Voice g2p_pl.py number_to_words and helpers.
 * MIT-0. No prerecorded utterances or alternate voice engine. */
#include "tvox_numeric.h"
#include "tvox_data.inc"

typedef struct {char data[2048];size_t length;} Words;
static void word(Words* result,const char* text) {
    if(result->length)result->data[result->length++]=' ';
    while(*text)result->data[result->length++]=*text++;
}
static void below1000(Words* result,unsigned n) {
    if(n>=100){word(result,tvox_hund[n/100]);n%=100;}
    if(n>=20){word(result,tvox_tens[n/10]);n%=10;if(n)word(result,tvox_ones[n]);}
    else if(n>=10)word(result,tvox_teens[n-10]);
    else if(n)word(result,tvox_ones[n]);
}
int tvox_pl_number(uint64_t number,char* output,size_t capacity,size_t* required) {
    if(!required || (!output && capacity))return -1;
    Words result;result.length=0;unsigned groups[7],count=0;
    if(!number)word(&result,tvox_ones[0]);
    while(number){groups[count++]=(unsigned)(number%1000);number/=1000;}
    while(count){unsigned gi=--count,g=groups[gi];if(!g)continue;
        if(!gi)below1000(&result,g);
        else {const char* const* forms=tvox_big[gi>3?2:gi-1];
            if(g==1)word(&result,forms[0]);
            else {below1000(&result,g);
                unsigned form=(g%10>=2 && g%10<=4 && !(g%100>=12 && g%100<=14))?1:2;
                word(&result,forms[form]);}
        }
    }
    result.data[result.length]=0;*required=result.length+1;
    if(capacity<*required)return 1;
    for(size_t i=0;i<*required;++i)output[i]=result.data[i];
    return 0;
}
