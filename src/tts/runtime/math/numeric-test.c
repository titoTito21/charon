/* Differential test of the exact freestanding object against host libm.
 * This test is hosted; the math object is not. No GUI or audio operations. */
#include <math.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
extern double hak_test_sin(double),hak_test_cos(double),hak_test_exp(double);
extern double hak_test_fabs(double),hak_test_floor(double),hak_test_sqrt(double);
extern double hak_test_pow(double,double),hak_test_ldexp(double,int),hak_test_scalbn(double,int);
extern double hak_test_log(double),hak_test_fmod(double,double);
extern double hak_test_tanh(double),hak_test_expm1(double),hak_test_copysign(double,double);
extern double hak_test_fmax(double,double),hak_test_fmin(double,double),hak_test_hypot(double,double);
extern double hak_test_nearbyint(double);
extern long hak_test_labs(long);
static unsigned checks;
static uint64_t greatest_ulp;
static uint64_t bits(double value) {uint64_t result;memcpy(&result,&value,8);return result;}
static uint64_t ordered(double value) {
    uint64_t result=bits(value);
    return result>>63 ? ~result : result|UINT64_C(0x8000000000000000);
}
/* Independent exact integer rounding oracle for binary64, without libm's
 * mode-dependent nearbyint/rint. Above 2^52 finite doubles are already integers.
 * Conversion to int64 truncates toward zero regardless of the FP rounding mode.
 * The fractional subtraction here is exact (Sterbenz, or subtraction of zero).
 * No result depends on arithmetic under the mode being tested. */
