/*
 * dxlAPRS toolchain
 *
 * Copyright (C) Christian Rabler <oe5dxl@oevsv.at>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */


#define X2C_int32
#define X2C_index32
#ifndef X2C_H_
#include "X2C.h"
#endif
#define adsb2aprs_C_
#ifndef osi_H_
#include "osi.h"
#endif
#include <osic.h>
#ifndef mlib_H_
#include "mlib.h"
#endif
#ifndef tcp_H_
#include "tcp.h"
#endif
#ifndef udp_H_
#include "udp.h"
#endif
#ifndef aprsstr_H_
#include "aprsstr.h"
#endif
#ifndef tcpb_H_
#include "tcpb.h"
#endif

/* dump1090 tcp output to aprs beacon by OE5DXL */
#define adsb2aprs_TIMETOL 20
/* max seconds between dir/speed and pos */

#define adsb2aprs_PURGETIME 120
/* seconds keep context */

#define adsb2aprs_DEFAULTBEACONTIME 20

#define adsb2aprs_SYMBOL "/^"

#define adsb2aprs_KNOTS 1.852
/* nautic miles */

#define adsb2aprs_FEET 0.3048

typedef char CSV[100][21];

struct FLY;

typedef struct FLY * pFLY;


struct FLY {
   pFLY next;
   char hex[6];
   char name[21];
   float lat;
   float long0;
   float alt;
   float speed;
   float dir;
   unsigned long speedtime;
   unsigned long postime;
   unsigned long lasttime;
   unsigned long lastbeacon;
   char newpos;
};

static char url[1001];

static char port[1001];

static char reconn;

static char verb;

static char verb2;

static long fd;

static pFLY dbase;

static unsigned long btime;

static char mycall[10];

static char symbol[2];

static long udpsock;

static unsigned long ipnum;

static unsigned long toport;


static void Error(char text[], unsigned long text_len)
{
   X2C_PCOPY((void **)&text,text_len);
   osi_Werr(text, text_len);
   osi_WerrLn(" error abort", 13ul);
   X2C_ABORT();
   X2C_PFREE(text);
} /* end Error() */


static char GetNum(const char h[], unsigned long h_len, char eot,
                unsigned long * p, unsigned long * n)
{
   *n = 0UL;
   while ((unsigned char)h[*p]>='0' && (unsigned char)h[*p]<='9') {
      *n = ( *n*10UL+(unsigned long)(unsigned char)h[*p])-48UL;
      ++*p;
   }
   return h[*p]==eot;
} /* end GetNum() */


static unsigned long truncc(double r)
{
   if (r<=0.0) return 0UL;
   else if (r>=2.147483647E+9) return 2147483647UL;
   else return (unsigned long)X2C_TRUNCC(r,0UL,X2C_max_longcard);
   return 0;
} /* end truncc() */


static unsigned long truncr(float r)
{
   if (r<=0.0f) return 0UL;
   else if (r>=2.147483647E+9f) return 2147483647UL;
   else return (unsigned long)X2C_TRUNCC(r,0UL,X2C_max_longcard);
   return 0;
} /* end truncr() */


static long GetIp(char h[], unsigned long h_len, unsigned long * p,
                unsigned long * ip0, unsigned long * port0)
{
   unsigned long n;
   unsigned long i;
   char ok0;
   long GetIp_ret;
   X2C_PCOPY((void **)&h,h_len);
   *p = 0UL;
   h[h_len-1] = 0;
   *ip0 = 0UL;
   for (i = 0UL; i<=4UL; i++) {
      n = 0UL;
      ok0 = 0;
      while ((unsigned char)h[*p]>='0' && (unsigned char)h[*p]<='9') {
         ok0 = 1;
         n = (n*10UL+(unsigned long)(unsigned char)h[*p])-48UL;
         ++*p;
      }
      if (!ok0) {
         GetIp_ret = -1L;
         goto label;
      }
      if (i<3UL) {
         if (h[*p]!='.' || n>255UL) {
            GetIp_ret = -1L;
            goto label;
         }
         *ip0 =  *ip0*256UL+n;
      }
      else if (i==3UL) {
         *ip0 =  *ip0*256UL+n;
         if (h[*p]!=':' || n>255UL) {
            GetIp_ret = -1L;
            goto label;
         }
      }
      else if (n>65535UL) {
         GetIp_ret = -1L;
         goto label;
      }
      *port0 = n;
      ++*p;
   } /* end for */
   GetIp_ret = 0L;
   label:;
   X2C_PFREE(h);
   return GetIp_ret;
} /* end GetIp() */


