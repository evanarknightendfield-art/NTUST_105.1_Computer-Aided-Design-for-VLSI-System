"""Generate test-only instrumented translation units; no edits to src/."""
from pathlib import Path
s=Path('AI_Solutions/CodexCapDP/src'); t=s.parent/'tests'
def replace(text,a,b):
    assert text.count(a)==1,(a,text.count(a))
    return text.replace(a,b)
c=(s/'capsearch.cpp').read_text()
c='#include "diagnostic.hpp"\n'+c
c=replace(c,'    // ---- cleanup: phase 1 wanders','    diag::member("phase1_membership",R.list);\n    // ---- cleanup: phase 1 wanders')
c=replace(c,'    // ---- phase 2: lower the exact L1','    diag::member("cleanup_membership",R.list);\n    // ---- phase 2: lower the exact L1')
c=replace(c,'    // ---- place the segments whose membership','    diag::member("phase2_membership",R.list);\n    // ---- place the segments whose membership')
c=replace(c,'    if (rowRepair(db, out, D, ro)) { how = 1; return true; }', '''    double rt=wallSeconds(); bool rok=rowRepair(db,out,D,ro); diag::current.rowSec=wallSeconds()-rt;
    if (rok) { how = 1; auto d=displacement(db,out);diag::current.rawTotal=d.total;diag::current.rawMax=d.max;return true; }''')
c=replace(c,'    if (wallSeconds() < until && repairCap(db, out, D, until, unsigned(seed))) { how = 2; return true; }', '''    double st=wallSeconds(); bool sok=wallSeconds()<until && repairCap(db,out,D,until,unsigned(seed));diag::current.siteSec=wallSeconds()-st;
    if (sok) {how=2;auto d=displacement(db,out);diag::current.rawTotal=d.total;diag::current.rawMax=d.max;return true;}''')
c=replace(c,'    CapSearchResult res;', '    diag::attempts.clear();diag::candidates.clear();\n    CapSearchResult res;')
c=replace(c,'            bool ok = repairAt(db, best, D, std::min(aUntil', '            diag::begin(db,best,D,round,seed,"probe");diag::current.limit=opt.totalRatio*base.total;\n            bool ok = repairAt(db, best, D, std::min(aUntil')
c=replace(c,'            if (opt.verbose)\n                std::fprintf(stderr, "[cap] probe', '            diag::finish(db,q,ok,false,how);\n            if (opt.verbose)\n                std::fprintf(stderr, "[cap] probe')
c=replace(c,'            bool ok = repairAt(db, best, D, wallSeconds()', '            diag::begin(db,best,D,round,seed,"full");diag::current.limit=opt.totalRatio*base.total;\n            bool ok = repairAt(db, best, D, wallSeconds()')
c=replace(c,'                windowSweeps(db, q, w, memo);','                if(!diag::prefix().empty())diag::capture(best,q,*memo);\n                double dt=wallSeconds();windowSweeps(db, q, w, memo);diag::current.dpSec=wallSeconds()-dt;')
c=replace(c,'            if (opt.verbose)\n                std::fprintf(stderr, "[cap] D %.6f: %s%s', '            diag::finish(db,q,ok,accept,how);\n            if (opt.verbose)\n                std::fprintf(stderr, "[cap] D %.6f: %s%s')
c=replace(c,'    return res;\n}', '    diag::flush(db);\n    return res;\n}')
c+='''
// Materialize each recorded membership only AFTER repair timing has ended.
// These intermediate coordinates are counterfactual, not executed moves.
void diagnosticMaterialize(const PlaceDB& db,const Placement& base,double D,const std::string& out) {
 std::ofstream f(out+"_stages.csv");f<<std::setprecision(15)<<"stage,legal,total,max,away,dirty_segments\\n";
 for(auto& snap:diag::memberships){Repair R(db,base,D);R.list=snap.second;std::vector<char> dirty(db.segs.size(),0);int away=0;
 for(size_t s=0;s<R.list.size();++s)for(int v:R.list[s]){R.seg[v]=s;if(int(s)!=base.seg[v]||displacementOf(db,base,v)>D+1e-9)dirty[s]=dirty[base.seg[v]]=1;away+=int(s)!=base.seg[v];}
 Placement p=base;bool ok=true;std::vector<int> order,sites;
 for(size_t s=0;s<R.list.size();++s)if(dirty[s]){if(R.cost(s,R.list[s],&order,&sites)>=1e300){ok=false;break;}for(size_t j=0;j<order.size();++j){p.seg[order[j]]=s;p.site[order[j]]=sites[j];}}
 if(ok){auto d=displacement(db,p);f<<snap.first<<','<<checkLegal(db,p).empty()<<','<<d.total<<','<<d.max<<','<<away<<','<<std::count(dirty.begin(),dirty.end(),1)<<'\\n';diag::cells(db,base,p,D,out+"_"+snap.first+"_cells.csv");}
 }
}
'''
(t/'diagnostic_capsearch.cpp').write_text(c)
m=(s/'main.cpp').read_text()
m='#include "diagnostic.hpp"\n'+m
m=replace(m,'    const double extraUntil = wallSeconds() + extraTime;','    diag::snapshot(db,legal,stage1Memo,"prechain");\n    const double extraUntil = wallSeconds() + extraTime;')
m=replace(m,'    const auto beforeGlobal = displacement(db, legal);','    diag::snapshot(db,legal,stage1Memo,"precap");\n    const auto beforeGlobal = displacement(db, legal);')
(t/'diagnostic_main.cpp').write_text(m)
w=(s/'window.cpp').read_text()
w='#include "diagnostic.hpp"\n'+w
w=replace(w,'                // stamp: newest change among the window cells', '''                if(diag::localEnabled && std::none_of(w.cells.begin(),w.cells.end(),[](int v){return diag::localMask[v];}))continue;
                // stamp: newest change among the window cells''')
w=replace(w,'        solvedAll += sol;','        solvedAll += sol; diag::solvedWindows += sol;')
(t/'diagnostic_window.cpp').write_text(w)
print('Generated diagnostic_main/capsearch/window.cpp in tests/')
