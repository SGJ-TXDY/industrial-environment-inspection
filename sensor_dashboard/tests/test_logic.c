#include "nmea.h"
#include "sensor_logic.h"
#include "lm75_decode.h"
#include "graphics.h"
#include "inspect.h"
#include "alarm_logic.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
static int frame(struct inspect_data*q,unsigned id,int ver,int result,int conf,unsigned long seq,double t){unsigned char b[8]={(unsigned char)ver,(unsigned char)result,(unsigned char)conf,0,(unsigned char)seq,(unsigned char)(seq>>8),(unsigned char)(seq>>16),(unsigned char)(seq>>24)};return inspect_parse(q,id,b,8,t);}
/* Version 2 frame: byte 3 is the STM32 boot session (heartbeat: byte 2). */
static int frame2(struct inspect_data*q,unsigned id,int session,int result,int conf,unsigned long seq,double t){unsigned char b[8]={2,(unsigned char)result,(unsigned char)conf,(unsigned char)session,(unsigned char)seq,(unsigned char)(seq>>8),(unsigned char)(seq>>16),(unsigned char)(seq>>24)};if(id==INSPECT_HEARTBEAT_ID){b[2]=(unsigned char)session;b[3]=0;}return inspect_parse(q,id,b,8,t);}
static int parse(struct gps_data*g,const char*body,double t){char line[256];unsigned char c=0;const char*p;for(p=body;*p;p++)c^=(unsigned char)*p;snprintf(line,sizeof line,"$%s*%02X\r\n",body,c);return nmea_parse(g,line,t);}
int main(void){
    struct gps_data g={0};struct attitude a;
    /* LM75B datasheet table: 11-bit two's complement, 0.125 C per LSB. */
    assert(env_lm75_decode(0x7f,0xe0)==127875&&env_lm75_decode(0x7d,0x00)==125000);
    assert(env_lm75_decode(0x19,0x20)==25125&&env_lm75_decode(0x00,0x20)==125&&env_lm75_decode(0x00,0x00)==0);
    assert(env_lm75_decode(0xff,0xe0)==-125&&env_lm75_decode(0xe7,0x00)==-25000&&env_lm75_decode(0xc9,0x20)==-54875);
    assert(env_lm75_decode(0xc9,0x00)==-55000&&env_lm75_decode(0x19,0x3f)==25125);   /* D4..D0 ignored */
    attitude_compute(0,0,1,&a);assert(a.valid&&a.roll_deg==0&&a.pitch_deg==0);
    attitude_compute(0,0,0,&a);assert(!a.valid);
    attitude_compute(0,1,0,&a);assert(a.valid&&fabs(a.roll_deg-90)<.001);
    assert(parse(&g,"GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,,,A",10)==1);
    assert(gps_has_fix(&g,11)&&fabs(g.latitude-48.1173)<.000001&&fabs(g.speed_kmh-41.4848)<.0001);
    assert(!gps_has_fix(&g,14));
    assert(parse(&g,"GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",10)==1&&g.satellites==8&&g.altitude==545.4);
    assert(nmea_parse(&g,"$GNRMC,123519,V,,,,,,,230394,,,N*00",11)==-1&&g.fix);
    assert(parse(&g,"GNRMC,123520,V,,,,,,,230394,,,N",11)==1&&!gps_has_fix(&g,11));
    assert(parse(&g,"GNRMC,123521,A,9001,N,01131,E,0,0,230394",12)==-1);
    assert(parse(&g,"GNRMC,123521,A,4807.038,S,01131.000,W,0,0,230394",12)==1&&g.latitude<0&&g.longitude<0);
    assert(parse(&g,"GNGGA,123521,,,,,0,00,,,,,,,",12)==1&&!gps_has_fix(&g,12));
    assert(parse(&g,"GNRMC,123521,A,4807.038,N,01131.000,E,nan,0,230394",13)==-1);
    assert(parse(&g,"GNRMC,123521,A,4807.038,N,01131.000,E,,0,230394",13)==1&&isnan(g.speed_kmh));
    {struct inspect_data q={0};unsigned char b[8]={1,0,97,0,1,0,0,0};
    assert(inspect_parse(&q,0x181,b,8,1)==0&&!q.rejected);
    assert(inspect_parse(&q,INSPECT_RESULT_ID,b,7,1)==-1&&q.rejected==1);
    assert(frame(&q,INSPECT_RESULT_ID,3,0,97,1,1)==-1&&frame(&q,INSPECT_RESULT_ID,1,2,97,1,1)==-1&&frame(&q,INSPECT_RESULT_ID,1,0,101,1,1)==-1&&q.rejected==4&&!q.result_seen);
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_NORMAL,97,500,1)==1&&q.normal_count==1&&q.missed==0&&q.confidence==97);
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_DEFECT,88,500,1.1)==0&&q.duplicates==1&&q.normal_count==1&&q.result==INSPECT_NORMAL);
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_DEFECT,INSPECT_CONF_UNKNOWN,503,1.2)==1&&q.defect_count==1&&q.missed==2&&q.confidence==-1);
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_NORMAL,90,1,2)==1&&q.restarts==1&&q.missed==2&&q.seq==1);
    assert(inspect_online(&q,4.9)&&!inspect_online(&q,5.1));
    assert(frame(&q,INSPECT_HEARTBEAT_ID,1,2,0,0x01020304,6)==1&&q.stm32_state==2&&q.stm32_total==0x01020304&&inspect_online(&q,8.9)&&!inspect_online(&q,9.1));
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_DEFECT,50,0xffffffffUL,7)==1&&q.missed==2&&q.restarts==2&&q.defect_count==2);}
    {struct inspect_data q={0};
    /* Reported case: last item seq 1, STM32 reboots, first new item is seq 1 again. */
    assert(frame2(&q,INSPECT_RESULT_ID,7,INSPECT_NORMAL,97,1,1)==1&&q.session==7&&q.seq==1);
    assert(frame2(&q,INSPECT_RESULT_ID,7,INSPECT_NORMAL,97,1,1.1)==0&&q.duplicates==1&&q.normal_count==1);
    assert(frame2(&q,INSPECT_RESULT_ID,9,INSPECT_DEFECT,80,1,2)==1&&q.session==9&&q.restarts==1&&q.missed==0&&q.defect_count==1&&q.duplicates==1);
    assert(frame2(&q,INSPECT_RESULT_ID,9,INSPECT_NORMAL,90,4,3)==1&&q.missed==2);
    /* New session first seen at seq 3: items 1 and 2 of it were missed. */
    assert(frame2(&q,INSPECT_RESULT_ID,10,INSPECT_NORMAL,90,3,4)==1&&q.restarts==2&&q.missed==4);
    /* Same session drawn again after a reboot: falls back to seq < last. */
    assert(frame2(&q,INSPECT_RESULT_ID,10,INSPECT_NORMAL,90,1,5)==1&&q.restarts==3&&q.missed==4);
    assert(frame2(&q,INSPECT_RESULT_ID,0,INSPECT_NORMAL,90,2,6)==-1&&q.rejected==1&&q.seq==1);
    /* Firmware downgrade to v1: session unknown, counted as a restart. */
    assert(frame(&q,INSPECT_RESULT_ID,1,INSPECT_NORMAL,90,1,7)==1&&q.session==INSPECT_SESSION_NONE&&q.restarts==4);
    assert(frame2(&q,INSPECT_HEARTBEAT_ID,12,1,0,0,8)==1&&q.stm32_session==12&&q.stm32_state==1);
    assert(q.normal_count+q.defect_count==6);}
    {struct alarm_config c;struct alarm_state st={0};alarm_config_default(&c);
    assert(!alarm_temperature(&st,&c,40000,1)&&alarm_outputs(&st,&c,1)==0);
    assert(alarm_temperature(&st,&c,40100,2)&&st.temp==ALARM_TEMP_HIGH);
    assert(alarm_outputs(&st,&c,2.1)==(ALARM_OUT_LED|ALARM_OUT_BUZZER)&&alarm_outputs(&st,&c,2.5)==ALARM_OUT_LED&&alarm_outputs(&st,&c,4.05)==(ALARM_OUT_LED|ALARM_OUT_BUZZER));
    assert(!alarm_temperature(&st,&c,39500,3)&&st.temp==ALARM_TEMP_HIGH);
    assert(alarm_temperature(&st,&c,39000,4)&&st.temp==ALARM_NONE&&alarm_outputs(&st,&c,4)==0);
    assert(alarm_temperature(&st,&c,-500,5)&&st.temp==ALARM_TEMP_LOW&&!alarm_temperature(&st,&c,500,6)&&alarm_temperature(&st,&c,1000,7)&&st.temp==ALARM_NONE);
    assert(alarm_temperature(&st,&c,-1000,8)&&alarm_temperature(&st,&c,45000,9)&&st.temp==ALARM_TEMP_HIGH);
    st.temp=ALARM_NONE;alarm_defect(&st,&c,10);
    assert(alarm_outputs(&st,&c,10.99)==(ALARM_OUT_LED|ALARM_OUT_BUZZER)&&alarm_outputs(&st,&c,11.01)==0);
    alarm_defect(&st,&c,10.8);assert(alarm_outputs(&st,&c,11.5)==(ALARM_OUT_LED|ALARM_OUT_BUZZER));
    c.buzzer=0;assert(alarm_outputs(&st,&c,11.5)==ALARM_OUT_LED);}
    puts("PASS: session+seq dedup (reboot reusing seq 1, missed in new session, repeated session, v1 fallback); alarm hysteresis/low/high/defect retrigger/buzzer pattern/buzzer off; CAN inspection version/result/confidence/DLC checks, duplicates, gaps, STM32 restart, heartbeat timeout; LM75 positive/negative/fraction decode, gravity tilt, NMEA checksum/empty fields/hemispheres/invalid fix/stale data");return 0;
}