static double nearby_expected(double x,int mode) {
    uint64_t magnitude=bits(x)&UINT64_C(0x7fffffffffffffff);
    if(magnitude>=UINT64_C(0x4330000000000000))return x; /* includes inf/NaN */
    int64_t integer=(int64_t)x;
    double fraction=x-(double)integer;
    if(mode==FE_DOWNWARD && fraction<0) --integer;
    else if(mode==FE_UPWARD && fraction>0) ++integer;
    else if(mode==FE_TONEAREST) {
        if(fraction>0.5 || (fraction==0.5 && integer%2!=0)) ++integer;
        else if(fraction< -0.5 || (fraction== -0.5 && integer%2!=0)) --integer;
    }
    if(integer==0) {
        uint64_t zero=bits(x)&UINT64_C(0x8000000000000000);
        double result;memcpy(&result,&zero,8);return result;
    }
    return (double)integer; /* exact: |integer| <= 2^52 */
}
static void compare(const char* name,double x,double y,double actual,double expected,uint64_t allowance) {
    ++checks;
    if(isnan(expected) && isnan(actual))return;
    if(isnan(expected) || isnan(actual))goto fail;
    if(expected==0 && actual==0) {if(!!signbit(expected)==!!signbit(actual))return;goto fail;}
    if(isinf(expected) || isinf(actual)) {if(expected==actual)return;goto fail;}
    uint64_t a=ordered(actual),b=ordered(expected),distance=a>b ? a-b : b-a;
    if(distance>greatest_ulp)greatest_ulp=distance;
    if(distance<=allowance)return;
fail:
    fprintf(stderr,"FAIL %s(%a,%a): actual=%a reference=%a allowance=%llu ULP\n",
        name,x,y,actual,expected,(unsigned long long)allowance);exit(1);
}
static void unary(double x) {
    compare("sin",x,0,hak_test_sin(x),sin(x),2);
    compare("cos",x,0,hak_test_cos(x),cos(x),2);
    compare("exp",x,0,hak_test_exp(x),exp(x),4);
    compare("sqrt",x,0,hak_test_sqrt(x),sqrt(x),1);
    compare("floor",x,0,hak_test_floor(x),floor(x),0);
    compare("fabs",x,0,hak_test_fabs(x),fabs(x),0);
    compare("log",x,0,hak_test_log(x),log(x),4);
    compare("fmod",x,2,hak_test_fmod(x,2),fmod(x,2),0);
    compare("tanh",x,0,hak_test_tanh(x),tanh(x),4);
    compare("expm1",x,0,hak_test_expm1(x),expm1(x),4);
}
int main(void) {
    _Static_assert(sizeof(double)==8 && DBL_MANT_DIG==53,"Requires binary64");
    int saved=fegetround();if(fesetround(FE_TONEAREST))return 2;
    const double cases[]={0.0,-0.0,INFINITY,-INFINITY,NAN,
        DBL_MAX,-DBL_MAX,DBL_MIN,-DBL_MIN,0x1p-1074,-0x1p-1074,
        1,-1,2,-2,0.5,-0.5,0x1.fffffffffffffp-1,0x1.0000000000001p0,
        0x1.921fb54442d18p0,-0x1.921fb54442d18p0,
        0x1.921fb54442d18p1,-0x1.921fb54442d18p1,
        709,709.782712893384,710,-744,-745,-746,
        1e-300,-1e-300,1e20,-1e20,1e100,-1e100,1e300,-1e300};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)unary(cases[i]);
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
        for(unsigned j=0;j<sizeof(cases)/sizeof(cases[0]);j++) {
            double x=cases[i],y=cases[j];
            /* Some hosted fmin/max choose their second equal-zero argument.
             * Check opposite signed zeros against musl's explicit Annex F
             * policy instead; all other cases use independent host libm. */
            double high=(x==0 && y==0 && !!signbit(x)!=!!signbit(y))?0.0:fmax(x,y);
            double low=(x==0 && y==0 && !!signbit(x)!=!!signbit(y))?-0.0:fmin(x,y);
            compare("fmax",x,y,hak_test_fmax(x,y),high,0);
            compare("fmin",x,y,hak_test_fmin(x,y),low,0);
            compare("hypot",x,y,hak_test_hypot(x,y),hypot(x,y),2);
        }
    const long integers[]={0,1,-1,127,-127,65536,-65536,LONG_MAX,-LONG_MAX};
    for(unsigned i=0;i<sizeof(integers)/sizeof(integers[0]);i++) {
        ++checks;if(hak_test_labs(integers[i])!=labs(integers[i]))return 1;
    } /* LONG_MIN intentionally excluded: its absolute value is not representable. */
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const double oracleInputs[]={-0.1,0.1,-0.5,0.5,-1.5,1.5,-2.5,2.5};
    const double oracleAnswers[4][8]={
        {-0.0,0.0,-0.0,0.0,-2,2,-2,2},
        {-1,0.0,-1,0.0,-2,1,-3,2},
        {-0.0,1,-0.0,1,-1,2,-2,3},
        {-0.0,0.0,-0.0,0.0,-1,1,-2,2}};
    for(unsigned m=0;m<4;m++) for(unsigned i=0;i<8;i++)
        compare("integer oracle table",oracleInputs[i],modes[m],
            nearby_expected(oracleInputs[i],modes[m]),oracleAnswers[m][i],0);
    const double roundCases[]={0.0,-0.0,0.1,-0.1,0.5,-0.5,1.5,-1.5,2.5,-2.5,
        0x1.fffffffffffffp-2,-0x1.fffffffffffffp-2,
        0x1.0000000000001p-1,-0x1.0000000000001p-1,
        0x1p-1074,-0x1p-1074,0x1.fffffffffffffp51,0x1p52,DBL_MAX,INFINITY,-INFINITY,NAN};
    for(unsigned m=0;m<4;m++) {
        if(fesetround(modes[m]))return 2;
        for(unsigned i=0;i<sizeof(roundCases)/sizeof(roundCases[0]);i++) {
            volatile double x=roundCases[i];
            feclearexcept(FE_ALL_EXCEPT);
            double actual=hak_test_nearbyint(x);
            if(fetestexcept(FE_INEXACT)) {fprintf(stderr,"FAIL nearbyint raised inexact\n");return 1;}
            /* The hosted Haiku nearbyint also misrounds negative fractions in
             * FE_DOWNWARD. Use exact integer/ties-even logic for all modes,
             * rather than treating that implementation as an oracle. */
            double expected=nearby_expected(x,modes[m]);
            compare("nearbyint",x,modes[m],actual,expected,0);
            /* Preserve exception flags already raised by another operation. */
            feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_INEXACT|FE_DIVBYZERO);
            actual=hak_test_nearbyint(x);
            if((fetestexcept(FE_INEXACT|FE_DIVBYZERO)&(FE_INEXACT|FE_DIVBYZERO))!=(FE_INEXACT|FE_DIVBYZERO)) {
                fprintf(stderr,"FAIL nearbyint lost existing exception flags\n");return 1;
            }
            compare("nearbyint flags",x,modes[m],actual,expected,0);
        }
    }
    feclearexcept(FE_ALL_EXCEPT);if(fesetround(FE_TONEAREST))return 2;
    /* copysign must preserve magnitude bits, including NaN payloads, and copy
     * the sign even from negative zero or NaN. No arithmetic approximation. */
    const uint64_t raw[]={0,UINT64_C(0x8000000000000000),1,UINT64_C(0x8000000000000001),
        UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000001234),UINT64_C(0xfff8000000005678),
        UINT64_C(0x3ff0000000000000),UINT64_C(0xbff0000000000000)};
    for(unsigned i=0;i<sizeof(raw)/sizeof(raw[0]);i++)
        for(unsigned j=0;j<sizeof(raw)/sizeof(raw[0]);j++) {
            double x,y;memcpy(&x,raw+i,8);memcpy(&y,raw+j,8);
            uint64_t got=bits(hak_test_copysign(x,y));
            uint64_t expected=(raw[i]&UINT64_C(0x7fffffffffffffff))|(raw[j]&UINT64_C(0x8000000000000000));
            ++checks;
            if(got!=expected || got!=bits(copysign(x,y))) {
                fprintf(stderr,"FAIL copysign bit preservation %u,%u\n",i,j);return 1;
            }
        }
    /* TVox soft limiter's normalized excess, from the knee into saturation. */
    for(unsigned i=0;i<=20000;i++) {
        double excess=(double)i/1000;
        compare("tanh limiter",excess,0,hak_test_tanh(excess),tanh(excess),4);
        compare("tanh negative",-excess,0,hak_test_tanh(-excess),tanh(-excess),4);
    }
    const double bases[]={0,-0.0,1,-1,2,-2,0.5,-0.5,DBL_MIN,DBL_MAX,INFINITY,-INFINITY,NAN};
    const double powers[]={0,-0.0,1,-1,2,-2,3,-3,0.5,-0.5,1.25,1024,-1074,INFINITY,-INFINITY,NAN};
    for(unsigned i=0;i<sizeof(bases)/sizeof(bases[0]);++i)
        for(unsigned j=0;j<sizeof(powers)/sizeof(powers[0]);++j)
            compare("pow",bases[i],powers[j],hak_test_pow(bases[i],powers[j]),pow(bases[i],powers[j]),4);
    const int scales[]={-2147483647,-1075,-1074,-1022,-53,-1,0,1,53,1023,1024,2147483647};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        for(unsigned j=0;j<sizeof(scales)/sizeof(scales[0]);++j) {
            compare("ldexp",cases[i],scales[j],hak_test_ldexp(cases[i],scales[j]),ldexp(cases[i],scales[j]),0);
            compare("scalbn",cases[i],scales[j],hak_test_scalbn(cases[i],scales[j]),scalbn(cases[i],scales[j]),0);
        }
    uint64_t seed=UINT64_C(0x5a17d912c984f32b);
    for(unsigned i=0;i<6000;++i) {
        seed=seed*UINT64_C(6364136223846793005)+1;
        double unit=(double)(seed>>11)*0x1p-53;
        /* TVox phase/recurrent oscillator, decay/envelope and rate ranges. */
        double phase=(unit-0.5)*1e8;
        compare("sin phase",phase,0,hak_test_sin(phase),sin(phase),2);
        compare("cos phase",phase,0,hak_test_cos(phase),cos(phase),2);
        double decay=(unit-1)*750;
        compare("exp decay",decay,0,hak_test_exp(decay),exp(decay),4);
        double base=0.001+unit*1000,power=(unit-0.5)*24;
        compare("pow envelope",base,power,hak_test_pow(base,power),pow(base,power),4);
        compare("sqrt amplitude",base,0,hak_test_sqrt(base),sqrt(base),1);
        compare("hypot filter",base,power,hak_test_hypot(base,power),hypot(base,power),2);
        /* Also exercise arbitrary binary64 exponents and reduction paths. */
        double arbitrary;memcpy(&arbitrary,&seed,sizeof(arbitrary));unary(arbitrary);
    }
    if(fesetround(saved))return 2;
    printf("PASS %u real math differential checks; maximum finite discrepancy %llu ULP; signed zero, NaN, infinity, subnormal, overflow and DSP ranges\n",
        checks,(unsigned long long)greatest_ulp);
    return 0;
}
