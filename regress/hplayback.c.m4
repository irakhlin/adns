m4_dnl hplayback.c.m4
m4_dnl (part of complex test harness, not of the library)
m4_dnl - playback routines

m4_dnl  This file is part of adns, which is Copyright (C) 1997, 1998 Ian Jackson
m4_dnl  
m4_dnl  This program is free software; you can redistribute it and/or modify
m4_dnl  it under the terms of the GNU General Public License as published by
m4_dnl  the Free Software Foundation; either version 2, or (at your option)
m4_dnl  any later version.
m4_dnl  
m4_dnl  This program is distributed in the hope that it will be useful,
m4_dnl  but WITHOUT ANY WARRANTY; without even the implied warranty of
m4_dnl  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
m4_dnl  GNU General Public License for more details.
m4_dnl  
m4_dnl  You should have received a copy of the GNU General Public License
m4_dnl  along with this program; if not, write to the Free Software Foundation,
m4_dnl  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 

m4_include(hmacros.i4)

#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "harness.h"

static FILE *Tinputfile, *Treportfile;
static vbuf vb2;

static void Tensurereportfile(void) {
  const char *fdstr;
  int fd;

  Treportfile= stderr;
  fdstr= getenv("ADNS_TEST_REPORT_FD"); if (!fdstr) return;
  fd= atoi(fdstr);
  Treportfile= fdopen(fd,"a"); if (!Treportfile) Tfailed("fdopen ADNS_TEST_REPORT_FD");
}

static void Tensureinputfile(void) {
  const char *fdstr;
  int fd;

  Tinputfile= stdin;
  fdstr= getenv("ADNS_TEST_IN_FD"); if (!fdstr) return;
  fd= atoi(fdstr);
  Tinputfile= fdopen(fd,"r"); if (!Tinputfile) Tfailed("fdopen ADNS_TEST_IN_FD");
}

static void Psyntax(const char *where) {
  fprintf(stderr,"adns test harness: syntax error in test log input file: %s\n",where);
  exit(-1);
}

static void Pcheckinput(void) {
  if (ferror(Tinputfile)) Tfailed("read test log input file");
  if (feof(Tinputfile)) Psyntax("eof at syscall reply");
}

static void Parg(const char *argname) {
  int l;

  if (vb2.buf[vb2.used++] != hm_squote hm_squote) Psyntax("not a space before argument");
  l= strlen(argname);
  if (memcmp(vb2.buf+vb2.used,argname,l)) Psyntax("argument name wrong");
  vb2.used+= l;
  if (vb2.buf[vb2.used++] != hm_squote=hm_squote) Psyntax("not = after argument name");
}

static int Perrno(const char *stuff) {
  const struct Terrno *te;
  int r;
  char *ep;

  for (te= Terrnos; te->n && strcmp(te->n,stuff); te++);
  if (te->n) return te->v;
  r= strtoul(stuff+1,&ep,10);
  if (ep) Psyntax("errno value not recognised, not numeric");
  return r;
}

static struct timeval begin;

static void Ptimevalabs(struct timeval *tvr) {
  int store, chars;
  unsigned long sec, usec;
  struct timeval rv;
  
  if (vb2.buf[vb2.used]==hm_squote+hm_squote) {
    vb2.used++;
    rv= begin;
    store= 0;
  } else {
    rv.tv_sec= 0;
    rv.tv_usec= 0;
    store= 1;
  }
  chars= -1;
  sscanf(vb2.buf+vb2.used,"%lu.%lu%n",&sec,&usec,&chars);
  if (chars==-1) Psyntax("timeval syntax error");
  rv.tv_sec += sec;
  rv.tv_usec += usec;
  if (begin.tv_usec > 1000000) {
    rv.tv_sec++;
    rv.tv_usec -= 1000000;
  }
  *tvr= rv;
  if (store) begin= rv;

  vb2.used += chars;
}

static void Pfdset(fd_set *set, int max) {
  int r, c;
  char *ep;
  
  if (vb2.buf[vb2.used++] != hm_squote[hm_squote) Psyntax("fd set start not [");
  FD_ZERO(set);
  for (;;) {
    r= strtoul(vb2.buf+vb2.used,&ep,10);
    if (r>=max) Psyntax("fd set member > max");
    FD_SET(r,set);
    vb2.used= ep - (char*)vb2.buf;
    c= vb2.buf[vb2.used++];
    if (c == hm_squote]hm_squote) break;
    if (c != hm_squote,hm_squote) Psyntax("fd set separator not ,");
  }
}

static void Paddr(struct sockaddr *addr, int *lenr) {
  struct sockaddr_in *sa= (struct sockaddr_in*)addr;
  char *p, *ep;
  long ul;
  
  assert(*lenr >= sizeof(*sa));
  p= strchr(vb2.buf+vb2.used,':');
  if (!p) Psyntax("no port on address");
  *p++= 0;
  memset(sa,0,sizeof(*sa));
  sa->sin_family= AF_INET;
  if (!inet_aton(vb2.buf+vb2.used,&sa->sin_addr)) Psyntax("invalid address");
  ul= strtoul(p,&ep,10);
  if (*ep && *ep != hm_squote hm_squote) Psyntax("invalid port (bad syntax)");
  if (ul >= 65536) Psyntax("port too large");
  sa->sin_port= htons(ul);
  *lenr= sizeof(*sa);

  vb2.used= ep - (char*)vb2.buf;
}

