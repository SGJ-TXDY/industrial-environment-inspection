#include "nmea.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
static int hex(char c) { if(c>='0'&&c<='9')return c-'0'; c=(char)toupper((unsigned char)c); return c>='A'&&c<='F'?c-'A'+10:-1; }
static int num(const char *s,double *v) {
    char *e; if(!*s)return 0; *v=strtod(s,&e); return !*e&&isfinite(*v);
}
static int coordinate(const char *s,const char *hem,int lat,double *v) {
    double raw,deg,min; if(!num(s,&raw)||raw<0||strlen(hem)!=1)return 0;
    deg=floor(raw/100); min=raw-deg*100;
    if(min>=60||deg>(lat?90:180)||(deg==(lat?90:180)&&min!=0))return 0;
    if(lat ? (*hem!='N'&&*hem!='S') : (*hem!='E'&&*hem!='W'))return 0;
    *v=deg+min/60; if(*hem=='S'||*hem=='W')*v=-*v; return 1;
}
int nmea_parse(struct gps_data *g,const char *line,double now) {
    char buf[256],*f[32],*star,*q; unsigned int sum=0; int n=0,a,b; double lat,lon,v;
    struct gps_data next=*g; size_t len=strlen(line);
    if(len<9||len>=sizeof(buf)||line[0]!='$')goto bad;
    memcpy(buf,line,len+1); star=strchr(buf,'*');
    if(!star||strlen(star)<3||(a=hex(star[1]))<0||(b=hex(star[2]))<0)goto bad;
    for(q=star+3;*q;q++)if(*q!='\r'&&*q!='\n')goto bad;
    for(q=buf+1;q<star;q++)sum^=(unsigned char)*q;
    if(sum!=(unsigned int)(a*16+b))goto bad;
    *star=0; f[n++]=buf+1;
    for(q=buf+1;*q;q++)if(*q==','){*q=0;if(n==32)goto bad;f[n++]=q+1;}
    if(strlen(f[0])!=5)return 0;
    if(!strcmp(f[0]+2,"RMC")) {
        if(n<10||strlen(f[2])!=1||(f[2][0]!='A'&&f[2][0]!='V'))goto bad;
        next.rmc_seen=1; next.rmc_at=now; next.fix=f[2][0]=='A';
        if(next.fix) {
            if(!coordinate(f[3],f[4],1,&lat)||!coordinate(f[5],f[6],0,&lon))goto bad;
            next.latitude=lat;next.longitude=lon;
        }
        next.speed_kmh=NAN;
        if(*f[7]) { if(!num(f[7],&v)||v<0)goto bad;next.speed_kmh=v*1.852; }
        snprintf(next.utc,sizeof(next.utc),"%.15s",f[1]);snprintf(next.date,sizeof(next.date),"%.11s",f[9]);
    } else if(!strcmp(f[0]+2,"GGA")) {
        if(n<11||!num(f[6],&v)||v<0||v>8||floor(v)!=v)goto bad;
        next.quality=(int)v; next.gga_seen=1; next.gga_at=now;
        if(!num(f[7],&v)||v<0||v>99||floor(v)!=v)goto bad;
        next.satellites=(int)v;next.altitude=NAN;next.hdop=NAN;
        if(*f[8]) { if(!num(f[8],&v)||v<0)goto bad; next.hdop=v; }
        if(next.quality) {
            if(!coordinate(f[2],f[3],1,&lat)||!coordinate(f[4],f[5],0,&lon))goto bad;
            if(*f[9]) { if(!num(f[9],&v)||strcmp(f[10],"M"))goto bad; next.altitude=v; }
        }
    } else return 0;
    next.sentences++; *g=next;return 1;
bad:g->rejected++;return -1;
}
int gps_has_fix(const struct gps_data *g,double now) {
    return g->rmc_seen&&g->fix&&now-g->rmc_at<=3.0&&
        !(g->gga_seen&&now-g->gga_at<=3.0&&!g->quality);
}
