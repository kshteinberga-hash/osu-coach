#include "overview.h"
#include "gui.h"
#include "ui_art.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

namespace {
constexpr ImU32 white = IM_COL32(245,247,241,255), soft = IM_COL32(171,199,211,255);
constexpr ImU32 pink = IM_COL32(255,52,146,255), cyan = IM_COL32(37,220,241,255);
constexpr ImU32 ink = IM_COL32(16,27,37,255), muted = IM_COL32(64,77,83,255);

// The artwork has six intentionally empty zones. Coordinates are in its 1400x1000 design space.
// Text and hit targets share this transform so resize/DPI cannot separate appearance from behavior.
struct Canvas {
    ImVec2 origin;
    float scale;
    ImDrawList* draw;
    ImVec2 point(float x, float y) const { return {origin.x+x*scale,origin.y+y*scale}; }
    float fontSize(float size) const { return std::max(S(11),size*scale); }
    void text(float x,float y,float w,float h,float size,ImU32 color,const std::string& value,int face=BODY) const {
        ImFont* f=font(face); float fs=fontSize(size);
        std::string visible=value;
        auto fits=[&] { return f->CalcTextSizeA(fs,FLT_MAX,w*scale,visible.c_str()).y <= h*scale+1; };
        while(!fits() && visible.size()>4) {
            auto end=visible.find_last_of(" \n",visible.size()-4);
            if(end==std::string::npos) break;
            visible.resize(end); visible+="...";
        }
        auto p=point(x,y), q=point(x+w,y+h);
        draw->PushClipRect(p,q,true);
        draw->AddText(f,fs,p,color,visible.c_str(),nullptr,w*scale);
        draw->PopClipRect();
    }
    bool target(const char* id,float x,float y,float w,float h,const std::string& tooltip="") const {
        ImGui::SetCursorScreenPos(point(x,y));
        bool pressed=ImGui::InvisibleButton(id,{w*scale,h*scale},ImGuiButtonFlags_EnableNav);
        if(ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if(!tooltip.empty()) ImGui::SetTooltip("%s",tooltip.c_str());
        }
        if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) draw->AddRect(point(x,y),point(x+w,y+h),cyan,0,0,2);
        return pressed;
    }
    bool button(const char* id,const char* label,float x,float y,float w,float h,bool primary=false,bool disabled=false) const {
        ImGui::BeginDisabled(disabled);
        bool pressed=target(id,x,y,w,h);
        bool hover=ImGui::IsItemHovered(), held=ImGui::IsItemActive();
        ImU32 fill=primary ? pink : ink;
        if(hover) fill=primary ? IM_COL32(221,19,114,255) : IM_COL32(39,59,70,255);
        if(disabled) fill=IM_COL32(105,111,103,255);
        draw->AddRectFilled(point(x,y),point(x+w,y+h),fill,2*scale);
        if(held || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)) draw->AddRect(point(x,y),point(x+w,y+h),white,2*scale,0,2*scale);
        text(x+18,y+8,w-48,h-8,22,primary ? ink : white,label,BAR);
        text(x+w-30,y+8,20,h-8,22,primary ? ink : white,"\xE2\x86\x92",BAR);
        ImGui::EndDisabled();
        return pressed;
    }
};

