#define _DEFAULT_SOURCE
#include "ui.h"
#include "ui_display.h"
#include "lvgl.h"
#include "can_bus.h"
#include "services.h"
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile sig_atomic_t running=1;
static void stop(int sig){(void)sig;running=0;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static const char *env(const char*n,const char*d){const char*v=getenv(n);return v&&*v?v:d;}
static int serial_open(void){
    struct termios t;int fd=open(env("GPS_DEVICE","/dev/ttymxc3"),O_RDWR|O_NOCTTY|O_NONBLOCK);speed_t rate;
    if(fd<0)return -1;
    switch(atoi(env("GPS_BAUD","9600"))){case 4800:rate=B4800;break;case 9600:rate=B9600;break;case 19200:rate=B19200;break;case 38400:rate=B38400;break;case 57600:rate=B57600;break;case 115200:rate=B115200;break;default:close(fd);errno=EINVAL;return -1;}
    if(tcgetattr(fd,&t)){close(fd);return -1;}cfmakeraw(&t);
    t.c_cflag=(t.c_cflag&~(CSIZE|PARENB|CSTOPB|CRTSCTS))|CS8|CLOCAL|CREAD;
    t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;cfsetispeed(&t,rate);cfsetospeed(&t,rate);
    if(tcsetattr(fd,TCSANOW,&t)){close(fd);return -1;}return fd;
}
static void on_inspect(const struct inspect_data*q){services_publish_inspect(q->session,q->seq,q->result,q->confidence);}
static unsigned can_bitrate(void){
    char*e;unsigned long v=strtoul(env("CAN_BITRATE","500000"),&e,10);
    /* 0 keeps timing set elsewhere; FlexCAN classic CAN tops out at 1 Mbit/s. */
    if(*e||(v&&(v<10000||v>1000000))){fprintf(stderr,"CAN_BITRATE %s invalid, keeping interface timing\n",env("CAN_BITRATE",""));return 0;}
    return (unsigned)v;
}
static int sample_read(int*fd,const char*path,void*sample,size_t len){
    ssize_t n;int error;
    if(*fd<0){*fd=open(path,O_RDONLY|O_NONBLOCK);if(*fd<0)return errno;}
    n=read(*fd,sample,len);if(n==(ssize_t)len)return 0;
    error=n<0?errno:EIO;
    if(error==ENODEV||error==EBADF){close(*fd);*fd=-1;}
    return error;
}
int main(void){
    int lock=-1,temp=-1,accel=-1,gps=-1,force=1,rc=1,display=-1;unsigned actions;
    struct ui_data d={0},frozen={0},view;
    struct can_bus can={.fd=-1,.state=BUS_CLOSED};int can_was_open=-1;unsigned bitrate=can_bitrate();
    struct env_accel_sample a;struct env_temp_sample t;
    char line[256];size_t used=0;double next_temp=0,next_accel=0,next_draw=0,next_retry=0,last_temp=0,last_accel=0;
    lock=open("/var/run/sensor-dashboard.lock",O_CREAT|O_RDWR,0644);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fprintf(stderr,"Cannot lock dashboard (already running or /var/run unavailable)\n");goto done;}
    signal(SIGINT,stop);signal(SIGTERM,stop);
    /* LVGL on the framebuffer, GT9147 as pointer input. */
    if((display=ui_display_open()))goto done;
    /* Alarm (LED0 + buzzer) and SQLite threads; the display works without them. */
    if(services_start())fprintf(stderr,"Alarm/database threads unavailable\n");
    fprintf(stderr,"Dashboard: GPS %s @ %s; CAN %s @ %u; CTRL-C to exit\n",env("GPS_DEVICE","/dev/ttymxc3"),env("GPS_BAUD","9600"),env("CAN_DEVICE","can0"),bitrate);
    while(running){
        double now=seconds();d.now=now;
        if(now>=next_retry){if(gps<0){gps=serial_open();used=0;}ui_display_retry_touch();
            if(can.fd<0){int open_ok=can_bus_open(&can,env("CAN_DEVICE","can0"),bitrate)>=0;
                if(open_ok!=can_was_open)fprintf(stderr,"CAN %s: %s\n",env("CAN_DEVICE","can0"),open_ok?"OK":strerror(errno));can_was_open=open_ok;}
            next_retry=now+2;}
        if(can.fd>=0){can_bus_read(&can,&d.inspect,now,on_inspect);if(can.fd<0)can_was_open=-1;}
        d.can_state=can.state;d.inspect_now=now;services_status(&d.services);
        d.touch_ok=ui_display_touch_ok();d.gps_open=gps>=0;
        /* LM75BDP converts every ~100 ms; once a second is plenty and keeps
         * I2C2, shared with the touch controller, quiet. */
        if(now>=next_temp){int old=d.sensors.temp_error;d.sensors.temp_error=sample_read(&temp,ENV_TEMP_DEVICE,&t,sizeof t);
            if(!d.sensors.temp_error){d.sensors.temp=t;d.sensors.temp_valid=1;last_temp=now;services_publish_sensor(t.temperature_mc);}
            if(old!=d.sensors.temp_error)fprintf(stderr,"LM75: %s\n",d.sensors.temp_error?strerror(d.sensors.temp_error):"OK");next_temp=now+1.0;}
        if(now>=next_accel){int old=d.sensors.accel_error;d.sensors.accel_error=sample_read(&accel,ENV_ACCEL_DEVICE,&a,sizeof a);
            if(!d.sensors.accel_error){attitude_compute(a.x*.0039,a.y*.0039,a.z*.0039,&d.sensors.attitude);d.sensors.accel_valid=1;last_accel=now;}
            if(old!=d.sensors.accel_error)fprintf(stderr,"ADXL345: %s\n",d.sensors.accel_error?strerror(d.sensors.accel_error):"OK");next_accel=now+.05;}
        if(gps>=0){char bytes[512];ssize_t n;int batch;
            for(batch=0;batch<8;batch++){
                n=read(gps,bytes,sizeof bytes);if(n<=0){if(n<0&&errno!=EAGAIN&&errno!=EINTR){close(gps);gps=-1;}break;}
                for(ssize_t i=0;i<n;i++){
                    char c=bytes[i];if(c=='$'){used=0;line[used++]=c;}
                    else if(c=='\n'&&used){line[used]=0;nmea_parse(&d.gps,line,now);used=0;}
                    else if(used){if(used<sizeof(line)-1)line[used++]=c;else{used=0;d.gps.rejected++;}}
                }
            }
        }
        d.sensors.temp_age=d.sensors.temp_valid?now-last_temp:0;d.sensors.accel_age=d.sensors.accel_valid?now-last_accel:0;
        /* Buttons were handled by LVGL in the previous lv_timer_handler(). */
        actions=ui_lvgl_take_actions();
        if(actions&UI_ACTION_PAUSE){d.paused=!d.paused;if(d.paused)frozen=d;force=1;}
        if(actions&UI_ACTION_PAGE){d.page=!d.page;force=1;}
        if(actions&(UI_ACTION_UPLOAD_PAUSE|UI_ACTION_UPLOAD_RECONNECT)){upload_control(!!(actions&UI_ACTION_UPLOAD_PAUSE),!!(actions&UI_ACTION_UPLOAD_RECONNECT));force=1;}
        if(force||now>=next_draw){
            view=d.paused?frozen:d;view.paused=d.paused;view.page=d.page;view.touch_ok=d.touch_ok;
            /* Inspection results are production records: never freeze them. */
            view.inspect=d.inspect;view.can_state=d.can_state;view.inspect_now=d.inspect_now;view.services=d.services;
            ui_lvgl_update(&view);
            next_draw=now+.2;force=0;
        }
        lv_timer_handler();
        usleep(10000);
    }rc=0;
done:
    if(temp>=0)close(temp);if(accel>=0)close(accel);if(gps>=0)close(gps);can_bus_close(&can);services_stop();
    if(!display)ui_display_close();if(lock>=0)close(lock);
    return rc;
}
