// Matched forward+inverse real FFT diagnostic. No FFT implementation is imported.
extern "C" {
#include "fft_double.h"
#include <fftw3.h>
void lsx_safe_rdft(int, int, double*);
int sox_init(void);
int sox_quit(void);
}
#include "dsp/fft.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace kbsrc::base::dsp;
static double cpu(){ timespec t{};clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9; }
int main() try {
  sox_init();
  std::cout<<std::setprecision(17)<<"[";bool comma=false;
  for(int n:{4096,32768,80000,36750}) {
    std::vector<double> original(n), work(n+2);for(int i=0;i<n;++i)original[i]=.3*std::sin(.1*i)+.2*std::cos(.031*i);
    for(std::string name:{"faar","faar-complex","ooura-sox","fftw","kbsrc-generic"}) {
      if((name=="ooura-sox" || name=="faar") && (n&(n-1)))continue;
      std::function<void()> pair;
      faar_real_fft_plan faar{};
      faar_fft_plan complex_plan{};std::vector<double> complex_work(2*n);
      fftw_plan fw=nullptr,iv=nullptr;double* fr=nullptr;fftw_complex* fc=nullptr;
      RFFT<double,double,V4> kb;std::vector<cx<double>> X(n/2+1),z(n/2),y(n/2);std::vector<Scratch32> scratch;
      double t=cpu();
      if(name=="faar") {
        if(faar_real_fft_plan_init(&faar,n))throw std::runtime_error("FAAR FFT size rejected");
        pair=[&]{std::copy(original.begin(),original.end(),work.begin());faar_real_fft_execute(&faar,work.data(),0);faar_real_fft_execute(&faar,work.data(),1);};
      } else if(name=="faar-complex") {
        if(faar_fft_plan_init(&complex_plan,n))throw std::runtime_error("FAAR complex FFT size rejected");
        pair=[&]{for(int i=0;i<n;++i){complex_work[2*i]=original[i];complex_work[2*i+1]=0;}faar_fft_execute(&complex_plan,complex_work.data(),0);faar_fft_execute(&complex_plan,complex_work.data(),1);for(int i=0;i<n;++i)work[i]=complex_work[2*i];};
      } else if(name=="ooura-sox") {
        lsx_safe_rdft(n,1,work.data());
        pair=[&]{std::copy(original.begin(),original.end(),work.begin());lsx_safe_rdft(n,1,work.data());lsx_safe_rdft(n,-1,work.data());for(int i=0;i<n;++i)work[i]*=2./n;};
      } else if(name=="fftw") {
        fr=fftw_alloc_real(n);fc=fftw_alloc_complex(n/2+1);
        fw=fftw_plan_dft_r2c_1d(n,fr,fc,FFTW_ESTIMATE);iv=fftw_plan_dft_c2r_1d(n,fc,fr,FFTW_ESTIMATE);
        pair=[&]{std::copy(original.begin(),original.end(),fr);fftw_execute(fw);fftw_execute(iv);for(int i=0;i<n;++i)work[i]=fr[i]/n;};
      } else {
        kb.init(n);scratch.resize(kb.scratch_bytes()/32+2);
        pair=[&]{kb.forward(original.data(),X.data(),z.data(),y.data(),scratch.data());kb.inverse(X.data(),work.data(),z.data(),y.data(),scratch.data());for(int i=0;i<n;++i)work[i]/=n;};
      }
      const double setup=cpu()-t;pair();double error=0;for(int i=0;i<n;++i)error=std::max(error,std::abs(work[i]-original[i]));
      if(error>1e-10)throw std::runtime_error(name+" FFT roundtrip failed "+std::to_string(error));
      int repetitions=1;for(;;){t=cpu();for(int r=0;r<repetitions;++r)pair();if(cpu()-t>=.05)break;repetitions*=2;}
      std::vector<double> samples;for(int s=0;s<7;++s){t=cpu();for(int r=0;r<repetitions;++r)pair();samples.push_back((cpu()-t)/repetitions);}std::sort(samples.begin(),samples.end());
      if(comma)std::cout<<",";comma=true;
      std::cout<<"{\"engine\":\""<<name<<"\",\"size\":"<<n<<",\"setup_cpu_seconds\":"<<setup<<",\"roundtrip_max_error\":"<<error<<",\"median_pair_cpu_seconds\":"<<samples[3]<<",\"repetitions_per_sample\":"<<repetitions<<"}"<<std::flush;
      if(fw){fftw_destroy_plan(fw);fftw_destroy_plan(iv);fftw_free(fr);fftw_free(fc);}faar_real_fft_plan_clear(&faar);faar_fft_plan_clear(&complex_plan);
    }
  }
  sox_quit();std::cout<<"]\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
