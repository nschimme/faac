// Local configurable binding for a bounded, matched-quality reference sweep.
#include "kbsrc/kbsrc.hpp"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char **argv) try {
 if(argc!=9)throw std::runtime_error("usage: kbsrc-sweep generic|auto length beta input.f32 output.f32 in_rate out_rate channels");
 setenv("KBSRC_CPU",argv[1],1);int ir=std::stoi(argv[6]),orr=std::stoi(argv[7]),ch=std::stoi(argv[8]);
 std::ifstream f(argv[4],std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input open");size_t bytes=f.tellg();f.seekg(0);std::vector<float> x(bytes/4);f.read((char*)x.data(),bytes);if(!f||bytes%(4*ch))throw std::runtime_error("input read");
 std::vector<double> source(x.begin(),x.end());kbsrc::Options opt;opt.threads=1;opt.lpc_edges=false;opt.precision=kbsrc::Precision::Fast;opt.length=std::stod(argv[2]);opt.beta=std::stod(argv[3]);
 kbsrc::Stats stats;auto y=kbsrc::resample(source.data(),x.size()/ch,ch,ir,orr,opt,&stats);
 std::vector<float> out(y[0].size()*ch);for(size_t i=0;i<y[0].size();i++)for(int c=0;c<ch;c++)out[i*ch+c]=(float)y[c][i];
 std::ofstream o(argv[5],std::ios::binary);o.write((char*)out.data(),out.size()*4);if(!o)throw std::runtime_error("output write");
 std::cout<<std::setprecision(17)<<"{\"setup_cpu_seconds\":"<<stats.design_seconds<<",\"processing_cpu_seconds\":"<<stats.process_seconds<<",\"block_factor\":"<<stats.block<<",\"kernel_taps\":"<<stats.taps<<",\"selected_isa\":\""<<stats.isa<<"\",\"passband_hz\":"<<stats.passband<<",\"stopband_hz\":"<<stats.stopband<<",\"output_frames\":"<<y[0].size()<<"}\n";
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
