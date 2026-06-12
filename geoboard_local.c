/*
 * geoboard_local.c  ---  Pipeline local de GeoBoard (un solo nodo). v4
 *
 * Cambios v4:
 *   - Un solo intento por figura: si CHECK falla, pasa automáticamente
 *     a la siguiente (con animación de error).
 *   - Umbral de downscale bajado al 15% (antes 25%) para detectar
 *     contornos delgados correctamente.
 *   - Validación con tolerancia: basta con que el 75% de los bits
 *     encendidos en el target estén también en la respuesta del usuario
 *     (permite pequeñas imprecisiones).
 *   - Imágenes 5000×5000 siguen funcionando (sin buffer bin intermedio).
 *
 * Compilación:
 *   make -f Makefile_local
 *
 * Uso:
 *   sudo ./geoboard_local images/          <- toda la carpeta
 *   sudo ./geoboard_local a.pgm b.pgm ...  <- archivos específicos
 *
 * Variables de entorno:
 *   GEOBOARD_HEAVY_PASSES=N   pasadas por worker (default 6)
 *   GEOBOARD_TOLERANCE=N      % mínimo de coincidencia (default 75)
 *
 * CE 4303 - Principios de Sistemas Operativos - Grupo 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/stat.h>

#include "DriverFiles/lib/geoboard_ioctl.h"

/* ======================================================================
 * Parámetros
 * ====================================================================== */

#define SHOW_SECONDS       10
#define CURSOR_BLINK_MS    30
#define PIXEL_THRESHOLD   128   /* píxel activo si valor < este umbral     */
#define DOWNSCALE_PCT      15   /* % mínimo de bloque activo para LED=1    */
#define DEFAULT_PASSES      6
#define DEFAULT_TOLERANCE  75   /* % de bits del target que el user acierta */

static int get_passes(void)
{
    const char *e = getenv("GEOBOARD_HEAVY_PASSES");
    return (e && atoi(e) > 0) ? atoi(e) : DEFAULT_PASSES;
}
static int get_tolerance(void)
{
    const char *e = getenv("GEOBOARD_TOLERANCE");
    int v = e ? atoi(e) : DEFAULT_TOLERANCE;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return v;
}

static const uint8_t CHACHA20_KEY[32] = {
    0x10,0x21,0x32,0x43,0x54,0x65,0x76,0x87,
    0x98,0xA9,0xBA,0xCB,0xDC,0xED,0xFE,0x0F,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00
};
static const uint8_t CHACHA20_NONCE[12] = {
    0x47,0x45,0x4F,0x42,0x4F,0x41,0x52,0x44,
    0x00,0x00,0x00,0x01
};

/* ======================================================================
 * ChaCha20
 * ====================================================================== */

#define ROTL32(v,n) (((v)<<(n))|((v)>>(32-(n))))

