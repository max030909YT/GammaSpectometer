#include "core.hpp"
#include <cstdio>
#include <map>
using namespace gs;
struct P{const char*n; std::vector<std::pair<const char*,double>> c;};
int main(){
  const auto& el=elements();
  auto idx=[&](const char*s){for(size_t i=0;i<el.size();++i) if(std::string(el[i].sym)==s) return (int)i; return -1;};
  std::vector<P> ps={
    {"Латунь",{{"Cu",70},{"Zn",30}}},
    {"Бронза",{{"Cu",90},{"Sn",10}}},
    {"Припой",{{"Sn",61},{"Pb",39}}},
    {"SiO2",{{"Si",46.74},{"O",53.26}}},
    {"Вода",{{"H",11.19},{"O",88.81}}},
    {"CaCO3",{{"Ca",40.04},{"C",12.0},{"O",47.96}}},
    {"Дюраль",{{"Al",95},{"Cu",5}}},
  };
  for(int mode=0;mode<2;mode++){
   printf("=== кандидаты: %s ===\n", mode?"только истинные + 2 лишних":"все 12");
   for(auto&p:ps){
    Setup S; S.N0=1e8; S.w.assign(12,0); double sum=0; for(auto&c:p.c) sum+=c.second;
    for(auto&c:p.c) S.w[idx(c.first)]=c.second/sum;
    S.x=std::round(1.5/mu_mix(S.w,59.54)*100)/100.0; S.det=0;
    std::mt19937_64 rng(12345);
    std::vector<int> cand; for(int i=0;i<12;i++) cand.push_back(i);
    if(mode){cand.clear(); for(int i=0;i<12;i++) if(S.w[i]>0|| el[i].sym==std::string("Fe")||el[i].sym==std::string("Si")) cand.push_back(i);}
    // MC
    int K=30; std::vector<std::vector<double>> W;
    Inverse first;
    for(int r=0;r<K;r++){
      auto sp=simulate(S,rng);
      auto inv=invert(detectors()[S.det],sp.open,sp.samp,S.x,cand,S.w);
      if(r==0) first=inv;
      if(inv.ok) W.push_back(inv.w);
    }
    printf("%s x=%.2f г/см2 used=%d chi2/ndf=%.2f/%d\n",p.n,S.x,first.nused,first.chi2,first.ndf);
    for(int i=0;i<12;i++){
      double m=0,s=0; for(auto&w:W) m+=w[i]; m/=W.size(); for(auto&w:W) s+=(w[i]-m)*(w[i]-m); s=std::sqrt(s/W.size());
      if(S.w[i]>0||m>0.005) printf("   %-2s true %6.2f  found %6.2f ± %5.2f\n",el[i].sym,100*S.w[i],100*m,100*s);
    }
   }
  }
}
