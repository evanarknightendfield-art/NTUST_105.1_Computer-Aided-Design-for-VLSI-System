#include "exact_placement.h"
#include "construct.h"
#include <random>
#include <cstdio>
#include <set>
int main(){
 std::mt19937 rng(48521);
 int tests=0;
 for(int run=0;run<100;++run){
  Problem P;P.dimx=15+rng()%50;P.dimy=15+rng()%50;P.srcX=1;P.srcY=1;P.srcF=1+rng()%5;P.srcL=200;P.wSkew=1+rng()%30;
  P.lib={{"b0",2,10,1},{"b1",3,25,3},{"b2",4,45,7},{"b3",8,1000,19}};
  std::set<std::pair<int,int>> used{{1,1}};
  int nn=8+rng()%35;
  while((int)P.sinkX.size()<nn){int x=1+rng()%(P.dimx-1),y=1+rng()%(P.dimy-1);if(!used.insert({x,y}).second)continue;P.sinkX.push_back(x);P.sinkY.push_back(y);}
  TypeTable TT;TT.build(P.lib);Tree T;T.init(P,TT);if(!buildInitial(T,false)){fprintf(stderr,"build failure\n");return 1;}
  // Preserve fixed topology and compare every integer coordinate with the
  // production transaction / legality / Score code, including occupied cells.
  for(int b=T.N+1;b<(int)T.nd.size();++b)if(T.nd[b].alive){
   for(bool axis:{false,true}){
    long long brute=T.score();int bx,by;bool found=exactCoordinateSpot(T,b,axis,bx,by);
    int ox=T.nd[b].x,oy=T.nd[b].y,p=T.nd[b].parent;
    for(int u=1;u<(axis?P.dimx:P.dimy);++u){
     int x=axis?u:ox,y=axis?oy:u;if(x==ox&&y==oy)continue;if(!T.usable(x,y))continue;
     T.begin();T.moveTo(b,x,y);T.refreshUp(b);T.refreshUp(p);if(T.touchedLegal())brute=std::min(brute,T.score());T.rollback();
    }
    long long exact=T.score();if(found){T.begin();T.moveTo(b,bx,by);T.refreshUp(b);T.refreshUp(p);if(!T.touchedLegal()){fprintf(stderr,"illegal result\n");return 2;}exact=T.score();T.rollback();}
    if(exact!=brute){fprintf(stderr,"mismatch run=%d b=%d axis=%d exact=%lld brute=%lld at=%d,%d orig=%d,%d\n",run,b,axis,exact,brute,bx,by,ox,oy);return 3;}
    ++tests;
   }
  }
 }
 printf("PASS %d exact integer line searches vs exhaustive legal-coordinate evaluation\n",tests);
}