struct TrendDay { int date=0; double misses[3]{}, objects[3]{}; };
void trendChart(const Canvas& c,const Report& report,int generation) {
    static int cachedGeneration=-1;
    static std::vector<TrendDay> days;
    if(cachedGeneration!=generation) {
        cachedGeneration=generation;
        std::map<int,TrendDay> byDate;
        const Skill skills[]={JUMPS,STREAMS,BIGJUMPS};
        for(const auto& d:report.days) {
            auto& row=byDate[d.date]; row.date=d.date;
            for(int i=0;i<3;i++) row.misses[i]+=d.miss[skills[i]],row.objects[i]+=d.obj[skills[i]];
        }
        days.clear();
        for(const auto& entry:byDate) days.push_back(entry.second);
        if(days.size()>14) days.erase(days.begin(),days.end()-14);
    }
    c.text(65,687,420,30,25,ink,"MISS RATE BY DAY",BAR);
    const ImU32 colors[]={IM_COL32(34,78,222,255),IM_COL32(210,24,123,255),IM_COL32(16,110,84,255)};
    const char* labels[]={"Jumps","Streams","Big jumps"};
    for(int i=0;i<3;i++) {
        c.draw->AddCircleFilled(c.point(75+i*170,734),4*c.scale,colors[i]);
        c.text(87+i*170,723,150,24,18,ink,labels[i]);
    }
    if(days.size()<2) {
        c.text(80,790,520,80,24,muted,"Play on two different days to see your trend here.");
        return;
    }
    float maxRate=5;
    for(const auto& d:days) for(int i=0;i<3;i++) if(d.objects[i]>0) maxRate=std::max(maxRate,(float)(100*d.misses[i]/d.objects[i]));
    maxRate=std::ceil(maxRate/5)*5;
    const float x=90,y=766,w=490,h=125;
    for(int i=0;i<4;i++) {
        float yy=y+h*i/3;
        c.draw->AddLine(c.point(x,yy),c.point(x+w,yy),IM_COL32(49,63,35,55),c.scale);
        c.text(595,yy-9,60,22,17,muted,sf("%.0f%%",maxRate*(1-i/3.f)));
    }
    for(int k=0;k<3;k++) {
        bool previous=false; ImVec2 last;
        for(size_t i=0;i<days.size();i++) {
            const auto& d=days[i];
            if(d.objects[k]<=0) { previous=false; continue; }
            auto p=c.point(x+w*i/(days.size()-1),y+h*(1-(100*d.misses[k]/d.objects[k])/maxRate));
            if(previous) c.draw->AddLine(last,p,colors[k],2.5f*c.scale);
            c.draw->AddCircleFilled(p,2.8f*c.scale,colors[k]); last=p; previous=true;
        }
    }
    c.text(x,902,200,24,17,muted,sf("%02d/%02d",days.front().date/100%100,days.front().date%100));
    c.text(x+w-80,902,100,24,17,muted,sf("%02d/%02d",days.back().date/100%100,days.back().date%100));
}

void priorities(const Canvas& c,const Report& report) {
    c.text(735,687,430,30,25,ink,"FOCUS AREAS",BAR);
    c.text(735,721,520,24,17,muted,"Click a skill to inspect it. Hover for the full advice.");
    const int tabs[]={1,2,2,3,3,4,3};
    if(!report.nTop) c.text(745,773,550,95,24,ink,"Nothing is costing you much right now. Try slightly harder maps.");
    for(int i=0;i<report.nTop;i++) {
        const auto& wk=report.top[i]; float y=746+i*50;
        c.draw->AddRectFilled(c.point(735,y),c.point(770,y+38),ink);
        c.text(743,y-1,30,39,32,i==0 ? pink : i==1 ? cyan : white,std::to_string(i+1),TITLE);
        c.text(786,y-2,490,29,24,ink,kSkillName[wk.s],BAR);
        auto sentence=wk.fix.substr(0,wk.fix.find('.')+1);
        if(sentence.empty()) sentence=wk.fix;
        c.text(786,y+27,490,25,19,muted,sentence);
        if(c.target(("priority-"+std::to_string(i)).c_str(),730,y,560,52,wk.metric+"\n\n"+wk.fix)) gForceTab=tabs[wk.s];
    }
    if(!report.habits.empty()) {  // how you practise, not what: one line under the skills, the advice on hover
        const auto& h=report.habits[0];
        c.draw->AddRectFilled(c.point(735,902),c.point(1300,904),ink);
        c.text(745,908,540,24,18,ink,"Habit: "+h.title,BAR);
        c.target("habit",730,902,570,32,h.text);
    }
}
}