static void chacha20_quarter(uint32_t s[16],int a,int b,int c,int d)
{
    s[a]+=s[b];s[d]^=s[a];s[d]=ROTL32(s[d],16);
    s[c]+=s[d];s[b]^=s[c];s[b]=ROTL32(s[b],12);
    s[a]+=s[b];s[d]^=s[a];s[d]=ROTL32(s[d], 8);
    s[c]+=s[d];s[b]^=s[c];s[b]=ROTL32(s[b], 7);
}
static void chacha20_block(const uint32_t in[16],uint8_t out[64])
{
    uint32_t x[16];int i;
    memcpy(x,in,64);
    for(i=0;i<10;i++){
        chacha20_quarter(x,0,4, 8,12);chacha20_quarter(x,1,5, 9,13);
        chacha20_quarter(x,2,6,10,14);chacha20_quarter(x,3,7,11,15);
        chacha20_quarter(x,0,5,10,15);chacha20_quarter(x,1,6,11,12);
        chacha20_quarter(x,2,7, 8,13);chacha20_quarter(x,3,4, 9,14);
    }
    for(i=0;i<16;i++){
        uint32_t v=x[i]+in[i];
        out[i*4]=(uint8_t)v;out[i*4+1]=(uint8_t)(v>>8);
        out[i*4+2]=(uint8_t)(v>>16);out[i*4+3]=(uint8_t)(v>>24);
    }
}
static void chacha20_apply(uint8_t *data,uint64_t size,
                           const uint8_t key[32],const uint8_t nonce[12],
                           uint32_t ctr0,uint64_t byte_off)
{
    uint32_t st[16];uint8_t ks[64];
    uint64_t bnum=byte_off/64;uint32_t boff=(uint32_t)(byte_off%64);
    uint64_t i;int k;
    st[0]=0x61707865u;st[1]=0x3320646eu;st[2]=0x79622d32u;st[3]=0x6b206574u;
    for(k=0;k<8;k++)
        st[4+k]=(uint32_t)key[k*4]|((uint32_t)key[k*4+1]<<8)
               |((uint32_t)key[k*4+2]<<16)|((uint32_t)key[k*4+3]<<24);
    st[12]=ctr0+(uint32_t)bnum;
    st[13]=(uint32_t)nonce[0]|((uint32_t)nonce[1]<<8)|((uint32_t)nonce[2]<<16)|((uint32_t)nonce[3]<<24);
    st[14]=(uint32_t)nonce[4]|((uint32_t)nonce[5]<<8)|((uint32_t)nonce[6]<<16)|((uint32_t)nonce[7]<<24);
    st[15]=(uint32_t)nonce[8]|((uint32_t)nonce[9]<<8)|((uint32_t)nonce[10]<<16)|((uint32_t)nonce[11]<<24);
    i=0;
    if(boff>0){chacha20_block(st,ks);st[12]++;
        while(boff<64&&i<size)data[i++]^=ks[boff++];}
    while(i<size){chacha20_block(st,ks);st[12]++;
        uint32_t ch=(size-i<64)?(uint32_t)(size-i):64;
        for(uint32_t j=0;j<ch;j++)data[i++]^=ks[j];}
}

/* ======================================================================
 * PGM parser
 * ====================================================================== */

typedef struct{uint32_t width,height;uint8_t *pixels;}PgmImage;

static void free_pgm(PgmImage *img)
{if(img&&img->pixels){free(img->pixels);img->pixels=NULL;}}

