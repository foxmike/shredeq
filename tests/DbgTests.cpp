#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "Source/DSP/ChugStabilizer.h"
#include <cmath>
namespace { using namespace ShredEQ::DSP; constexpr double sr=48000.0,Pi=3.141592653589793; }
class Dbg final : public juce::UnitTest { public: Dbg():juce::UnitTest("Dbg","Dbg"){} void runTest() override {
 auto run=[&](const char* name,bool mt,double th,bool pp,double atk,double hld,double rel,double a1,double a8,double a3,int on,int off,int tot){
  ChugStabilizer cs; cs.prepare(sr,256); cs.setEnabled(true); cs.setTargetFrequency(120); cs.setBandwidthOctaves(1.2); cs.setMaximumReductionDb(8);
  cs.setAutomaticThreshold(!mt); if(mt) cs.setThresholdDb(th); cs.setAttackMs(atk); cs.setHoldMs(hld); cs.setReleaseMs(rel); cs.setPreservePick(pp);
  juce::AudioBuffer<float> t(2,1); float mg=0;
  for(int n=0;n<tot;++n){ double v=0; if(n>=on&&n<off) v=a1*std::sin(2*Pi*100*n/sr)+a8*std::sin(2*Pi*800*n/sr)+a3*std::sin(2*Pi*3000*n/sr);
   t.setSample(0,0,(float)v); t.setSample(1,0,(float)v); cs.process(t); float gr=cs.getLastGainReductionDb(); mg=std::max(mg,gr);
   if(n==on||n==on+64||n==on+200||n==off-1||n==off+2000||n==tot-1) juce::Logger::writeToLog(juce::String(name)+" n="+juce::String(n)+" gr="+juce::String(gr,3)+" lowDb="+juce::String(cs.getLowBandLevelDb(),1)); }
  juce::Logger::writeToLog(juce::String(name)+" MAXGR="+juce::String(mg,3)); };
 // test4 timing: manual -6, hold 0
 run("t4_fast",true,-6,true,0.5,0,80,0.9,0,0,0,2880,12480);
 run("t4_slow",true,-6,true,20.0,0,80,0.9,0,0,0,2880,12480);
 // test2 steady: broadband chord proxy
 auto runChord=[&](const char* name,bool mt,double th){
  ChugStabilizer cs; cs.prepare(sr,256); cs.setEnabled(true); cs.setTargetFrequency(120); cs.setBandwidthOctaves(1.2); cs.setMaximumReductionDb(8);
  cs.setAutomaticThreshold(!mt); if(mt) cs.setThresholdDb(th); cs.setAttackMs(3); cs.setHoldMs(25); cs.setReleaseMs(100);
  juce::AudioBuffer<float> t(2,1); float mg=0;
  double freqs[]={200,300,400,600,800,1200,1600,2400}; double amps[]={0.15,0.15,0.2,0.2,0.2,0.15,0.15,0.1};
  for(int n=0;n<30000;++n){ double v=0; for(int k=0;k<8;++k) v+=amps[k]*std::sin(2*Pi*freqs[k]*n/sr);
   t.setSample(0,0,(float)v); t.setSample(1,0,(float)v); cs.process(t); float gr=cs.getLastGainReductionDb(); mg=std::max(mg,gr);
   if(n==0||n==500||n==5000||n==29999) juce::Logger::writeToLog(juce::String(name)+" n="+juce::String(n)+" gr="+juce::String(gr,3)+" lowDb="+juce::String(cs.getLowBandLevelDb(),1)); }
  juce::Logger::writeToLog(juce::String(name)+" MAXGR="+juce::String(mg,3)); };
 runChord("chord_auto",false,0);
 runChord("chord_man-6",true,-6);
}}; static Dbg dbg;
