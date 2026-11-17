#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define SW 1100
#define SH 720
#define PANEL_X 0
#define MAX_VEHICLES 32
#define CX 390
#define CY 350
#define ROAD_W 300
#define ROAD_H 250

typedef enum { RED=0, YELLOW=1, GREEN=2 } Signal;
typedef struct { int active, dir, lane, type, w, h; float x,y,speed; SDL_Color color; } Vehicle;
typedef struct { int x,y,w,h; } Button;
static SDL_Window *window;
static SDL_Renderer *renderer;
static Vehicle cars[MAX_VEHICLES];
/* Approaches: 0 north->south, 1 south->north, 2 west->east, 3 east->west */
static Signal signals[4]={RED,RED,GREEN,GREEN};
static Button buttons[4][3];
static int running=1, paused=0, passed=0;
static Uint32 lastSpawn=0;
static float speedFactor=1.0f;

static SDL_Color road={54,58,63,255}, edge={155,157,145,255}, white={239,238,225,255};
static SDL_Color dark={24,31,35,255}, panel={32,45,51,255}, text={232,236,231,255};
static SDL_Color muted={155,173,172,255}, red={219,66,64,255}, yellow={239,190,65,255};
static SDL_Color green={66,184,105,255}, cyan={50,210,230,255};

static void color(SDL_Color c){ SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
static void rect(int x,int y,int w,int h,SDL_Color c){ SDL_Rect r={x,y,w,h}; color(c); SDL_RenderFillRect(renderer,&r); }
static void outline(int x,int y,int w,int h,SDL_Color c){ SDL_Rect r={x,y,w,h}; color(c); SDL_RenderDrawRect(renderer,&r); }
static void line(int x,int y,int xx,int yy,SDL_Color c){ color(c); SDL_RenderDrawLine(renderer,x,y,xx,yy); }
static void circle(int x,int y,int r,SDL_Color c){ color(c); for(int dy=-r;dy<=r;dy++){int dx=(int)sqrt((double)r*r-(double)dy*dy); SDL_RenderDrawLine(renderer,x-dx,y+dy,x+dx,y+dy);} }

/* Compact built-in font: A-Z, digits and basic punctuation. */
static const unsigned char *glyph(char ch){
 static const unsigned char blank[7]={0,0,0,0,0,0,0};
 static const unsigned char g[36][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
 static const unsigned char dash[7]={0,0,0,31,0,0,0}, colon[7]={0,4,4,0,4,4,0};
 if(ch>='a'&&ch<='z')ch-=32;
 if(ch>='A'&&ch<='Z')return g[ch-'A'];
 if(ch>='0'&&ch<='9')return g[26+ch-'0'];
 if(ch=='-')return dash; if(ch==':')return colon; return blank;
}
static void txt(int x,int y,const char*s,int scale,SDL_Color c){
 color(c); while(*s){char ch=*s++; if(ch==' '){x+=4*scale;continue;} const unsigned char*r=glyph(ch);
 for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(r[yy]&(1<<(4-xx))){SDL_Rect p={x+xx*scale,y+yy*scale,scale,scale};SDL_RenderFillRect(renderer,&p);} x+=6*scale;}
}
static int hit(Button b,int x,int y){return x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;}

static const char *names[4]={"NORTH TO SOUTH","SOUTH TO NORTH","WEST TO EAST","EAST TO WEST"};
static int isVertical(int d){return d<2;}
static int lanePos(int d, int lane){
    /* Two lanes per approach, all positioned inside the painted road surface. */
    switch(d){
        case 0: return lane ? 455 : 415;  /* north -> south */
        case 1: return lane ? 645 : 605;  /* south -> north */
        case 2: return lane ? 300 : 340;  /* west -> east */
        default: return lane ? 420 : 460; /* east -> west */
    }
}
static int stopLine(int d){
    /* Entry stop lines are at the edge of the central junction. */
    switch(d){case 0:return 220;case 1:return 500;case 2:return 370;default:return 730;}
}
static int allowed(int d){return signals[d]==GREEN;}

static void drawRoad(void){
    rect(0,0,SW,SH,(SDL_Color){29,39,43,255});
    /* Central crossing leaves four uncluttered corners for signal panels. */
    rect(370,0,360,SH,road);
    rect(0,220,SW,280,road);
    rect(358,0,12,SH,edge); rect(730,0,12,SH,edge);
    rect(0,208,SW,12,edge); rect(0,500,SW,12,edge);
    /* Vertical lane markings */
    for(int y=0;y<208;y+=42){rect(425,y+8,3,22,white);rect(505,y+8,3,22,white);rect(585,y+8,3,22,white);rect(665,y+8,3,22,white);}
    for(int y=512;y<SH;y+=42){rect(425,y+8,3,22,white);rect(505,y+8,3,22,white);rect(585,y+8,3,22,white);rect(665,y+8,3,22,white);}
    /* Horizontal lane markings */
    for(int x=0;x<358;x+=42){rect(x+8,280,22,3,white);rect(x+8,435,22,3,white);}
    for(int x=742;x<SW;x+=42){rect(x+8,280,22,3,white);rect(x+8,435,22,3,white);}
    /* Stop bars and zebra crossings */
    rect(370,220,360,4,white); rect(370,496,360,4,white);
    rect(370,220,4,280,white); rect(726,220,4,280,white);
    for(int i=0;i<10;i++){rect(380+i*34,228,20,18,white);rect(380+i*34,474,20,18,white);}
    for(int i=0;i<7;i++){rect(8+i*45,228,22,18,white);rect(8+i*45,474,22,18,white);rect(750+i*48,228,22,18,white);rect(750+i*48,474,22,18,white);}
    outline(525,350,50,10,(SDL_Color){90,95,96,255});
}

static void drawSignal(int d,int x,int y){
    txt(x,y,names[d],2,text);
    for(int st=0;st<3;st++){
        Button b=buttons[d][st];
        SDL_Color lamp=st==0?red:(st==1?yellow:green);
        SDL_Color bg=signals[d]==st?(st==0?(SDL_Color){130,45,45,255}:st==1?(SDL_Color){118,92,32,255}:(SDL_Color){35,100,62,255}):dark;
        rect(b.x,b.y,b.w,b.h,bg);
        outline(b.x,b.y,b.w,b.h,signals[d]==st?lamp:(SDL_Color){90,100,102,255});
        circle(b.x+14,b.y+b.h/2,6,lamp);
        txt(b.x+32,b.y+4,st==0?"RED - STOP":st==1?"YELLOW - WAIT":"GREEN - GO",2,text);
    }
}
static void drawRoadSignal(int d,int x,int y){
    rect(x-13,y-8,26,70,(SDL_Color){16,20,22,255});
    outline(x-13,y-8,26,70,(SDL_Color){105,113,111,255});
    for(int st=0;st<3;st++){
        SDL_Color c=st==0?red:(st==1?yellow:green);
        circle(x,y+st*21,7,signals[d]==st?c:(SDL_Color){65,69,66,255});
    }
}
static void drawAllRoadSignals(void){
    drawRoadSignal(0, 755, 185);
    drawRoadSignal(1, 345, 535);
    drawRoadSignal(2, 340, 185);
    drawRoadSignal(3, 760, 535);
}
static void drawPanel(void){
    /* Four compact cards occupy the screen corners; all fit within 720px height. */
    rect(12,10,315,185,panel); outline(12,10,315,185,(SDL_Color){65,91,96,255});
    rect(773,10,315,185,panel); outline(773,10,315,185,(SDL_Color){65,91,96,255});
    rect(12,515,315,190,panel); outline(12,515,315,190,(SDL_Color){65,91,96,255});
    rect(773,515,315,190,panel); outline(773,515,315,190,(SDL_Color){65,91,96,255});
    txt(25,20,"NORTH TO SOUTH",2,text);
    txt(786,20,"EAST TO WEST",2,text);
    txt(25,525,"WEST TO EAST",2,text);
    txt(786,525,"SOUTH TO NORTH",2,text);
    for(int st=0;st<3;st++){
        buttons[0][st]=(Button){25,50+st*28,285,24};
        buttons[3][st]=(Button){786,50+st*28,285,24};
        buttons[2][st]=(Button){25,557+st*28,285,24};
        buttons[1][st]=(Button){786,557+st*28,285,24};
    }
    /* Draw the buttons using their assigned corner positions. */
    for(int st=0;st<3;st++){
        int ds[4]={0,3,2,1};
        for(int k=0;k<4;k++){
            int d=ds[k]; Button b=buttons[d][st];
            SDL_Color lamp=st==0?red:(st==1?yellow:green);
            SDL_Color bg=signals[d]==st?(st==0?(SDL_Color){130,45,45,255}:st==1?(SDL_Color){118,92,32,255}:(SDL_Color){35,100,62,255}):dark;
            rect(b.x,b.y,b.w,b.h,bg); outline(b.x,b.y,b.w,b.h,signals[d]==st?lamp:(SDL_Color){90,100,102,255});
            circle(b.x+14,b.y+b.h/2,6,lamp);
            txt(b.x+32,b.y+4,st==0?"RED - STOP":st==1?"YELLOW - WAIT":"GREEN - GO",2,text);
        }
    }
}

static SDL_Color palette[6]={{215,220,217,255},{62,132,188,255},{177,72,64,255},{76,143,95,255},{206,160,75,255},{103,91,143,255}};
static void spawn(void){
 for(int i=0;i<MAX_VEHICLES;i++)if(!cars[i].active){
  Vehicle*v=&cars[i];v->active=1;v->dir=rand()%4;v->lane=rand()%2;v->type=rand()%6;v->speed=1.1f+(rand()%75)/100.0f;v->color=palette[rand()%6];
  /* type: car, bus, truck, auto-rickshaw, motorcycle, scooter */
  if(isVertical(v->dir)){
      v->w=(v->type==4||v->type==5)?13:(v->type==3?21:29);
      v->h=v->type==1?70:(v->type==2?66:(v->type==3?36:42));
      v->x=lanePos(v->dir,v->lane);v->y=v->dir==0?-50:SH+50;
  } else {
      v->w=v->type==1?70:(v->type==2?66:(v->type==3?36:42));
      v->h=(v->type==4||v->type==5)?13:(v->type==3?21:29);
      v->x=v->dir==2?-55:SW+55;v->y=lanePos(v->dir,v->lane);
  }
  return;
 }
}
static void update(float dt){
 float mul=dt/16.0f*speedFactor;if(mul>2.5f)mul=2.5f;
 for(int i=0;i<MAX_VEHICLES;i++){Vehicle*v=&cars[i];if(!v->active)continue;
  float step=v->speed*mul; int vert=isVertical(v->dir); float *pos=vert?&v->y:&v->x;
  float next=*pos+(v->dir==0||v->dir==2?step:-step);
  int stop=stopLine(v->dir); float half=vert?v->h/2.0f:v->w/2.0f;
  if(!allowed(v->dir)){
   if((v->dir==0||v->dir==2)&&*pos<stop&&next+half>stop)next=stop-half;
   if((v->dir==1||v->dir==3)&&*pos>stop&&next-half<stop)next=stop+half;
  }
  for(int j=0;j<MAX_VEHICLES;j++){Vehicle*o=&cars[j];if(!o->active||o==v||o->dir!=v->dir||o->lane!=v->lane)continue;
   float op=vert?o->y:o->x;float oh=vert?o->h/2.0f:o->w/2.0f;
   if((v->dir==0||v->dir==2)&&op>*pos){float max=op-oh-half-18;if(next>max)next=max;}
   if((v->dir==1||v->dir==3)&&op<*pos){float min=op+oh+half+18;if(next<min)next=min;}
  }
  *pos=next;
  if((v->dir==0&&v->y-v->h/2>SH)||(v->dir==1&&v->y+v->h/2<0)||(v->dir==2&&v->x-v->w/2>SW)||(v->dir==3&&v->x+v->w/2<0)){v->active=0;passed++;}
 }
}
static void drawVehicle(Vehicle*v){
 if(!v->active)return;
 int x=(int)v->x,y=(int)v->y,w=v->w,h=v->h;
 SDL_Color tire={18,21,23,255}, glass={105,164,185,255};
 if(v->type==4||v->type==5){
    /* Motorcycle and scooter: very narrow body and exposed wheels. */
    if(isVertical(v->dir)){
        circle(x,y-h/3,3,tire);circle(x,y+h/3,3,tire);
        rect(x-3,y-h/4,6,h/2,v->type==4?v->color:(SDL_Color){230,184,87,255});
        circle(x,y-2,3,(SDL_Color){35,39,42,255});
        rect(x-2,y-1,4,6,(SDL_Color){225,224,210,255});
    }else{
        circle(x-w/3,y,3,tire);circle(x+w/3,y,3,tire);
        rect(x-w/4,y-3,w/2,6,v->type==4?v->color:(SDL_Color){230,184,87,255});
        circle(x+1,y,3,(SDL_Color){35,39,42,255});
    }
    return;
 }
 rect(x-w/2+2,y-h/2+3,w,h,(SDL_Color){18,21,23,255});
 rect(x-w/2,y-h/2,w,h,v->color);
 if(v->type==3){
    /* Auto-rickshaw: yellow canopy, dark front and passenger cabin. */
    if(isVertical(v->dir)){
        rect(x-w/2+3,y-h/2+4,w-6,h/3,(SDL_Color){228,190,77,255});
        rect(x-w/2+4,y-h/2+7,w-8,6,(SDL_Color){43,53,52,255});
        rect(x-w/2+4,y+2,w-8,h/4,glass);
    }else{
        rect(x-w/2+4,y-h/2+3,w/3,h-6,(SDL_Color){228,190,77,255});
        rect(x-w/2+6,y-h/2+4,6,h-8,(SDL_Color){43,53,52,255});
        rect(x+3,y-h/2+4,w/4,h-8,glass);
    }
 }else if(v->type==1){
    /* Bus: long body with multiple passenger windows. */
    if(isVertical(v->dir)){
        for(int k=0;k<4;k++)rect(x-w/2+4,y-h/2+8+k*13,w-8,8,glass);
        rect(x-w/2+4,y+h/2-9,w-8,3,(SDL_Color){235,75,65,255});
    }else{
        for(int k=0;k<4;k++)rect(x-w/2+8+k*13,y-h/2+4,8,h-8,glass);
        rect(x+w/2-8,y-h/2+4,3,h-8,(SDL_Color){235,75,65,255});
    }
 }else if(v->type==2){
    /* Truck: cargo box with ribs and a separate cab. */
    if(isVertical(v->dir)){
        rect(x-w/2+3,y-h/2+3,w-6,h*2/3,(SDL_Color){170,177,174,255});
        for(int k=0;k<5;k++)line(x-w/2+4,y-h/2+7+k*8,x+w/2-4,y-h/2+7+k*8,(SDL_Color){105,114,113,255});
        rect(x-w/2+4,y+h/2-h/3,w-8,h/4,(SDL_Color){66,130,174,255});
        rect(x-w/2+5,y+h/2-h/3+3,w-10,5,glass);
    }else{
        rect(x-w/2+3,y-h/2+3,w*2/3,h-6,(SDL_Color){170,177,174,255});
        for(int k=0;k<5;k++)line(x-w/2+7+k*8,y-h/2+4,x-w/2+7+k*8,y+h/2-4,(SDL_Color){105,114,113,255});
        rect(x+w/2-w/3,y-h/2+4,w/4,h-8,(SDL_Color){66,130,174,255});
        rect(x+w/2-w/3+3,y-h/2+5,5,h-10,glass);
    }
 }else{
    /* Passenger car: short body and two windshields. */
    if(isVertical(v->dir)){
        rect(x-w/2+4,y-h/2+7,w-8,h/4,glass);
        rect(x-w/2+4,y+2,w-8,h/4,glass);
        rect(x-w/2+5,y+h/2-5,w-10,3,(SDL_Color){235,75,65,255});
    }else{
        rect(x-w/2+7,y-h/2+4,w/4,h-8,glass);
        rect(x+2,y-h/2+4,w/4,h-8,glass);
        rect(x+w/2-5,y-h/2+5,3,h-10,(SDL_Color){235,75,65,255});
    }
 }
}

static void stats(void){
 int count[4]={0},total=0;
 for(int i=0;i<MAX_VEHICLES;i++)if(cars[i].active){count[cars[i].dir]++;total++;}
 char b[40];
 snprintf(b,sizeof(b),"ON ROAD: %d",total);txt(25,665,b,1,cyan);
 snprintf(b,sizeof(b),"N-S %d  S-N %d",count[0],count[1]);txt(25,679,b,1,text);
 snprintf(b,sizeof(b),"W-E %d  E-W %d",count[2],count[3]);txt(786,665,b,1,text);
 snprintf(b,sizeof(b),"PASSED: %d",passed);txt(786,679,b,1,green);
}

static void reset(void){for(int i=0;i<MAX_VEHICLES;i++)cars[i].active=0;signals[0]=RED;signals[1]=RED;signals[2]=GREEN;signals[3]=GREEN;passed=0;paused=0;lastSpawn=SDL_GetTicks();}
int main(int argc,char**argv){
 (void)argc;(void)argv;srand((unsigned)time(NULL));
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)return 1;
 window=SDL_CreateWindow("Four-Way Traffic Signal Simulator",
     SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1040,660,SDL_WINDOW_SHOWN);
 if(!window){SDL_Quit();return 1;}
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer){SDL_DestroyWindow(window);SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(renderer, SW, SH);
 for(int st=0;st<3;st++){
   buttons[0][st]=(Button){25,50+st*28,285,24};
   buttons[3][st]=(Button){786,50+st*28,285,24};
   buttons[2][st]=(Button){25,557+st*28,285,24};
   buttons[1][st]=(Button){786,557+st*28,285,24};
 }
 reset();SDL_Event e;Uint32 prev=SDL_GetTicks();
 while(running){
  Uint32 now=SDL_GetTicks();float dt=(float)(now-prev);prev=now;if(dt>50)dt=50;
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT)running=0;
   if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT){
    int mx=e.button.x,my=e.button.y;
    for(int d=0;d<4;d++)for(int s=0;s<3;s++)if(hit(buttons[d][s],mx,my))signals[d]=(Signal)s;
   }
   if(e.type==SDL_KEYDOWN)switch(e.key.keysym.sym){case SDLK_ESCAPE:running=0;break;case SDLK_SPACE:paused=!paused;prev=SDL_GetTicks();break;case SDLK_r:reset();prev=SDL_GetTicks();break;case SDLK_UP:speedFactor+=.25f;if(speedFactor>2.5f)speedFactor=2.5f;break;case SDLK_DOWN:speedFactor-=.25f;if(speedFactor<.5f)speedFactor=.5f;break;}
  }
  if(!paused){if(now-lastSpawn>850){spawn();lastSpawn=now;}update(dt);}
  drawRoad();
  for(int i=0;i<MAX_VEHICLES;i++)drawVehicle(&cars[i]);
  drawAllRoadSignals();
  drawPanel();
  SDL_RenderPresent(renderer);SDL_Delay(16);
 }
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
