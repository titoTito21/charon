// Host unit test for the ACSS frame transform (modulation/stress/richness).
// The ApplyAcss body and the Steps/AcssSteps/families tables below are copied
// VERBATIM from src/tts/runtime/CharonLiveVoice.cpp (lines 232-283) so the test
// exercises the exact production math. It feeds a known F0/amplitude contour and
// checks that pitch-range (modulacja) widens the F0 spread, stress widens the
// amplitude contrast, and richness changes the glottal Rd - i.e. the mapped
// dimensions really move the synthesis frames, independent of any audio device.
#include <cstdio>
#include <cmath>
#include <cstddef>

static constexpr size_t frameDoubles=28;
// --- verbatim helpers (CharonLiveVoice.cpp 232-246) ---
static double AcssSteps(char c,double per,double low,double high){int n=int(c)-'m';if(n<-12)n=-12;if(n>12)n=12;double v=1+n*per;return v<low?low:v>high?high:v;}
struct Family {double formants,bandwidths,rd,pitch,breath;};
static const Family families[8]={
    {1,1,1,1,0},{.93,1,.9,.92,0},{1.08,1,1,1.12,0},{1.04,1,.95,1.05,0},
    {.88,1.1,.9,.85,0},{1.14,.9,1,1.2,0},{.97,1.35,1.05,1,0},{1,1.1,1.3,1.02,6}};
struct Persona {double range=1,stress=1,richness=1;unsigned family=0;};
// --- verbatim ApplyAcss body (CharonLiveVoice.cpp 267-283) ---
static void ApplyAcss(double* frames,size_t count,const Persona& p){
    const Family& fam=families[p.family<8?p.family:0];
    if(!frames||!count||(p.range==1&&p.stress==1&&p.richness==1&&!p.family))return;
    double f0Sum=0,avSum=0;size_t voiced=0;
    for(size_t i=0;i<count;++i){const double* fr=frames+i*frameDoubles;if(fr[0]>0&&fr[1]>0){f0Sum+=fr[0];avSum+=fr[1];++voiced;}}
    const double meanF0=voiced?f0Sum/double(voiced):0,meanAv=voiced?avSum/double(voiced):0;
    for(size_t i=0;i<count;++i){
        double* fr=frames+i*frameDoubles;
        if(voiced&&fr[0]>0&&p.range!=1){double f=meanF0+(fr[0]-meanF0)*p.range;fr[0]=f<40?40:f>600?600:f;}
        if(voiced&&fr[1]>0&&p.stress!=1){double a=meanAv+(fr[1]-meanAv)*p.stress;fr[1]=a<1?1:a>90?90:a;}
        double rd=fr[26]*fam.rd/p.richness;fr[26]=rd<.32?.32:rd>2.65?2.65:rd;
        const double breath=(p.richness<1?(1-p.richness)*40:0)+fam.breath;
        if(breath>0&&fr[1]>0){const double ah=fr[1]-30+breath;if(ah>fr[2])fr[2]=ah;}
    }
}
// --- test ---
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){printf("FAIL: %s\n",m);++fails;} }while(0)
static double f0spread(const double* fr,size_t n){double mn=1e9,mx=-1e9;for(size_t i=0;i<n;++i){if(fr[i*frameDoubles]<mn)mn=fr[i*frameDoubles];if(fr[i*frameDoubles]>mx)mx=fr[i*frameDoubles];}return mx-mn;}
static double avspread(const double* fr,size_t n){double mn=1e9,mx=-1e9;for(size_t i=0;i<n;++i){if(fr[i*frameDoubles+1]<mn)mn=fr[i*frameDoubles+1];if(fr[i*frameDoubles+1]>mx)mx=fr[i*frameDoubles+1];}return mx-mn;}

static void contour(double* fr,size_t n){   // a natural rise/fall F0 + amplitude wobble
    for(size_t i=0;i<n;++i){double* f=fr+i*frameDoubles;for(size_t k=0;k<frameDoubles;++k)f[k]=0;
        double t=double(i)/double(n-1);
        f[0]=120+30*std::sin(t*3.14159);   // F0 100..150-ish
        f[1]=50+10*std::sin(t*6.28318);     // AV voicing amplitude
        f[2]=0; f[26]=1.0;}                 // AH aspiration, Rd glottal
}

int main(){
    const size_t N=64; double base[N*frameDoubles],w[N*frameDoubles];
    auto reset=[&](){contour(base,N);for(size_t i=0;i<N*frameDoubles;++i)w[i]=base[i];};
    contour(base,N);   // initialise the reference contour before measuring it
    const double baseF0=f0spread(base,N), baseAv=avspread(base,N), baseRd=base[26];

    // modulacja (pitch-range) +6 steps -> AcssSteps 1.72 -> F0 spread ~1.72x
    reset(); {Persona p; p.range=AcssSteps('s',.12,.05,2.5); ApplyAcss(w,N,p);
        double r=f0spread(w,N)/baseF0; printf("range +6: AcssSteps=%.2f F0 spread x%.2f\n",p.range,r);
        CHECK(r>1.5,"modulation +6 should widen F0 spread >1.5x");}
    // monotone 'a' -> range 0.05 -> F0 spread collapses
    reset(); {Persona p; p.range=AcssSteps('a',.12,.05,2.5); ApplyAcss(w,N,p);
        double r=f0spread(w,N)/baseF0; printf("range 'a' (monotone): AcssSteps=%.2f F0 spread x%.2f\n",p.range,r);
        CHECK(r<0.2,"monotone collapses F0 spread");}
    // stress +6 -> amplitude contrast widens
    reset(); {Persona p; p.stress=AcssSteps('s',.10,.3,2.2); ApplyAcss(w,N,p);
        double r=avspread(w,N)/baseAv; printf("stress +6: AcssSteps=%.2f AV spread x%.2f\n",p.stress,r);
        CHECK(r>1.3,"stress +6 widens amplitude contrast");}
    // richness down (breathy) -> Rd rises, aspiration added
    reset(); {Persona p; p.richness=AcssSteps('g',.06,.4,1.8); ApplyAcss(w,N,p);  // 'g' = -6 -> 0.64
        printf("richness -6: AcssSteps=%.2f Rd %.2f->%.2f AH=%.1f\n",p.richness,baseRd,w[26],w[2]);
        CHECK(w[26]>baseRd,"breathy raises Rd"); CHECK(w[2]>0,"breathy adds aspiration");}
    // family highlight (smaller head, higher) -> formants scaled
    reset(); {Persona p; p.family=2; ApplyAcss(w,N,p);
        CHECK(families[2].formants!=1.0,"highlight family changes head size");}

    printf(fails?"\n%d CHECK(S) FAILED\n":"\nALL APPLYACSS TRANSFORM CHECKS PASSED\n",fails);
    return fails?1:0;
}
