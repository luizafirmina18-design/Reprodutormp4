// Cobra Music FTP Player - Manda do celular via FTP e toca no PS3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <sysutil/audio.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <lv2/sysfs.h>

#define CB_SIZE (0x100000)
#define HOST_SIZE (32*1024*1024)
#define MUSIC_DIR "/dev_hdd0/music"
#define MAXMUSIC 100
#define MAXNAME 128

static gcmContextData *ctx; static u32 W,H,*fb[2],fboff[2],*back; static int cur=0,first_flip=1; static volatile int running=1;
static char music_list[MAXMUSIC][MAXNAME]; static int music_count=0, sel=0, playing=-1, is_playing=0, scroll=0;
static char ps3_ip[32]="0.0.0.0";
static int ftp_on=1, ftp_clients=0;
static char status_msg[128]="AGUARDANDO CELULAR...";
static volatile int need_rescan=1;
static u32 audio_port=0; static int audio_ok=0;

// ---- AUDIO WAV ----
static void audio_init(void){
  AudioPortParam p; memset(&p,0,sizeof(p)); p.numChannels=2; p.numBlocks=8; p.attr=0; p.level=1.0f;
  if(audioInit()==0 && audioPortOpen(&p,&audio_port)==0){ audioPortStart(audio_port); audio_ok=1; }
}
static void play_wav_thread(void *arg){
  char *fname=(char*)arg; char path[256]; snprintf(path,sizeof(path),"%s/%s",MUSIC_DIR,fname);
  FILE *f=fopen(path,"rb"); if(!f){ snprintf(status_msg,sizeof(status_msg),"ERRO AO ABRIR"); free(fname); return; }
  // Pula header WAV 44 bytes - V1 toca WAV 44100Hz 16bit stereo
  fseek(f,44,SEEK_SET);
  short buf[2048]; float fbuf[4096];
  while(is_playing && running){
    int n=fread(buf,2,2048,f); if(n<=0) break;
    for(int i=0;i<n;i++) fbuf[i]=buf[i]/32768.0f;
    u64 w=0; audioPortWrite(audio_port,fbuf,n/2,&w);
  }
  fclose(f); is_playing=0; playing=-1; snprintf(status_msg,sizeof(status_msg),"FIM - ESCOLHA OUTRA"); free(fname);
  sysThreadExit(0);
}
static void start_play(int idx){
  if(idx<0||idx>=music_count) return;
  // So WAV nessa V1 - MP3 a gente converte no celular
  if(!strstr(music_list[idx],".wav") && !strstr(music_list[idx],".WAV")){
    snprintf(status_msg,sizeof(status_msg),"V1 TOCA WAV! CONVERTE NO CEL");
    return;
  }
  is_playing=0; usleep(100000);
  is_playing=1; playing=idx; snprintf(status_msg,sizeof(status_msg),"TOCANDO: %s",music_list[idx]);
  char *copy=strdup(music_list[idx]);
  sys_ppu_thread_t th; sysThreadCreate(&th, play_wav_thread, copy, 0, 0x10000, 0, "play");
}

// ---- SCAN PASTA ----
static void scan_music(void){
  mkdir(MUSIC_DIR,0777);
  DIR *d=opendir(MUSIC_DIR); music_count=0;
  if(d){ struct dirent *e; while((e=readdir(d)) && music_count<MAXMUSIC){
    if(e->d_name[0]=='.') continue;
    if(strstr(e->d_name,".wav")||strstr(e->d_name,".WAV")||strstr(e->d_name,".mp3")||strstr(e->d_name,".MP3")){
      strncpy(music_list[music_count],e->d_name,MAXNAME-1); music_count++;
    }
  } closedir(d); }
  if(music_count==0) snprintf(status_msg,sizeof(status_msg),"PASTA VAZIA - MANDA VIA FTP!");
  else snprintf(status_msg,sizeof(status_msg),"%d MUSICAS ENCONTRADAS",music_count);
  need_rescan=0;
}

