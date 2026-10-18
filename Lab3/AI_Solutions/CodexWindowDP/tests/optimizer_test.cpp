#include "../src/optimizer.hpp"
#include "../src/window.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
int main() {
    std::mt19937 random(20261018);
    for(int trial=0;trial<250;++trial) {
        int n=1+random()%7;
        std::vector<double> costs(n*n);
        for(double& c:costs) c=(int(random()%800)-200)/10.0;
        std::vector<int> perm(n);std::iota(perm.begin(),perm.end(),0);
        double best=std::numeric_limits<double>::infinity();
        do {double c=0;for(int i=0;i<n;++i)c+=costs[i*n+perm[i]];best=std::min(best,c);} while(std::next_permutation(perm.begin(),perm.end()));
        auto answer=minAssignment(costs,n);
        require(answer.feasible && std::abs(answer.cost-best)<1e-7,"Hungarian vs exhaustive mismatch");
        for(double& c:costs) {c=std::abs(c);if(random()%5==0)c=std::numeric_limits<double>::infinity();}
        best=std::numeric_limits<double>::infinity();
        do {double c=0;for(int i=0;i<n;++i)c+=costs[i*n+perm[i]];best=std::min(best,c);} while(std::next_permutation(perm.begin(),perm.end()));
        answer=minAssignment(costs,n);
        require(answer.feasible==std::isfinite(best),"forbidden-edge feasibility mismatch");
        if(answer.feasible)require(std::abs(answer.cost-best)<1e-7,"forbidden-edge optimum mismatch");
        for(double& c:costs) c=(random()%1000)/10.0;
        double budget=0,expectedMax=std::numeric_limits<double>::infinity(),expectedSum=expectedMax;
        for(int i=0;i<n;++i)budget+=costs[i*n+i];
        do {
            double sum=0,mx=0;for(int i=0;i<n;++i){double c=costs[i*n+perm[i]];sum+=c;mx=std::max(mx,c);}
            if(sum<=budget+1e-7 && (mx<expectedMax || (mx==expectedMax && sum<expectedSum))) {expectedMax=mx;expectedSum=sum;}
        } while(std::next_permutation(perm.begin(),perm.end()));
        answer=paretoAssignment(costs,n);double mx=0;
        for(int i=0;i<n;++i) mx=std::max(mx,costs[i*n+answer.column[i]]);
        require(std::abs(mx-expectedMax)<1e-7 && std::abs(answer.cost-expectedSum)<1e-7,"constrained bottleneck vs exhaustive mismatch");
    }
    for(int trial=0;trial<100;++trial) {
        PlaceDB db;db.uniformSites=true;db.siteSpacing=0.2;
        db.rows={{0,1.6,{0}},{1.6,1.6,{1}}};
        db.segs={{0,0,1.6,-1,0.2,40},{1,1.6,1.6,-1,0.2,40}};
        Placement p;
        for(int v=0;v<12;++v) {
            Node n;n.name="c"+std::to_string(v);n.w=(v%2+2)*0.2;n.h=1.6;
            n.gx=(int(random()%100)-50)*0.1;n.gy=(int(random()%40)-10)*0.1;
            db.nodes.push_back(n);db.movable.push_back(v);db.wSites.push_back(v%2+2);
            p.seg.push_back(v/6);p.site.push_back((v%6)*5);
        }
        Node fixed;fixed.name="pad";fixed.w=0.2;fixed.h=0.2;fixed.fixed=true;fixed.gx=9;fixed.gy=9;fixed.orient="FS";
        db.nodes.push_back(fixed);db.wSites.push_back(1);p.seg.push_back(-1);p.site.push_back(0);
        db.nodeNets.resize(13);
        for(int n=0;n<18;++n) {
            std::vector<int> pins{int(random()%13),int(random()%13),int(random()%13)};
            std::sort(pins.begin(),pins.end());pins.erase(std::unique(pins.begin(),pins.end()),pins.end());
            for(int v:pins)db.nodeNets[v].push_back(n);db.nets.push_back(pins);
        }
        OptimizeOptions opt;opt.groupSize=8;
        auto d0=displacement(db,p);paretoDisplacement(db,p,opt);auto d1=displacement(db,p);
        require(d1.total<=d0.total+1e-7 && d1.max<=d0.max+1e-7,"displacement monotonicity failed");
        require(checkLegal(db,p).empty(),"illegal Pareto placement");
        WindowOptions window;window.maxSites=80;
        d0=displacement(db,p);optimizeWindows(db,p,window);d1=displacement(db,p);
        require(d1.total<=d0.total+1e-7 && d1.max<=d0.max+1e-7 && checkLegal(db,p).empty(),"window displacement/legality failed");
        double h0=totalHPWL(db,p);polishHPWL(db,p,opt);
        optimizeWindows(db,p,window,true);
        reorderWindows(db,p,2);
        require(totalHPWL(db,p)<=h0+1e-7 && checkLegal(db,p).empty(),"polish HPWL/legality failed");
        require(p.seg.back()==-1 && db.nodes.back().orient=="FS","fixed cell changed");
    }
    std::cout<<"PASS: 250 exhaustive assignment/bottleneck cases and 100 randomized placement cases\n";
}