static void Parms(void)
{
   char s[1001];
   unsigned long n;
   unsigned long m;
   reconn = 0;
   verb = 0;
   verb2 = 0;
   strncpy(url,"127.0.0.1",1001u);
   strncpy(port,"30003",1001u);
   mycall[0] = 0;
   btime = 20UL;
   strncpy(symbol,"/^",2u);
   for (;;) {
      osi_NextArg(s, 1001ul);
      if (s[0U]==0) break;
      if ((s[0U]=='-' && s[1U]) && s[2U]==0) {
         if (s[1U]=='t') {
            osi_NextArg(s, 1001ul); /* url */
            n = 0UL;
            while ((n<1000UL && s[n]) && s[n]!=':') {
               if (n<1000UL) url[n] = s[n];
               ++n;
            }
            if (n>1000UL) n = 1000UL;
            url[n] = 0;
            if (s[n]==':') {
               m = 0UL;
               ++n;
               while ((n<1000UL && s[n]) && m<1000UL) {
                  port[m] = s[n];
                  ++n;
                  ++m;
               }
               if (m>1000UL) m = 1000UL;
               port[m] = 0;
            }
         }
         else if (s[1U]=='k') reconn = 1;
         else if (s[1U]=='b') {
            osi_NextArg(s, 1001ul);
            n = 0UL;
            if (!GetNum(s, 1001ul, 0, &n, &btime)) Error("-b <s>", 7ul);
         }
         else if (s[1U]=='I') {
            osi_NextArg(mycall, 10ul);
            if (aprsstr_Length(mycall, 10ul)<3UL || aprsstr_Length(mycall,
                10ul)>9UL) Error("-I <callsign>", 14ul);
         }
         else if (s[1U]=='s') {
            osi_NextArg(symbol, 2ul);
            if (aprsstr_Length(symbol, 2ul)!=2UL || symbol[0U]=='-') {
               Error("-s <symbol>", 12ul);
            }
         }
         else if (s[1U]=='u') {
            osi_NextArg(s, 1001ul);
            n = 0UL;
            if (GetIp(s, 1001ul, &n, &ipnum, &toport)<0L) {
               Error("-u ip:port number", 18ul);
            }
            udpsock = openudp();
            if (udpsock<0L) Error("cannot open udp socket", 23ul);
         }
         else if (s[1U]=='v') verb = 1;
         else if (s[1U]=='V') {
            verb = 1;
            verb2 = 1;
         }
         else if (s[1U]=='h') {
            osi_WrStrLn("", 1ul);
            osi_WrStrLn("dump1090 basestation format tcp output to aprs objec\
t beacon", 61ul);
            osi_WrStrLn("", 1ul);
            osi_WrStrLn(" -b <seconds>        aprs minimum send intervall -b \
10 (20)", 60ul);
            osi_WrStrLn(" -h                  help", 26ul);
            osi_WrStrLn(" -I <mycall>         Sender of Object Callsign -I OE\
0AAA", 57ul);
            osi_WrStrLn(" -k                  keep tcp connection", 41ul);
            osi_WrStrLn(" -s <symbol>         aprs symbol (/^)", 38ul);
            osi_WrStrLn(" -t <url:port>       connect dump1090 tcp server (12\
7.0.0.1:30003)", 67ul);
            osi_WrStrLn(" -u <ip>:<port>      send AXUDP -u 127.0.0.1:9001 us\
e udpgate4 or aprsmap as receiver", 86ul);
            osi_WrStrLn(" -v                  verbous", 29ul);
            osi_WrStrLn("example: -t 127.0.0.1:30003 -I YOURCALL-11 -u 127.0.\
0.1:9002 -k -v", 67ul);
            osi_WrStrLn("before this start \"dump1090 --net\"", 35ul);
            osi_WrStrLn("", 1ul);
            X2C_ABORT();
         }
         else Error("-h", 3ul);
      }
      else Error("-h", 3ul);
   }
} /* end Parms() */


