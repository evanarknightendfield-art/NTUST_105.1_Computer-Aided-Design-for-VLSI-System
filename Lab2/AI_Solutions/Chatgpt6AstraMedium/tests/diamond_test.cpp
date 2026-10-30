#include "diamond.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

bool slow(const Tree& t,int v,int p,long long a,long long b,int& nx,int& ny) {
    for(long long dx=-a;dx<=a;++dx) {
        long long dy=a-std::llabs(dx);
        for(int s=0;s<(dy?2:1);++s) {
            long long x=t.nd[v].x+dx,y=t.nd[v].y+(s?-dy:dy);
            if(manhattan(x,y,t.nd[p].x,t.nd[p].y)==b && t.usable(x,y)) {
                nx=x;ny=y;return true;
            }
        }
    }
    return false;
}
int main() {
    std::mt19937 rng(612);
    long long cases=0;
    for(int instance=0;instance<100;++instance) {
        Problem p;p.dimx=20;p.dimy=17;p.srcX=rng()%21;p.srcY=rng()%18;
        p.lib.push_back({"B",8,100,1});p.sinkX={1+(int)(rng()%18)};p.sinkY={1+(int)(rng()%15)};
        if(p.sinkX[0]==p.srcX && p.sinkY[0]==p.srcY) p.srcX=0;
        TypeTable tt;tt.build(p.lib);Tree t;t.init(p,tt);
        for(int i=0;i<40;++i) {int x=1+rng()%18,y=1+rng()%15;if(t.usable(x,y))t.addBuffer(x,y);}
        for(int a=0;a<50;++a) for(int b=0;b<50;++b) {
            int x1=-1,y1=-1,x2=-1,y2=-1;
            bool ok1=slow(t,1,0,a,b,x1,y1),ok2=diamondIntersection(t,1,0,a,b,x2,y2);
            if(ok1!=ok2 || (ok1 && (x1!=x2 || y1!=y2))) {
                std::fprintf(stderr,"mismatch instance=%d a=%d b=%d\n",instance,a,b);return 1;
            }
            ++cases;
        }
    }
    Problem p;p.dimx=1000000000;p.dimy=1000000000;p.srcX=10;p.srcY=10;
    p.sinkX={500000000};p.sinkY={500000000};p.lib.push_back({"B",2,2000000000,1});
    TypeTable tt;tt.build(p.lib);Tree t;t.init(p,tt);
    int x,y;auto start=std::chrono::steady_clock::now();
    bool ok=diamondIntersection(t,1,0,500000000,499999980,x,y);
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    if(!ok || manhattan(x,y,p.srcX,p.srcY)!=499999980 || manhattan(x,y,p.sinkX[0],p.sinkY[0])!=500000000) return 2;
    p.srcX=504000000;p.srcY=500000000;
    Tree tangent;tangent.init(p,tt);
    int ox,oy,fx,fy;
    auto tslow=std::chrono::steady_clock::now();
    bool oldOk=slow(tangent,1,0,2000000,2000000,ox,oy);
    double oldSec=std::chrono::duration<double>(std::chrono::steady_clock::now()-tslow).count();
    auto tfast=std::chrono::steady_clock::now();
    bool newOk=diamondIntersection(tangent,1,0,2000000,2000000,fx,fy);
    double newSec=std::chrono::duration<double>(std::chrono::steady_clock::now()-tfast).count();
    if(!oldOk || !newOk || ox!=fx || oy!=fy) return 3;
    std::printf("Tangent radius 2000000: scan %.9fs, analytic %.9fs\n",oldSec,newSec);
    std::printf("PASS %lld comparisons; billion-scale coordinates %.9fs (%d,%d)\n",cases,seconds,x,y);
}