static int Pbytes(byte *buf, int maxlen) {
  static const char hexdigits[]= "0123456789abcdef";

  int c, v, done;
  const char *pf;

  done= 0;
  for (;;) {
    c= getc(Tinputfile); Pcheckinput();
    if (c=='\n' || c==' ' || c=='\t') continue;
    if (c=='.') break;
    pf= strchr(hexdigits,c); if (!pf) Psyntax("invalid first hex digit");
    v= (pf-hexdigits)<<4;
    c= getc(Tinputfile); Pcheckinput();
    pf= strchr(hexdigits,c); if (!pf) Psyntax("invalid second hex digit");
    v |= (pf-hexdigits);
    if (maxlen<=0) Psyntax("buffer overflow in bytes");
    *buf++= v;
    maxlen--; done++;
  }
  for (;;) {
    c= getc(Tinputfile); Pcheckinput();
    if (c=='\n') return done;
  }
}
  
void Q_vb(void) {
  int r;

  Tensureinputfile();
  if (!adns__vbuf_ensure(&vb2,vb.used+2)) Tnomem();
  r= fread(vb2.buf,1,vb.used+2,Tinputfile);
  if (feof(Tinputfile)) {
    fprintf(stderr,"adns test harness: input ends prematurely; program did:\n %.*s\n",
           vb.used,vb.buf);
    exit(-1);
  }
  Pcheckinput();
  if (vb2.buf[0] != hm_squote hm_squote) Psyntax("not space before call");
  if (memcmp(vb.buf,vb2.buf+1,vb.used) ||
      vb2.buf[vb.used+1] != hm_squote\nhm_squote) {
    fprintf(stderr,
            "adns test harness: program did unexpected:\n %.*s\n"
            "was expecting:\n %.*s\n",
            vb.used,vb.buf, vb.used,vb2.buf+1);
    exit(1);
  }
}

m4_define(`hm_syscall', `
 hm_create_proto_h
int H$1(hm_args_massage($3,void)) {
 int r;
 m4_define(`hm_rv_fd',`char *ep;')
 m4_define(`hm_rv_any',`char *ep;')
 m4_define(`hm_rv_len',`')
 m4_define(`hm_rv_must',`')
 m4_define(`hm_rv_succfail',`')
 $2

 hm_create_hqcall_vars
 $3

 hm_create_hqcall_init($1)
 $3

 hm_create_hqcall_args
 Q$1(hm_args_massage($3));

 m4_define(`hm_r_offset',`m4_len(` $1=')')
 if (!adns__vbuf_ensure(&vb2,1000)) Tnomem();
 fgets(vb2.buf,vb2.avail,Tinputfile); Pcheckinput();

 Tensurereportfile();
 fprintf(Treportfile,"syscallr %s",vb2.buf);
 vb2.avail= strlen(vb2.buf);
 if (vb.avail<=0 || vb2.buf[--vb2.avail]!=hm_squote\nhm_squote)
  Psyntax("badly formed line");
 vb2.buf[vb2.avail]= 0;
 if (memcmp(vb2.buf," $1=",hm_r_offset)) Psyntax("syscall reply mismatch");

 if (vb2.buf[hm_r_offset] == hm_squoteEhm_squote) {
  errno= Perrno(vb2.buf+hm_r_offset);
  return -1;
 }

 m4_define(`hm_rv_succfail',`
  if (memcmp(vb2.buf+hm_r_offset,"OK",2)) Psyntax("success/fail not E* or OK");
  vb2.used= hm_r_offset+2;
  r= 0;
 ')
 m4_define(`hm_rv_len',`hm_rv_succfail')
 m4_define(`hm_rv_must',`hm_rv_succfail')
 m4_define(`hm_rv_any',`
  r= strtoul(vb2.buf+hm_r_offset,&ep,10);
  if (*ep && *ep!=hm_squote hm_squote) Psyntax("return value not E* or positive number");
  vb2.used= ep - (char*)vb2.buf;
 ')
 m4_define(`hm_rv_fd',`hm_rv_any')
 $2

 hm_create_nothing
 m4_define(`hm_arg_fdset_io',`Parg("$'`1"); Pfdset($'`1,$'`2);')
 m4_define(`hm_arg_addr_out',`Parg("$'`1"); Paddr($'`1,$'`2);')
 m4_define(`hm_arg_timeval_out_abs',`Parg("$'`1"); Ptimevalabs($'`1);')
 $3
 if (vb2.used != vb2.avail) Psyntax("junk at end of line");

 hm_create_nothing
 m4_define(`hm_arg_bytes_out',`r= Pbytes($'`2,$'`4);')
 $3

 return r;
}
')

m4_include(`hsyscalls.i4')