static void decodeline(const char line0[], unsigned long line_len, CSV csv0)
{
   unsigned long j;
   unsigned long w;
   unsigned long i;
   memset((char *)csv0,(char)0,2100UL);
   i = 0UL;
   j = 0UL;
   w = 0UL;
   while (i<=line_len-1 && (unsigned char)line0[i]>=' ') {
      if (line0[i]!=',') {
         if (w<=99UL && j<=20UL) {
            csv0[w][j] = line0[i];
            ++j;
         }
      }
      else {
         ++w;
         j = 0UL;
      }
      ++i;
   }
} /* end decodeline() */


static char num(unsigned long n)
{
   return (char)(n%10UL+48UL);
} /* end num() */


static unsigned long dao91(double x)
/* radix91(xx/1.1) of dddmm.mmxx */
{
   double a;
   a = fabs(x);
   return ((truncc((a-(double)(float)truncc(a))*6.E+5)%100UL)*20UL+11UL)
                /22UL;
} /* end dao91() */


static void sendaprs(char dao, unsigned long time0, char mycall0[],
                unsigned long mycall_len, char destcall[],
                unsigned long destcall_len, char via[],
                unsigned long via_len, char sym[], unsigned long sym_len,
                char obj[], unsigned long obj_len, double lat, double long0,
                double alt, double course, double speed, char comm[],
                unsigned long comm_len)
{
   char ds[201];
   char h[201];
   char b[201];
   char raw[361];
   long rp;
   unsigned long n;
   unsigned long i;
   float a;
   X2C_PCOPY((void **)&mycall0,mycall_len);
   X2C_PCOPY((void **)&destcall,destcall_len);
   X2C_PCOPY((void **)&via,via_len);
   X2C_PCOPY((void **)&sym,sym_len);
   X2C_PCOPY((void **)&obj,obj_len);
   X2C_PCOPY((void **)&comm,comm_len);
   b[0] = 0;
   aprsstr_Append(b, 201ul, mycall0, mycall_len);
   aprsstr_Append(b, 201ul, ">", 2ul);
   aprsstr_Append(b, 201ul, destcall, destcall_len);
   if (via[0UL]) {
      aprsstr_Append(b, 201ul, ",", 2ul);
      aprsstr_Append(b, 201ul, via, via_len);
   }
   aprsstr_Append(b, 201ul, ":;", 3ul);
   aprsstr_Assign(h, 201ul, obj, obj_len);
   aprsstr_Append(h, 201ul, "         ", 10ul);
   h[9U] = 0;
   aprsstr_Append(b, 201ul, h, 201ul);
   aprsstr_Append(b, 201ul, "*", 2ul);
   aprsstr_DateToStr(time0, ds, 201ul);
   ds[0U] = ds[11U];
   ds[1U] = ds[12U];
   ds[2U] = ds[14U];
   ds[3U] = ds[15U];
   ds[4U] = ds[17U];
   ds[5U] = ds[18U];
   ds[6U] = 0;
   aprsstr_Append(b, 201ul, ds, 201ul);
   aprsstr_Append(b, 201ul, "h", 2ul);
   i = aprsstr_Length(b, 201ul);
   a = (float)fabs(lat);
   n = truncr(a);
   b[i] = num(n/10UL);
   ++i;
   b[i] = num(n);
   ++i;
   n = truncr((a-(float)n)*6000.0f);
   b[i] = num(n/1000UL);
   ++i;
   b[i] = num(n/100UL);
   ++i;
   b[i] = '.';
   ++i;
   b[i] = num(n/10UL);
   ++i;
   b[i] = num(n);
   ++i;
   if (lat>=0.0) b[i] = 'N';
   else b[i] = 'S';
   ++i;
   b[i] = sym[0UL];
   ++i;
   a = (float)fabs(long0);
   n = truncr(a);
   b[i] = num(n/100UL);
   ++i;
   b[i] = num(n/10UL);
   ++i;
   b[i] = num(n);
   ++i;
   n = truncr((a-(float)n)*6000.0f);
   b[i] = num(n/1000UL);
   ++i;
   b[i] = num(n/100UL);
   ++i;
   b[i] = '.';
   ++i;
   b[i] = num(n/10UL);
   ++i;
   b[i] = num(n);
   ++i;
   if (lat>=0.0) b[i] = 'E';
   else b[i] = 'W';
   ++i;
   b[i] = sym[1UL];
   ++i;
   if (speed>0.5) {
      n = truncr((float)(course+1.5));
      b[i] = num(n/100UL);
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
      b[i] = '/';
      ++i;
      n = truncr((float)(speed*5.3995680345572E-1+0.5));
      b[i] = num(n/100UL);
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
   }
   if (alt>0.5) {
      b[i] = '/';
      ++i;
      b[i] = 'A';
      ++i;
      b[i] = '=';
      ++i;
      n = truncr((float)fabs(alt*3.2808398950131+0.5));
      if (alt>=0.0) b[i] = num(n/100000UL);
      else b[i] = '-';
      ++i;
      b[i] = num(n/10000UL);
      ++i;
      b[i] = num(n/1000UL);
      ++i;
      b[i] = num(n/100UL);
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
   }
   if (dao) {
      b[i] = '!';
      ++i;
      b[i] = 'w';
      ++i;
      b[i] = (char)(33UL+dao91(lat));
      ++i;
      b[i] = (char)(33UL+dao91(long0));
      ++i;
      b[i] = '!';
      ++i;
   }
   b[i] = 0;
   aprsstr_Append(b, 201ul, comm, comm_len);
   if (verb) osi_WrStrLn(b, 201ul);
   aprsstr_mon2raw(b, 201ul, raw, 361ul, &rp);
   rp = udpsend(udpsock, raw, rp, toport, ipnum);
   X2C_PFREE(mycall0);
   X2C_PFREE(destcall);
   X2C_PFREE(via);
   X2C_PFREE(sym);
   X2C_PFREE(obj);
   X2C_PFREE(comm);
} /* end sendaprs() */


