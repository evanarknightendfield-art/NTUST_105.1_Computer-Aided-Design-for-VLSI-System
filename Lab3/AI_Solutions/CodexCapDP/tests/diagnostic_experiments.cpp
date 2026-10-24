#include "diagnostic.hpp"
#include "chain.hpp"
#include "annealer.hpp"
#include "optimizer.hpp"
#include <set>
#include <map>
#include <numeric>
#include <iostream>
bool readPlX(const PlaceDB&,const std::string&,Placement&);
void require(bool ok,const std::string& e){if(!ok)throw std::runtime_error(e);}
WindowOptions options(const PlaceDB& db){WindowOptions w;w.passes=1000;w.threads=8;if(db.movable.size()<=1500&&(db.utilization()>=0.4||db.movable.size()<100)){w.perRow=5;w.perRow3=4;}return w;}
void plateau(const PlaceDB& db,const Placement& p,const std::string& out){
 std::ofstream f(out+"_plateau.csv");f<<std::setprecision(15)<<"fraction,cap,cells,segments,max_per_segment,movable,total_segments\n";
 auto b=displacement(db,p);for(double frac:{.005,.01,.02}){std::map<int,int> count;int n=0,m=0;for(int v:db.movable)if(displacementOf(db,p,v)>b.max*(1-frac)+1e-9){m=std::max(m,++count[p.seg[v]]);++n;}f<<frac<<','<<b.max*(1-frac)<<','<<n<<','<<count.size()<<','<<m<<','<<db.movable.size()<<','<<db.segs.size()<<'\n';}
}
void compareDP(const PlaceDB& db,const Placement& base,const Placement& raw,const WindowMemo& memo,double D,const std::string& out){
 // Audit memo invalidation against a non-propagating immediate-neighbour set.
 // Observation only: the production sync is still called unchanged below.
 if(memo.mod.size()==db.nodes.size()){
  std::vector<char> changed(db.nodes.size(),0),expected(db.nodes.size(),0);
  for(int v:db.movable)if(memo.seg[v]!=raw.seg[v]||memo.site[v]!=raw.site[v])changed[v]=expected[v]=1;
  for(int pass=0;pass<2;++pass){const auto& S=pass?raw.seg:memo.seg;const auto& X=pass?raw.site:memo.site;
   std::vector<std::vector<int>> lists(db.segs.size());for(int v:db.movable)lists[S[v]].push_back(v);
   for(auto& l:lists){std::sort(l.begin(),l.end(),[&](int a,int b){return X[a]<X[b];});for(int i=0;i<int(l.size());++i)if(changed[l[i]]){if(i)expected[l[i-1]]=1;if(i+1<int(l.size()))expected[l[i+1]]=1;}}
  }
  auto audit=memo;audit.sync(db,raw);long actual=0,extra=0;for(int v:db.movable)if(audit.mod[v]==audit.clock){++actual;extra+=!expected[v];}
  std::ofstream m(out+"_memo_audit.csv");m<<"changed,changed_and_immediate_neighbours,actual_marked,extra_marked,movable\n"<<std::count(changed.begin(),changed.end(),1)<<','<<std::count(expected.begin(),expected.end(),1)<<','<<actual<<','<<extra<<','<<db.movable.size()<<'\n';
 }
 std::ofstream f(out+"_dp.csv");f<<std::setprecision(15)<<"mode,seconds,total,max,legal,mask_cells,improved_windows,solved_windows\n";
 diag::cells(db,base,raw,D,out+"_raw_cells.csv");
 for(int radius:{-1,0,4}){
  diag::localEnabled=radius>=0;diag::localMask.assign(db.nodes.size(),0);
  std::vector<char> seed(db.nodes.size(),0);for(int v:db.movable)if(base.seg[v]!=raw.seg[v]||base.site[v]!=raw.site[v]||displacementOf(db,base,v)>D+1e-9)seed[v]=1;
  diag::localMask=seed;
  if(radius>0)for(const Placement* p:{&base,&raw}){std::vector<std::vector<int>> lists(db.segs.size());for(int v:db.movable)lists[p->seg[v]].push_back(v);
   for(auto& l:lists){std::sort(l.begin(),l.end(),[&](int a,int b){return p->site[a]<p->site[b];});for(int i=0;i<int(l.size());++i)if(seed[l[i]])for(int j=std::max(0,i-radius);j<std::min(int(l.size()),i+radius+1);++j)diag::localMask[l[j]]=1;}}
  auto p=raw;auto mm=memo;auto w=options(db);w.perRow=4;w.perRow3=3;w.timeLimit=10;
  diag::solvedWindows=0;double start=wallSeconds();long got=windowSweeps(db,p,w,&mm);double seconds=wallSeconds()-start;auto d=displacement(db,p);
  f<<(radius<0?"full":radius==0?"local0":"local4")<<','<<seconds<<','<<d.total<<','<<d.max<<','<<checkLegal(db,p).empty()<<','<<std::count(diag::localMask.begin(),diag::localMask.end(),1)<<','<<got<<','<<diag::solvedWindows<<'\n';f.flush();
  diag::cells(db,base,p,D,out+(radius<0?"_full_cells.csv":radius==0?"_local0_cells.csv":"_local4_cells.csv"));
 }
 diag::localEnabled=false;
}
void chain(const PlaceDB& db,const Placement& base,const WindowMemo& memo,const std::string& out){
 std::ofstream f(out+"_chain.csv");f<<std::setprecision(15)<<"budget,seconds,base_total,base_max,total,max,legal,targets,chains\n";
 for(double budget:{1.5,4.}){auto p=base;auto mm=memo;auto w=options(db);ChainOptions c;double start=wallSeconds(),until=start+budget;long targets=0,chains=0;
  for(int round=0;round<2&&wallSeconds()<until;++round){auto before=displacement(db,p);auto target=w;target.passes=2;target.timeLimit=(until-wallSeconds())*.3/(2-round);long got=targetedWindowsDisp(db,p,target);targets+=got;
   c.seconds=std::max(0.,(until-wallSeconds())*.75/(2-round));auto result=chainDisplacement(db,p,c);chains+=result.accepted;
   if(!result.accepted&&!got)break;
   auto after=w;after.timeLimit=std::max(0.,(until-wallSeconds())*.5/(2-round));windowSweeps(db,p,after,&mm);
   auto d=displacement(db,p);require(checkLegal(db,p).empty()&&d.total<=before.total+1e-5&&d.max<=before.max+1e-6,"chain invariant");
  }
  double seconds=wallSeconds()-start;auto b=displacement(db,base),d=displacement(db,p);f<<budget<<','<<seconds<<','<<b.total<<','<<b.max<<','<<d.total<<','<<d.max<<','<<checkLegal(db,p).empty()<<','<<targets<<','<<chains<<'\n';f.flush();
  writePl(db,p,out+"_chain"+std::to_string(budget)+".pl");
 }
}
void schedule(const PlaceDB& db,const Placement& base,const std::string& out){
 std::ofstream f(out+"_schedule.csv");f<<std::setprecision(15)<<"schedule,seed,start_hpwl,before_sa_hpwl,postsa_hpwl,final_hpwl,sa_attempts,sa_s,polish_s,total_s,legal\n";
 // One seed, same cached Stage 1, default 3 polish sweeps, nominal 90 s total.
 for(int mode=0;mode<3;++mode){auto p=base;double t0=wallSeconds(),pre=totalHPWL(db,p),afterSA=pre,polishS=0;
  auto polish=[&](double sec){auto start=wallSeconds();OptimizeOptions o;o.until=start+sec;o.verbose=true;polishHPWL(db,p,o,out+"_"+std::to_string(mode)+"_polish.csv");WindowOptions w;w.threads=8;w.timeLimit=std::max(0.,o.until-wallSeconds());optimizeWindowsHPWL(db,p,w);reorderWindows(db,p,2,true,o.until);polishS+=wallSeconds()-start;};
  if(mode==2){polish(30);pre=totalHPWL(db,p);}
  SAOptions o;o.seed=1;o.timeLimit=mode==1?30:60;o.verbose=true;o.tracePath=out+"_"+std::to_string(mode)+"_sa.csv";
  auto r=anneal(db,p,o);afterSA=totalHPWL(db,p);if(mode!=2)polish(mode==1?60:30);
  double elapsed=wallSeconds()-t0,final=totalHPWL(db,p);bool legal=checkLegal(db,p).empty();require(legal,"schedule legality");
  f<<(mode==0?"SA60_P30":mode==1?"SA30_P60":"P30_SA60")<<",1,"<<totalHPWL(db,base)<<','<<pre<<','<<afterSA<<','<<final<<','<<r.attempts<<','<<r.seconds<<','<<polishS<<','<<elapsed<<','<<legal<<'\n';f.flush();writePl(db,p,out+"_"+std::to_string(mode)+".pl");
 }
}
int main(int argc,char** argv){try{
 require(argc>=5,"mode BENCH PL OUT [memo or raw, memo, cap]");std::string mode=argv[1],out=argv[4];PlaceDB db;std::string err;require(db.read(argv[2],err),err);Placement p;require(readPlX(db,argv[3],p),"read placement");require(checkLegal(db,p).empty(),"input legality");
 if(mode=="plateau")plateau(db,p,out);
 else if(mode=="chain")chain(db,p,diag::loadMemo(argv[5]),out);
 else if(mode=="schedule")schedule(db,p,out);
 else if(mode=="dp"){Placement raw;require(readPlX(db,argv[5],raw),"read raw");std::string mp=argv[6];mp=mp.substr(0,mp.size()-5)+".members";diag::loadMembers(mp);if(!diag::memberships.empty())diagnosticMaterialize(db,p,std::stod(argv[7]),out+"_actual");compareDP(db,p,raw,diag::loadMemo(argv[6]),std::stod(argv[7]),out);}
 else if(mode=="repair"){
  double D=std::stod(argv[6]);RepairOptions o;o.seed=7;o.phase1Until=wallSeconds()+1;o.phase2Until=o.phase1Until+1;o.phase2PerCell=db.movable.size()<=3000?2000:0;
  diag::detail=true;diag::memberships.clear();auto q=p;double start=wallSeconds();bool ok=rowRepair(db,q,D,o);double seconds=wallSeconds()-start;
  std::ofstream f(out+"_repair.csv");auto b=displacement(db,p),d=displacement(db,q);f<<std::setprecision(15)<<"D,seed,ok,seconds,base_total,raw_total,base_max,raw_max\n"<<D<<",7,"<<ok<<','<<seconds<<','<<b.total<<','<<d.total<<','<<b.max<<','<<d.max<<'\n';
  if(ok){diagnosticMaterialize(db,p,D,out);compareDP(db,p,q,diag::loadMemo(argv[5]),D,out);writePl(db,q,out+"_raw.pl");}
 }else throw std::runtime_error("unknown mode");
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
