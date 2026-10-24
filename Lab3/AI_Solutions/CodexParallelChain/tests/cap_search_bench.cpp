#include "diagnostic.hpp"
#include <cstdio>
bool readPlX(const PlaceDB&,const std::string&,Placement&);
int main(int argc,char** argv){
 if(argc<6)return 1; PlaceDB db;Placement p;std::string err;
 if(!db.read(argv[1],err)||!readPlX(db,argv[2],p))return 2;
 auto mm=diag::loadMemo(std::string(argv[2]).substr(0,std::string(argv[2]).size()-3)+".memo");
 CapSearchOptions o;o.seconds=std::stod(argv[4]);o.totalRatio=std::stod(argv[5]);o.threads=8;o.dp.passes=1000;o.verbose=true;
 if(db.movable.size()<=1500&&(db.utilization()>=.4||db.movable.size()<100)){o.dp.perRow=5;o.dp.perRow3=4;}
 auto r=capSearch(db,p,o,&mm);auto d=displacement(db,p);
 std::printf("seconds,total,max,legal,attempts,feasible,accepted\n%.6f,%.12f,%.12f,%d,%d,%d,%d\n",r.seconds,d.total,d.max,checkLegal(db,p).empty(),r.attempts,r.feasible,r.accepted);
 writePl(db,p,std::string(argv[3])+".pl");
}