static void aprs(const struct FLY f)
{
   char h[31];
   aprsstr_Assign(h, 31ul, f.name, 21ul);
   h[9U] = 0;
   while (aprsstr_Length(h, 31ul)<9UL) aprsstr_Append(h, 31ul, " ", 2ul);
   sendaprs(0, f.postime, mycall, 10ul, "APLFR1", 7ul, "", 1ul, "/^", 3ul, h,
                 31ul, (double)f.lat, (double)f.long0, (double)f.alt,
                (double)f.dir, (double)(f.speed*1.852f), "", 1ul);
} /* end aprs() */


static void store(const CSV csv0)
{
   pFLY f0;
   pFLY f1;
   pFLY f;
   unsigned long msg;
   unsigned long t;
   float oalt;
   float olong;
   float olat;
   t = osic_time();
   if ((((csv0[0U][0U]=='M' && csv0[0U][1U]=='S') && csv0[0U][2U]=='G')
                && aprsstr_StrToCard(csv0[1U], 21ul,
                &msg)) && ((msg==1UL || msg==3UL) || msg==4UL)) {
      f = dbase;
      f0 = 0;
      while (f && !aprsstr_StrCmp(f->hex, 6ul, csv0[4U], 21ul)) {
         f1 = f->next;
         if (f->lasttime+120UL<t) {
            if (f0==0) dbase = f1;
            else f0->next = f1;
            if (verb2) {
               osi_WrStr("purge ", 7ul);
               osi_WrStrLn(f->hex, 6ul);
            }
            osic_free((X2C_ADDRESS *) &f, sizeof(struct FLY));
         }
         else f0 = f;
         f = f1;
      }
      if (f==0) {
         osic_alloc((X2C_ADDRESS *) &f, sizeof(struct FLY));
         if (f==0) {
            osi_WerrLn("Out of Memory", 14ul);
            return;
         }
         memset((X2C_ADDRESS)f,(char)0,sizeof(struct FLY));
         f->next = dbase;
         dbase = f;
         aprsstr_Assign(f->hex, 6ul, csv0[4U], 21ul);
         if (verb2) {
            osi_WrStr("new ", 5ul);
            osi_WrStrLn(f->hex, 6ul);
         }
      }
      f->lasttime = t;
      if (msg==1UL) {
         if (verb2 && f->name[0U]==0) {
            osi_WrStr("found name ", 12ul);
            osi_WrStr(f->hex, 6ul);
            osi_WrStr(" ", 2ul);
            osi_WrStrLn(f->name, 21ul);
         }
         aprsstr_Assign(f->name, 21ul, csv0[10U], 21ul);
      }
      else if (msg==4UL) {
         if (((aprsstr_StrToFix(&f->speed, csv0[12U],
                21ul) && aprsstr_StrToFix(&f->dir, csv0[13U],
                21ul)) && f->dir>=0.0f) && f->dir<=360.0f) f->speedtime = t;
      }
      else if (msg==3UL) {
         if (((((((aprsstr_StrToFix(&oalt, csv0[11U],
                21ul) && aprsstr_StrToFix(&olat, csv0[14U],
                21ul)) && olat>(-90.0f)) && olat<90.0f)
                && aprsstr_StrToFix(&olong, csv0[15U],
                21ul)) && olong>(-180.0f)) && olong<180.0f)
                && (olong!=f->long0 || olat!=f->lat)) {
            f->postime = t;
            f->newpos = 1;
            f->lat = olat;
            f->long0 = olong;
            f->alt = oalt*0.3048f;
         }
      }
      if (f->lastbeacon>t) f->lastbeacon = t;
      if (((((((f->newpos && f->name[0U]) && f->postime+20UL>=t)
                && f->speedtime+20UL>=t) && f->speed>0.0f) && f->lat!=0.0f)
                && f->long0!=0.0f) && f->lastbeacon+btime<t) {
         aprs(*f);
         f->newpos = 0;
         f->lastbeacon = t;
      }
   }
} /* end store() */

