// Compile as a separate executable with db.cpp. Reuse internal transaction API,
// but compare gap search against exhaustive independent integer-site enumeration.
#define main placer_cli_main
#include "../src/placer.cpp"
#undef main

void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
bool same(const std::vector<Slot>& a,const std::vector<Slot>& b) {
    for(size_t i=0;i<a.size();++i) if(a[i].seg!=b[i].seg || a[i].site!=b[i].site) return false;
    return true;
}
bool freeSite(const Layout& l,int v,int s,int t) {
    int w=l.db.width(v,s);
    if(t<0 || t+w>l.db.segments[s].capacity) return false;
    for(auto [site,id]:l.rows[s]) if(t<site+l.db.width(id,s) && site<t+w) return false;
    return true;
}
void checkMap(const Layout& l) {
    l.db.validate(l.p);
    size_t count=0;
    for(int s=0;s<int(l.rows.size());++s) for(auto [site,v]:l.rows[s]) {
        require(l.p[v].seg==s && l.p[v].site==site,"map/placement inconsistency");++count;
    }
    require(count==l.db.movable.size(),"missing map member");
}
int main() {
    DB db;
    db.segments={{-2,0,1.6,0.2,16},{3,2,1.6,0.3,20}};
    for(double w:{0.6,0.4,1.0,0.2,1.2,0.8,0.4}) {
        Cell c;c.name="c"+std::to_string(db.cells.size());c.w=w;c.h=1.6;c.gx=0;c.gy=0;
        db.movable.push_back(int(db.cells.size()));db.cells.push_back(c);
    }
    db.incident.resize(db.cells.size());
    Layout l(db);Options o;legalize(l,o);checkMap(l);
    std::mt19937_64 random(35);
    for(int trial=0;trial<5000;++trial) {
        int a=int(random()%db.cells.size());Slot old=l.p[a];l.erase(a);
        double x=double(int(random()%201)-100)/10,y=double(int(random()%41)-20)/10;
        double global=INF;
        for(int s=0;s<int(db.segments.size());++s) {
            double best=INF;
            for(int t=0;t<db.segments[s].capacity;++t) if(freeSite(l,a,s,t)) best=std::min(best,std::abs(db.x(a,{s,t})-x));
            int found=l.gap(a,s,x);
            require((found<0)==!std::isfinite(best),"gap feasibility mismatch");
            if(found>=0) require(freeSite(l,a,s,found) && std::abs(std::abs(db.x(a,{s,found})-x)-best)<1e-8,"gap is not nearest");
            global=std::min(global,best+std::abs(db.segments[s].y-y));
        }
        Slot q=l.nearest(a,x,y);
        require(q.seg>=0 && std::abs(std::abs(db.x(a,q)-x)+std::abs(db.y(a,q)-y)-global)<1e-8,"global nearest mismatch");
        l.insert(a,old);
        auto before=l.p;
        Change c;
        if(random()&1) c=exchange(l,a,int(random()%db.cells.size()));
        else c=relocate(l,a,int(random()%db.segments.size()),x);
        checkMap(l);
        if(c.valid && (random()&1)) {undo(l,c);require(same(before,l.p),"rollback changed positions");}
        if(!c.valid) require(same(before,l.p),"failed proposal changed positions");
        checkMap(l);
    }
    std::cout<<"PASS: 5000 exhaustive gap/nearest and transaction invariant trials\n";
}