void drawOverview(App& a,double accuracy,double ur,double fullCombos,size_t recentPlays) {
    ImVec2 origin=ImGui::GetCursorScreenPos();
    float available=ImGui::GetContentRegionAvail().x;
    float width=std::min(available,S(1400));
    origin.x+=(available-width)*.5f;
    Canvas c{origin,width/1400.f,ImGui::GetWindowDrawList()};
    if(gOverviewArt) c.draw->AddImage(gOverviewArt,origin,c.point(1400,1000));
    else {
        c.draw->AddRectFilled(origin,c.point(1400,1000),ink);
        c.draw->AddRectFilled(c.point(870,195),c.point(1370,390),cWin);
        c.draw->AddRectFilled(c.point(870,420),c.point(1370,640),cYellow);
        c.draw->AddRectFilled(c.point(35,670),c.point(670,935),cYellow);
        c.draw->AddRectFilled(c.point(700,670),c.point(1360,935),cWell);
        c.text(80,35,900,130,72,pink,"osu! COACH",HEAD);
    }
    // Accuracy is a real ratio; it is not an invented practice-goal completion meter.
    auto center=c.point(130,296); float radius=49*c.scale;
    c.draw->AddCircle(center,radius,IM_COL32(65,76,82,255),64,10*c.scale);
    if(accuracy>0) {
        c.draw->PathArcTo(center,radius,-1.5707963f,-1.5707963f+6.2831853f*(float)std::clamp(accuracy/100,0.,1.),64);
        c.draw->PathStroke(pink,0,10*c.scale);
    }
    c.text(87,279,100,35,25,white,sf("%.1f%%",accuracy),BAR);
    c.text(91,350,120,22,16,soft,"ACCURACY");
    c.text(216,231,600,27,18,soft,"YOUR NEXT FOCUS",BAR);
    c.text(216,265,590,65,32,white,a.rep.verdict,TITLE);
    c.text(216,337,590,26,19,soft,sf("Based on %zu recent plays. Compared with your earlier play.",recentPlays));

    c.text(906,221,390,28,23,ink,"RECENT PLAY / AT A GLANCE",BAR);
    c.text(910,260,130,45,38,ink,sf("%.0f",ur),TITLE);
    c.text(1065,260,130,45,38,ink,sf("%.0f%%",fullCombos),TITLE);
    c.text(1220,260,120,45,38,ink,std::to_string(recentPlays),TITLE);
    c.text(910,308,140,24,18,muted,"Timing / UR");
    c.text(1065,308,140,24,18,muted,"Full combos");
    c.text(1220,308,120,24,18,muted,"Plays");
    const char* timing=a.rep.trend[0]>0 ? "Timing is improving." : a.rep.trend[0]<0 ? "Timing is less steady than usual." : "Timing is holding steady.";
    c.text(910,350,420,26,19,muted,timing);

    const PlayStats* latest=a.res.plays.empty() ? nullptr : &a.res.plays.back();
    auto watch=[&] { if(latest) { a.selPlay=(int)a.res.plays.size()-1; a.openViewer(); } };
    c.text(68,436,550,25,18,soft,"LATEST REPLAY",BAR);
    if(latest) {
        const DbMap* map=findMap(a.res.db,latest->md5);
        UiCover cover=coverForMap(map ? map->path : L"");
        auto p=c.point(68,471),q=c.point(191,594);
        if(cover.texture) {
            ImVec2 uv0{0,0},uv1{1,1};
            if(cover.aspect>1) uv0.x=(1-1/cover.aspect)*.5f,uv1.x=1-uv0.x;
            else uv0.y=(1-cover.aspect)*.5f,uv1.y=1-uv0.y;
            c.draw->AddImage(cover.texture,p,q,uv0,uv1);
        } else {
            c.draw->AddRectFilled(p,q,IM_COL32(25,49,67,255));
            for(int i=0;i<4;i++) c.draw->AddCircle(c.point(105+i*16,515+i*10),(17+i*6)*c.scale,i%2 ? pink : cyan,40,2*c.scale);
        }
        c.text(219,469,440,62,29,white,latest->title,TITLE);
        c.text(219,532,440,26,18,soft,sf("%.2f stars   /   %d combo   /   %.0f UR",latest->stars,latest->combo,latest->err.ur()));
        c.text(685,470,150,38,30,pink,sf("%.2f%%",latest->acc*100),TITLE);
        c.text(685,511,150,26,19,soft,latest->gmiss ? sf("%d misses",latest->gmiss) : "Full combo");
        int maximum=1; for(int value:latest->hist) maximum=std::max(maximum,value);
        for(int i=0;i<100;i++) {
            float x=220+i*5.7f,h=35.f*latest->hist[i]/maximum;
            if(h>0) c.draw->AddRectFilled(c.point(x,603-h),c.point(x+3.7f,603),i<45 ? cyan : i>55 ? pink : white);
        }
        c.text(219,607,580,20,15,soft,"CLICK TIMING                 EARLY  /  ON TIME  /  LATE");
        c.draw->AddCircleFilled(c.point(130,569),20*c.scale,IM_COL32(8,16,24,220));
        c.draw->AddTriangleFilled(c.point(125,558),c.point(125,580),c.point(143,569),white);
        if(c.target("latest-replay",65,465,765,166,latest->title+"\nOpen this replay with the coach")) watch();
    } else c.text(80,491,680,90,28,white,"No replay available yet.");

    c.text(901,440,360,28,23,ink,"QUICK ACTIONS",BAR);
    if(c.button("watch-latest","Watch latest replay",901,477,424,34,true,!latest)) watch();
    if(c.button("practice-maps","Find practice maps",901,518,424,34)) gForceTab=6;
    if(c.button("browse-plays","Browse your plays",901,559,424,34)) gForceTab=5;
    if(c.button("view-progress","View progress",901,600,424,34)) gForceTab=7;
    trendChart(c,a.rep,a.gen);
    priorities(c,a.rep);
    ImGui::SetCursorScreenPos(c.point(0,1000));
    ImGui::Dummy({width,1});
}
