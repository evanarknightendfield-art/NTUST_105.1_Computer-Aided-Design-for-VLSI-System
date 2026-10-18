#include "../src/window.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>

int main() {
    std::mt19937 rng(20261019);
    const double inf=std::numeric_limits<double>::infinity();
    for(int trial=0;trial<400;++trial) {
        WindowProblem p;p.cells=1+rng()%5;
        for(int r=0;r<2;++r) {
            p.length[r]=2+rng()%6;p.width[r].resize(p.cells);p.cost[r].resize(p.cells);
            for(int i=0;i<p.cells;++i) {
                p.width[r][i]=1+rng()%3;p.cost[r][i].resize(p.length[r]+1);
                for(double& c:p.cost[r][i]) c=rng()%5 ? (int(rng()%101)-50)*0.1 : inf;
            }
        }
        double exact=inf;
        std::array<unsigned,2> occupied{};
        std::function<void(int,double)> enumerate=[&](int i,double value) {
            if(i==p.cells) {exact=std::min(exact,value);return;}
            for(int r=0;r<2;++r) for(int x=0;x+p.width[r][i]<=p.length[r];++x) {
                unsigned bits=((1u<<p.width[r][i])-1)<<x;
                if((occupied[r]&bits) || !std::isfinite(p.cost[r][i][x]))continue;
                occupied[r]|=bits;enumerate(i+1,value+p.cost[r][i][x]);occupied[r]^=bits;
            }
        };
        enumerate(0,0);auto result=solveWindow(p);
        assert(result.feasible==std::isfinite(exact));
        if(result.feasible) {
            assert(std::fabs(result.cost-exact)<1e-8);occupied={};double sum=0;
            for(int i=0;i<p.cells;++i) {
                int r=result.row[i],x=result.site[i];
                assert(r>=0 && r<2 && x>=0 && x+p.width[r][i]<=p.length[r]);
                unsigned bits=((1u<<p.width[r][i])-1)<<x;
                assert(!(occupied[r]&bits));occupied[r]|=bits;sum+=p.cost[r][i][x];
            }
            assert(std::fabs(sum-exact)<1e-8);
        }
    }
    // A fully packed two-row fixture: two cells in EACH row must trade rows.
    // The joint optimum changes all four cells in one window solution.
    WindowProblem p;p.cells=4;p.length={3,3};
    for(int r=0;r<2;++r) {
        p.width[r]={1,2,1,2};p.cost[r].assign(4,std::vector<double>(4,10));
        for(int i=0;i<4;++i)for(double& c:p.cost[r][i])c=((i<2)==(r==1))?0:10;
    }
    auto solution=solveWindow(p);assert(solution.feasible && solution.cost==0);
    std::cout<<"PASS: 400 exhaustive arbitrary-cost two-row comparisons and coordinated row-exchange fixture\n";
    for(int trial=0;trial<150;++trial) {
        int n=1+rng()%7;OrderProblem problem;problem.origin=-3.7;problem.spacing=0.2;
        for(int i=0;i<n;++i) {
            int w=1+rng()%5;problem.widths.push_back(w);problem.centerOffsets.push_back((w-0.1)*0.1);
            if(i+1<n)problem.gaps.push_back(rng()%4);
        }
        for(int j=0;j<15;++j) {
            unsigned mask=1+rng()%((1u<<n)-1);
            double lo=(int(rng()%200)-100)*0.1,hi=lo+rng()%70*0.1;
            if(rng()%3==0){lo=inf;hi=-inf;}
            problem.nets.push_back({mask,lo,hi});
        }
        std::vector<int> order(n);std::iota(order.begin(),order.end(),0);
        auto cost=[&](const std::vector<int>& permutation) {
            std::vector<double> centers(n);int site=0;
            for(int k=0;k<n;++k) {
                int i=permutation[k];centers[i]=problem.origin+site*problem.spacing+problem.centerOffsets[i];
                site+=problem.widths[i]+(k+1<n?problem.gaps[k]:0);
            }
            double total=0;
            for(const auto& net:problem.nets) {
                double lo=net.outsideLow,hi=net.outsideHigh;
                for(int i=0;i<n;++i)if(net.members&(1u<<i)){lo=std::min(lo,centers[i]);hi=std::max(hi,centers[i]);}
                total+=hi-lo;
            }
            return total;
        };
        double best=inf;
        do{best=std::min(best,cost(order));}while(std::next_permutation(order.begin(),order.end()));
        auto result=solveOrder(problem);
        assert(std::fabs(result.cost-best)<1e-7 && std::fabs(cost(result.order)-best)<1e-7);
    }
    std::cout<<"PASS: 150 exhaustive shared-net HPWL ordering comparisons\n";
}