static char ibuf[100];

static char line[100];

static unsigned long ip;

static unsigned long lp;

static CSV csv;


X2C_STACK_LIMIT(100000l)
extern int main(int argc, char **argv)
{
   X2C_BEGIN(&argc,argv,1,4000000l,8000000l);
   if (sizeof(CSV)!=2100) X2C_ASSERT(0);
   aprsstr_BEGIN();
   osi_BEGIN();
   Parms();
   fd = -1L;
   dbase = 0;
   fd = connecttob(url, port);
   lp = 0UL;
   for (;;) {
      if (fd>=0L) {
         if (readsockb(fd, (char *)ibuf, 100L)<0L) {
            /* connect lost */
            osic_Close(fd);
            fd = -1L;
         }
         else {
            for (ip = 0UL; ip<=99UL; ip++) {
               if ((unsigned char)ibuf[ip]<' ') {
                  if (lp<99UL) line[lp] = 0;
                  if (verb) osi_WrStrLn(line, 100ul);
                  decodeline(line, 100ul, csv);
                  store(csv);
                  lp = 0UL;
               }
               else if (lp<99UL) {
                  line[lp] = ibuf[ip];
                  ++lp;
               }
            } /* end for */
         }
      }
      else if (reconn) {
         osi_WerrLn("connection lost", 16ul);
         usleep(1000000UL);
         fd = connecttob(url, port);
      }
      else break;
   }
   X2C_EXIT();
   return 0;
}

X2C_MAIN_DEFINITION
