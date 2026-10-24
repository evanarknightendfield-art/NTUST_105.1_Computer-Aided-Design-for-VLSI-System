#include "../src/global.hpp"
#include <cassert>
#include <iostream>

int main() {
    PlaceDB db;Placement p;
    db.uniformSites=true;db.siteSpacing=1;
    for(int r=0;r<8;++r) {
        db.rows.push_back({double(r),1,{r}});
        db.segs.push_back({r,double(r),1,0,1,4});
        Node n;n.name="c"+std::to_string(r);n.w=4;n.h=1;n.gy=r;
        db.nodes.push_back(n);db.movable.push_back(r);db.wSites.push_back(4);
        p.seg.push_back((r+1)%8);p.site.push_back(0);
    }
    auto old=p;
    assert(checkLegal(db,p).empty());
    assert(!repairCap(db,p,0,wallSeconds()));
    assert(p.seg==old.seg && p.site==old.site);
    assert(repairCap(db,p,0,wallSeconds()+2));
    assert(checkLegal(db,p).empty() && displacement(db,p).max==0);
    db.nodes[0].gx=0.4;
    old=p;
    assert(!repairCap(db,p,0.1,wallSeconds()+0.2));
    assert(p.seg==old.seg && p.site==old.site);
    // A crowded single row requires coordinated shifts; nearest sites alone
    // overlap. The cap is checked against every cell, including incumbents.
    PlaceDB row;Placement q;
    row.rows.push_back({0,1,{0}});row.segs.push_back({0,0,1,0,1,8});
    for(int i=0;i<4;++i) {
        Node n;n.name="r"+std::to_string(i);n.w=2;n.h=1;n.gx=1.0+1.5*i;
        row.nodes.push_back(n);row.movable.push_back(i);q.seg.push_back(0);q.site.push_back(2*i);
    }
    assert(repairCap(row,q,1.0,wallSeconds()+1));
    assert(checkLegal(row,q).empty() && displacement(row,q).max<=1.0+1e-7);
    PlaceDB fractional;Placement fp;
    fractional.rows.push_back({0,1,{0}});fractional.segs.push_back({0,0,1,0,1,4});
    double targets[]={0.9,1.49,2.45};
    for(int i=0;i<3;++i){Node n;n.name="f"+std::to_string(i);n.w=n.h=1;n.gx=targets[i];
        fractional.nodes.push_back(n);fractional.movable.push_back(i);fp.seg.push_back(0);fp.site.push_back(i);}
    assert(repairCap(fractional,fp,1,wallSeconds()+1));
    assert(std::abs(displacement(fractional,fp).total-1.16)<1e-8);
    std::cout<<"PASS: global row cycle, cap, contiguous packing and rollback\n";
}