// ---- FTP SERVER SIMPLES ----
static int send_all(int s,const char *buf,int len){ int sent=0; while(sent<len){ int n=send(s,buf+sent,len-sent,0); if(n<=0) return -1; sent+=n; } return sent; }
static void ftp_handle(int client){
  char buf[1024]; char data_ip[32]; int pasv_sock=-1; int pasv_port=0;
  send_all(client,"220 Cobra Music FTP Pronto\r\n",27);
  while(running){
    memset(buf,0,sizeof(buf)); int n=recv(client,buf,sizeof(buf)-1,0); if(n<=0) break;
    // Remove \r\n
    char *p=strstr(buf,"\r\n"); if(p) *p=0;
    if(strncmp(buf,"USER",4)==0) send_all(client,"230 OK\r\n",8);
    else if(strncmp(buf,"PASS",4)==0) send_all(client,"230 OK\r\n",8);
    else if(strncmp(buf,"SYST",4)==0) send_all(client,"215 PS3 Cobra Music\r\n",19);
    else if(strncmp(buf,"FEAT",4)==0) send_all(client,"211 FIM\r\n",9);
    else if(strncmp(buf,"PWD",3)==0) send_all(client,"257 \"/music\"\r\n",13);
    else if(strncmp(buf,"TYPE",4)==0) send_all(client,"200 OK\r\n",8);
    else if(strncmp(buf,"PASV",4)==0){
      if(pasv_sock>=0) close(pasv_sock);
      pasv_sock=socket(AF_INET,SOCK_STREAM,0);
      struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=0;
      bind(pasv_sock,(struct sockaddr*)&a,sizeof(a)); listen(pasv_sock,1);
      socklen_t l=sizeof(a); getsockname(pasv_sock,(struct sockaddr*)&a,&l); pasv_port=ntohs(a.sin_port);
      int ip1,ip2,ip3,ip4; sscanf(ps3_ip,"%d.%d.%d.%d",&ip1,&ip2,&ip3,&ip4);
      char resp[128]; snprintf(resp,sizeof(resp),"227 Entering Passive (%d,%d,%d,%d,%d,%d)\r\n",ip1,ip2,ip3,ip4,pasv_port/256,pasv_port%256);
      send_all(client,resp,strlen(resp));
    }
    else if(strncmp(buf,"LIST",4)==0){
      send_all(client,"150 LIST\r\n",10);
      int ds=accept(pasv_sock,NULL,NULL);
      if(ds>=0){ char listbuf[4096]; listbuf[0]=0; for(int i=0;i<music_count;i++){ char line[256]; snprintf(line,sizeof(line),"-rw-r--r-- 1 0 0 1234 Jan 01 00:00 %s\r\n",music_list[i]); strcat(listbuf,line); } send_all(ds,listbuf,strlen(listbuf)); close(ds); }
      if(pasv_sock>=0){ close(pasv_sock); pasv_sock=-1; }
      send_all(client,"226 OK\r\n",8); need_rescan=1;
    }
    else if(strncmp(buf,"STOR",4)==0){
      char *fname=buf+5; // STOR nome
      send_all(client,"150 RECEBENDO\r\n",16);
      int ds=accept(pasv_sock,NULL,NULL);
      if(ds>=0){ char path[256]; snprintf(path,sizeof(path),"%s/%s",MUSIC_DIR,fname); FILE *f=fopen(path,"wb"); if(f){ char db[4096]; int rn; while((rn=recv(ds,db,sizeof(db),0))>0) fwrite(db,1,rn,f); fclose(f); snprintf(status_msg,sizeof(status_msg),"RECEBIDO: %s",fname); } close(ds); }
      if(pasv_sock>=0){ close(pasv_sock); pasv_sock=-1; }
      send_all(client,"226 RECEBIDO\r\n",14); need_rescan=1;
    }
    else if(strncmp(buf,"QUIT",4)==0){ send_all(client,"221 TCHAU\r\n",11); break; }
    else send_all(client,"200 OK\r\n",8);
  }
  if(pasv_sock>=0) close(pasv_sock);
  close(client); ftp_clients--;
}
static void ftp_thread(void *arg){
  (void)arg;
  int srv=socket(AF_INET,SOCK_STREAM,0); int opt=1; setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(21);
  if(bind(srv,(struct sockaddr*)&a,sizeof(a))!=0){ snprintf(status_msg,sizeof(status_msg),"FTP ERRO - USA WEBMAN"); return; }
  listen(srv,5);
  while(running){ struct sockaddr_in c; socklen_t l=sizeof(c); int cl=accept(srv,(struct sockaddr*)&c,&l); if(cl<0) continue; ftp_clients++; sys_ppu_thread_t th; sysThreadCreate(&th, ftp_handle, (void*)(long)cl, 0, 0x10000, 0, "ftp"); }
}
static void get_ip(void){
  // Tenta pegar IP via net
  char ip[32]; if(netGetIfAddr("eth0",ip,32)==0) strncpy(ps3_ip,ip,sizeof(ps3_ip)-1);
  else strncpy(ps3_ip,"192.168.0.X - VEJA NO WEBMAN",sizeof(ps3_ip)-1);
}