static const uint8_t *pgm_skip(const uint8_t *p,const uint8_t *e)
{while(p<e){if(*p=='#'){while(p<e&&*p!='\n')p++;}
 else if(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;else break;}return p;}
static const uint8_t *pgm_uint(const uint8_t *p,const uint8_t *e,uint32_t *v)
{*v=0;while(p<e&&*p>='0'&&*p<='9'){*v=*v*10+(*p-'0');p++;}return p;}

static int parse_pgm(const uint8_t *data,uint64_t size,PgmImage *out)
{
    const uint8_t *p=data,*e=data+size;
    uint32_t w,h,mv;int p5;uint64_t n;
    if(size<3||p[0]!='P')return -1;
    if(p[1]=='5')p5=1;else if(p[1]=='2')p5=0;else return -1;
    p+=2;
    p=pgm_skip(p,e);p=pgm_uint(p,e,&w);if(!w)return -1;
    p=pgm_skip(p,e);p=pgm_uint(p,e,&h);if(!h)return -1;
    p=pgm_skip(p,e);p=pgm_uint(p,e,&mv);if(!mv)return -1;
    if(p5){if(p<e)p++;}else p=pgm_skip(p,e);
    n=(uint64_t)w*h;
    out->width=w;out->height=h;
    out->pixels=(uint8_t*)malloc(n);if(!out->pixels)return -1;
    if(p5){
        if((uint64_t)(e-p)<n){free(out->pixels);return -1;}
        if(mv==255)memcpy(out->pixels,p,n);
        else for(uint64_t i=0;i<n;i++)out->pixels[i]=(uint8_t)((uint32_t)p[i]*255u/mv);
    }else{
        for(uint64_t i=0;i<n;i++){uint32_t v;
            p=pgm_skip(p,e);p=pgm_uint(p,e,&v);
            out->pixels[i]=(uint8_t)(v*255u/mv);}
    }
    return 0;
}

/* ======================================================================
 * Worker: franja → máscara parcial 8×8
 * ====================================================================== */

typedef struct{
    uint8_t mask[8];
    uint64_t active_pixels;
    int32_t bx0,by0,bx1,by1;
}WorkerResult;

static void process_stripe(const uint8_t *pix,
                            uint32_t iw,uint32_t ih,
                            uint32_t y0,uint32_t sh,
                            uint32_t wid,int passes,
                            WorkerResult *res)
{
    uint32_t ye=y0+sh; if(ye>ih)ye=ih;
    uint32_t lh=ye-y0;
    memset(res->mask,0,8);
    res->active_pixels=0;
    res->bx0=(int32_t)iw;res->by0=(int32_t)ih;res->bx1=-1;res->by1=-1;

    /* Pasadas pesadas */
    for(int pass=0;pass<passes;pass++){
        uint64_t acc=0;
        int32_t tx0=(int32_t)iw,ty0=(int32_t)ih,tx1=-1,ty1=-1;
        for(uint32_t y=0;y<lh;y++)
            for(uint32_t x=0;x<iw;x++)
                if(pix[y*iw+x]<PIXEL_THRESHOLD){
                    acc++;
                    if((int32_t)x<tx0)tx0=(int32_t)x;
                    if((int32_t)x>tx1)tx1=(int32_t)x;
                    if((int32_t)(y0+y)<ty0)ty0=(int32_t)(y0+y);
                    if((int32_t)(y0+y)>ty1)ty1=(int32_t)(y0+y);
                }
        if(pass==passes-1){
            res->active_pixels=acc;
            res->bx0=tx0;res->bx1=tx1;res->by0=ty0;res->by1=ty1;
        }
    }

    /* Downscale directo a 8×8 con umbral DOWNSCALE_PCT */
    uint32_t lrs=wid*8u/3u;
    uint32_t lre=(wid==2)?8u:(wid+1)*8u/3u;
    uint32_t lrn=lre-lrs;
    for(uint32_t lr=0;lr<lrn;lr++){
        uint32_t rs=lr*lh/lrn, re=(lr+1)*lh/lrn;
        if(re>lh)re=lh;
        for(uint32_t lc=0;lc<8;lc++){
            uint32_t cs=lc*iw/8u, ce=(lc+1)*iw/8u;
            if(ce>iw)ce=iw;
            uint64_t tot=0,act=0;
            for(uint32_t r=rs;r<re;r++)
                for(uint32_t c=cs;c<ce;c++){
                    if(pix[r*iw+c]<PIXEL_THRESHOLD)act++;
                    tot++;
                }
            /* DOWNSCALE_PCT% en lugar del 25% anterior */
            if(tot>0 && act*100u/tot >= (uint32_t)DOWNSCALE_PCT)
                res->mask[lrs+lr]|=(uint8_t)(1u<<lc);
        }
    }
}

static void consolidate(const WorkerResult w[3],uint8_t mask[8])
{for(int r=0;r<8;r++){mask[r]=0;for(int i=0;i<3;i++)mask[r]|=w[i].mask[r];}}

/* ======================================================================
 * PGM → máscara 8×8
 * ====================================================================== */

static int pgm_to_mask(const char *path, uint8_t mask[8])
{
    int passes=get_passes();
    FILE *fp=fopen(path,"rb");
    if(!fp){fprintf(stderr,"[E] No se abre: %s\n",path);return -1;}
    fseek(fp,0,SEEK_END);long fs=ftell(fp);rewind(fp);
    if(fs<=0){fclose(fp);return -1;}
    uint8_t *raw=(uint8_t*)malloc((uint64_t)fs);
    if(!raw){fclose(fp);return -1;}
    if(fread(raw,1,(size_t)fs,fp)!=(size_t)fs){free(raw);fclose(fp);return -1;}
    fclose(fp);

    PgmImage img;memset(&img,0,sizeof(img));
    if(parse_pgm(raw,(uint64_t)fs,&img)<0){
        fprintf(stderr,"[E] PGM inválido: %s\n",path);free(raw);return -1;}
    free(raw);

    printf("  Imagen: %u x %u px  |  pasadas: %d\n",img.width,img.height,passes);
    uint32_t w=img.width,h=img.height;
    uint64_t n=(uint64_t)w*h;

    uint8_t *enc=(uint8_t*)malloc(n);
    if(!enc){free_pgm(&img);return -1;}
    memcpy(enc,img.pixels,n);
    chacha20_apply(enc,n,CHACHA20_KEY,CHACHA20_NONCE,1,0);

    WorkerResult wr[3];
    for(int wid=0;wid<3;wid++){
        uint32_t y0=(uint32_t)((uint64_t)h*(uint32_t)wid/3u);
        uint32_t y1=(wid==2)?h:(uint32_t)((uint64_t)h*(uint32_t)(wid+1)/3u);
        uint32_t sh=y1-y0;
        uint64_t off=(uint64_t)y0*w;
        chacha20_apply(enc+off,(uint64_t)sh*w,CHACHA20_KEY,CHACHA20_NONCE,1,off);
        printf("  Worker %d  filas [%u-%u]",wid,y0,y1-1);fflush(stdout);
        process_stripe(enc+off,w,h,y0,sh,(uint32_t)wid,passes,&wr[wid]);
        printf("  activos=%llu\n",(unsigned long long)wr[wid].active_pixels);
    }
    free(enc);free_pgm(&img);
    consolidate(wr,mask);
    return 0;
}

/* ======================================================================
 * Validación con tolerancia
 * ====================================================================== */

/*
 * Calcula qué porcentaje de los bits encendidos en |target| también
 * están encendidos en |user|.
 * Con tolerancia=75 el usuario puede equivocarse en hasta un 25% de
 * los LEDs de la figura y aun así pasar.
 */
static int score_pct(const uint8_t user[8], const uint8_t target[8])
{
    int total=0, hits=0;
    for(int r=0;r<8;r++){
        for(int c=0;c<8;c++){
            if((target[r]>>c)&1){
                total++;
                if((user[r]>>c)&1) hits++;
            }
        }
    }
    if(total==0) return 100;   /* target vacío → siempre correcto */
    return hits*100/total;
}

/* ======================================================================
 * Hardware
 * ====================================================================== */

static int gfd=-1;
static void hw_draw(const uint8_t *m){write(gfd,m,8);}
static int  hw_btn(void){int b=GEO_BTN_NONE;ioctl(gfd,GEO_READ_BUTTON,&b);return b;}
static void hw_buzzer(int on){int v=on;ioctl(gfd,GEO_BUZZER,&v);}
static void hw_servo(int up){int v=up;ioctl(gfd,GEO_SERVO,&v);}
static void hw_clear(void){ioctl(gfd,GEO_CLEAR);}

static void sound_success(void)
{for(int i=0;i<3;i++){hw_buzzer(1);usleep(300000);hw_buzzer(0);usleep(700000);}}

static void sound_error(void)
{hw_buzzer(1);sleep(2);hw_buzzer(0);}

static void blink_success(const uint8_t *t)
{
    uint8_t full[8],empty[8];
    memset(full,0xFF,8);memset(empty,0,8);
    for(int i=0;i<5;i++){hw_draw(full);usleep(200000);hw_draw(empty);usleep(200000);}
    hw_draw(t);usleep(500000);
}

/* Animación de error: la figura parpadea 3 veces rápido y apaga */
static void blink_error(const uint8_t *t)
{
    uint8_t empty[8]={0};
    for(int i=0;i<3;i++){
        hw_draw(t);  usleep(150000);
        hw_draw(empty);usleep(150000);
    }
    hw_clear();
}

/* Transición entre figuras: cuenta regresiva con filas de LEDs */
static void splash_next(void)
{
    uint8_t f[8]={0};
    for(int r=7;r>=0;r--){
        f[r]=0xFF;hw_draw(f);usleep(120000);
    }
    usleep(300000);
    hw_clear();
}

/* ======================================================================
 * Imprime máscara en consola
 * ====================================================================== */

static void print_mask(const uint8_t m[8],const char *label)
{
    printf("\n  Mascara [%s]:\n  +--------+\n",label);
    for(int r=0;r<8;r++){
        printf("  |");
        for(int c=0;c<8;c++)printf("%c",(m[r]>>c)&1?'#':'.');
        printf("| %02X\n",m[r]);
    }
    printf("  +--------+\n\n");
}

/* ======================================================================
 * Ronda de juego — UN solo intento
 *
 * Devuelve 1 si el usuario acertó, 0 si falló (y ya pasó a la siguiente).
 * ====================================================================== */

static int run_round(const uint8_t target[8], const char *label,
                     int fig_num, int fig_total)
{
    uint8_t user[8]={0};
    int cx=0,cy=0,prev=GEO_BTN_NONE,tick=0;
    int tol=get_tolerance();

    printf("\n[Juego] Figura %d/%d: %s\n", fig_num, fig_total, label);
    printf("        Mostrando %d s...  (tolerancia=%d%%)\n", SHOW_SECONDS, tol);

    hw_draw(target);
    sleep(SHOW_SECONDS);
    hw_clear();
    printf("        Un intento. SELECT=marcar  CHECK=validar\n");

    for(;;){
        int b=hw_btn();
        uint8_t frame[8];

        if(b!=prev && b!=GEO_BTN_NONE){
            switch(b){
            case GEO_BTN_UP:    if(cy>0)cy--;   break;
            case GEO_BTN_DOWN:  if(cy<7)cy++;   break;
            case GEO_BTN_LEFT:  if(cx>0)cx--;   break;
            case GEO_BTN_RIGHT: if(cx<7)cx++;   break;
            case GEO_BTN_SELECT:
                user[cy]^=(uint8_t)(1u<<cx); break;
            case GEO_BTN_CHECK:{
                int pct=score_pct(user,target);
                printf("        Coincidencia: %d%%\n",pct);
                if(pct>=tol){
                    printf("        Correcto!\n");
                    hw_servo(1);
                    sound_success();
                    blink_success(target);
                    hw_servo(0);
                    return 1;   /* acierto */
                } else {
                    printf("        Incorrecto (%d%% < %d%%). Pasando a la siguiente...\n",
                           pct,tol);
                    sound_error();
                    blink_error(target);  /* muestra la figura correcta brevemente */
                    return 0;   /* fallo — pasa igual */
                }
            }
            default:break;
            }
        }
        prev=b;

        memcpy(frame,user,8);
        if((tick/8)%2) frame[cy]^=(uint8_t)(1u<<cx);
        hw_draw(frame);
        tick++;
        usleep((useconds_t)(CURSOR_BLINK_MS*1000));
    }
}

/* ======================================================================
 * Recolector de PGMs
 * ====================================================================== */

static int cmp_str(const void *a,const void *b)
{return strcmp(*(const char**)a,*(const char**)b);}

static char **collect_pgms(int argc,char *argv[],int *cnt)
{
    char **paths=NULL;int n=0;
    struct stat st;
    if(argc==2&&stat(argv[1],&st)==0&&S_ISDIR(st.st_mode)){
        DIR *d=opendir(argv[1]);if(!d){*cnt=0;return NULL;}
        struct dirent *ent;
        while((ent=readdir(d))!=NULL){
            size_t nl=strlen(ent->d_name);
            if(nl<5)continue;
            const char *ext=ent->d_name+nl-4;
            if(strcasecmp(ext,".pgm")!=0)continue;
            size_t dl=strlen(argv[1]);
            char *p=(char*)malloc(dl+2+nl);if(!p)continue;
            snprintf(p,dl+2+nl,"%s/%s",argv[1],ent->d_name);
            paths=(char**)realloc(paths,(n+1)*sizeof(char*));
            paths[n++]=p;
        }
        closedir(d);
        if(n>1)qsort(paths,n,sizeof(char*),cmp_str);
    } else {
        for(int i=1;i<argc;i++){
            paths=(char**)realloc(paths,(n+1)*sizeof(char*));
            paths[n++]=strdup(argv[i]);
        }
    }
    *cnt=n;return paths;
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(int argc,char *argv[])
{
    if(argc<2){
        fprintf(stderr,
            "Uso:\n"
            "  %s <carpeta/>\n"
            "  %s <a.pgm> [b.pgm ...]\n\n"
            "Variables de entorno:\n"
            "  GEOBOARD_HEAVY_PASSES=N   (default %d)\n"
            "  GEOBOARD_TOLERANCE=N%%     (default %d)\n",
            argv[0],argv[0],DEFAULT_PASSES,DEFAULT_TOLERANCE);
        return 1;
    }

    int nf=0;
    char **pgms=collect_pgms(argc,argv,&nf);
    if(nf==0){fprintf(stderr,"No hay PGMs.\n");return 1;}

    printf("[GeoBoard] Playlist: %d figura(s)\n",nf);
    for(int i=0;i<nf;i++)printf("  [%d] %s\n",i+1,pgms[i]);
    printf("\n[GeoBoard] Pre-procesando imagenes...\n\n");

    /* Pre-procesar todas */
    uint8_t (*masks)[8]=(uint8_t(*)[8])malloc((uint64_t)nf*8);
    if(!masks)return 1;
    int *valid=(int*)calloc(nf,sizeof(int));

    for(int i=0;i<nf;i++){
        const char *base=strrchr(pgms[i],'/');
        base=base?base+1:pgms[i];
        printf("[%d/%d] %s\n",i+1,nf,base);
        if(pgm_to_mask(pgms[i],masks[i])<0){
            fprintf(stderr,"  ERROR: se omite.\n"); continue;
        }
        int has=0;for(int r=0;r<8;r++)if(masks[i][r]){has=1;break;}
        if(!has){fprintf(stderr,"  AVISO: mascara vacia, se omite.\n");continue;}
        print_mask(masks[i],base);
        valid[i]=1;
    }

    int total_valid=0;
    for(int i=0;i<nf;i++)total_valid+=valid[i];
    if(total_valid==0){
        fprintf(stderr,"Ninguna figura valida.\n");return 1;
    }

    /* Abrir driver */
    printf("[GeoBoard] Abriendo /dev/geoboard...\n");
    gfd=open("/dev/geoboard",O_RDWR);
    if(gfd<0){perror("open /dev/geoboard");return 1;}

    /* Bucle de juego */
    int fig=0,round=1,aciertos=0,intentos=0;
    printf("\n[GeoBoard] Iniciando juego — tolerancia=%d%%\n\n",get_tolerance());

    for(;;){
        if(!valid[fig]){fig=(fig+1)%nf;continue;}

        const char *base=strrchr(pgms[fig],'/');
        base=base?base+1:pgms[fig];

        int ok=run_round(masks[fig],base,fig+1,nf);
        intentos++;
        if(ok)aciertos++;

        printf("        [Stats] Aciertos: %d/%d\n",aciertos,intentos);

        int next=(fig+1)%nf;
        if(next==0){
            printf("\n[GeoBoard] Vuelta %d completada! "
                   "Aciertos en esta vuelta: %d/%d\n\n",
                   round,aciertos,total_valid);
            round++;aciertos=0;intentos=0;
        }
        splash_next();
        fig=next;
    }

    close(gfd);
    free(masks);free(valid);
    for(int i=0;i<nf;i++)free(pgms[i]);
    free(pgms);
    return 0;
}