// ---- VIDEO ----
static void sysutil_cb(u64 s,u64 p,void*u){(void)p;(void)u; if(s==SYSUTIL_EXIT_GAME) running=0;}
static void init_screen(void){ void *host=memalign(1024*1024,HOST_SIZE); rsxInit(&ctx,CB_SIZE,HOST_SIZE,host); videoState st; videoGetState(VIDEO_PRIMARY,0,&st); u32 resid=st.displayMode.resolution; if(videoGetResolutionAvailability(VIDEO_PRIMARY,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0)) resid=VIDEO_RESOLUTION_720; videoResolution res; videoGetResolution(resid,&res); W=res.width; H=res.height; videoConfiguration vc; memset(&vc,0,sizeof(vc)); vc.resolution=resid; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=W*4; videoConfigure(VIDEO_PRIMARY,&vc,NULL,0); videoGetState(VIDEO_PRIMARY,0,&st); gcmSetFlipMode(GCM_FLIP_VSYNC); for(int i=0;i<2;i++){fb[i]=(u32*)rsxMemalign(64,W*H*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],W*4,W,H);} back=(u32*)memalign(64,W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus(); }
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){ if(!first_flip) wait_flip(); else gcmResetFlipStatus(); memcpy(fb[cur],back,W*H*4); gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1; first_flip=0; }
static void fill_rect(int x,int y,int w,int h,u32 c){ int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h; if(x1>(int)W)x1=W; if(y1>(int)H)y1=H; for(int yy=y0;yy<y1;yy++){u32*p=back+yy*W; for(int xx=x0;xx<x1;xx++)p[xx]=c;} }
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[]={ {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},{'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},{'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},{'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},{'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},{'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},{'C',"###" "#.." "#.." "#.." "###"},{'E',"###" "#.." "###" "#.." "###"},{'F',"###" "#.." "###" "#.." "#.."},{'G',"###" "#.." "#.#" "#.#" "###"},{'I',"###" ".#." ".#." ".#." "###"},{'M',"#.#" "###" "###" "#.#" "#.#"},{'N',"#.#" "###" "###" "#.#" "#.#"},{'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},{'R',"##." "#.#" "##." "#.#" "#.#"},{'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},{'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},{'X',"#.#" "#.#" ".#." "#.#" "#.#"},{':',"..." ".#." "..." ".#." "..."},{'.',"..." "..." "..." "..." ".#."},{'-',"..." "..." "###" "..." "..."},{' ',"..." "..." "..." "..." "..."} };
static const char *find_glyph(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){ for(;*s;s++){ const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc; } }
static void draw_text_clipped(const char *s,int x,int y,int sc,u32 c,int maxchars){ char tmp[64]; strncpy(tmp,s,maxchars); tmp[maxchars]=0; draw_text(tmp,x,y,sc,c); }

int main(int argc,char**argv){ (void)argc;(void)argv; init_screen(); ioPadInit(7); sysUtilRegisterCallback(0,sysutil_cb,NULL);
  netInitialize(); audio_init(); get_ip(); scan_music();
  sys_ppu_thread_t ft; sysThreadCreate(&ft, ftp_thread, NULL, 0, 0x10000, 0, "ftpserver");
  int up_prev=0, down_prev=0, x_prev=0, sq_prev=0;
  while(running){ sysUtilCheckCallback(); if(need_rescan) scan_music();
    padInfo info; padData d; int up=0,down=0,xb=0,sq=0;
    ioPadGetInfo(&info); if(info.status[0]){ ioPadGetData(0,&d); up=d.BTN_UP||((int)d.ANA_L_V<60); down=d.BTN_DOWN||((int)d.ANA_L_V>200); xb=d.BTN_CROSS; sq=d.BTN_SQUARE; }
    if(up && !up_prev){ if(sel>0) sel--; if(sel<scroll) scroll=sel; }
    if(down && !down_prev){ if(sel<music_count-1) sel++; if(sel>=scroll+10) scroll=sel-9; }
    if(xb && !x_prev){ if(music_count>0) start_play(sel); }
    if(sq && !sq_prev){ is_playing=0; playing=-1; snprintf(status_msg,sizeof(status_msg),"PARADO"); }
    up_prev=up; down_prev=down; x_prev=xb; sq_prev=sq;
    // DESENHA
    for(u32 i=0;i<W*H;i++) back[i]=0x000A0A1E;
    int ui=4; fill_rect(0,0,W,130,0x001A1A3A);
    draw_text("COBRA MUSIC",30,20,ui*2,0x00FF50C8);
    draw_text("FTP PLAYER",30,20+10*ui,ui,0x00FFFFFF);
    char ipbuf[64]; snprintf(ipbuf,sizeof(ipbuf),"IP: %s PORTA: 21",ps3_ip); draw_text(ipbuf,W/2,30,ui,0x0066FF66);
    char clbuf[64]; snprintf(clbuf,sizeof(clbuf),"CLIENTES: %d",ftp_clients); draw_text(clbuf,W/2,30+7*ui,ui,0x00FFD23F);
    draw_text(status_msg,30,150,ui,0x00FFD23F);
    if(music_count==0){ draw_text("SEM MUSICA",60,260,ui,0x00809BBF); draw_text("MANDA PELO CELULAR VIA FTP",60,300,ui,0x00809BBF); }
    for(int i=0;i<10 && scroll+i<music_count;i++){ int idx=scroll+i; int y=200+i*42; u32 c=0x00FFFFFF; if(idx==sel){ fill_rect(20,y-8,W-40,36,0x003333FF); c=0x00FFFF00; } if(idx==playing) c=0x0066FF66; char num[8]; snprintf(num,sizeof(num),"%d",idx+1); draw_text(num,30,y,ui,c); draw_text_clipped(music_list[idx],90,y,ui,c,28); }
    draw_text("X TOCAR  QUADRADO PARAR  CIMA BAIXO ESCOLHER",30,(int)H-40,ui,0x00809BBF);
    present(); usleep(16000);
  }
  netDeinitialize(); return 0; }