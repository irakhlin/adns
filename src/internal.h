/**/

#ifndef ADNS_INTERNAL_H_INCLUDED
#define ADNS_INTERNAL_H_INCLUDED

#define PRINTFFORMAT(a,b) __attribute__((format(printf,a,b)))
typedef unsigned char byte;

#include <stdarg.h>
#include <assert.h>
#include <unistd.h>

#include <sys/time.h>

#include "adns.h"

/* Configuration and constants */

#define MAXSERVERS 5
#define UDPMAXRETRIES /*15*/5
#define UDPRETRYMS 2000
#define TCPMS 30000
#define LOCALRESOURCEMS 20

#define DNS_PORT 53
#define DNS_MAXUDP 512
#define DNS_MAXDOMAIN 255
#define DNS_HDRSIZE 12
#define DNS_CLASS_IN 1

typedef enum {
  rcode_noerror,
  rcode_formaterror,
  rcode_servfail,
  rcode_nxdomain,
  rcode_notimp,
  rcode_refused
} dns_rcode;

/* Shared data structures */

typedef union {
  adns_status status;
  char *cp;
  adns_rrtype type;
  int i;
  struct in_addr ia;
  unsigned long ul;
} rr_align;

typedef struct {
  int used, avail;
  byte *buf;
} vbuf;

typedef union {
  void *ext;
  int dmaddr_index;
} qcontext;

typedef struct {
  adns_rrtype type;
  int rrsz;
  adns_status (*get_fn)(adns_state ads, adns_query qu, int serv,
			const byte *dgram, int dglen,
			int *cbyte_io, int max,
			int nsstart, int arcount, int *arstart_io,
			int roff, int *rcount_io);
} typeinfo;

struct adns__query {
  /* FIXME: make sure this is all init'd properly */
  enum { query_udp, query_tcpwait, query_tcpsent, query_child, query_done } state;
  adns_query back, next, parent;
  struct { adns_query head, tail; } children;
  struct { adns_query back, next; } siblings;
  
  const typeinfo *typei;
  vbuf ans;
  int id, flags, udpretries;
  int udpnextserver;
  unsigned long udpsent, tcpfailed; /* bitmap indexed by server */
  struct timeval timeout;
  byte *querymsg;
  int querylen, cnameoff, rrsoff;
  qcontext context;
  char owner[1];
  /* After the owner name and nul comes the query message, pointed to by querymsg */

  /* Possible states:
   *
   *  state   Queue   child  id   answer    nextudpserver  sentudp     failedtcp
   *
   *  udp     NONE    null   >=0  null      0              zero        zero
   *  udp     timew   null   >=0  null      any            nonzero     zero
   *  udp     NONE    null   >=0  null      any            nonzero     zero
   *
   *  tcpwait timew   null   >=0  null      irrelevant     zero        any
   *  tcpsent timew   null   >=0  null      irrelevant     zero        any
   *
   *  child   childw  set    >=0  partial   irrelevant     irrelevant  irrelevant
   *  done    output  null   -1   set/null  irrelevant     irrelevant  irrelevant
   *
   *			      +------------------------+
   *             START -----> |      udp/NONE          |
   *			      +------------------------+
   *                         /                       |\  \
   *        too big for UDP /             UDP timeout  \  \ send via UDP
   *        do this ASAP!  /              more retries  \  \   do this ASAP!
   *                     |_                  desired     \  _|
   *		  +---------------+     	    	+-----------+
   *              | tcpwait/timew | ____                | udp/timew |
   *              +---------------+     \	    	+-----------+
   *                    |  ^             |                 | |
   *     TCP conn'd;    |  | TCP died    |                 | |
   *     send via TCP   |  | more        |     UDP timeout | |
   *     do this ASAP!  |  | servers     |      no more    | |
   *                    v  | to try      |      retries    | |
   *              +---------------+      |      desired    | |
   *              | tcpsent/timew | ____ |                 | |
   *    	  +---------------+     \|                 | |
   *                  \   \ TCP died     | TCP             | |
   *                   \   \ no more     | timeout         / |
   *                    \   \ servers    |                /  |
   *                     \   \ to try    |               /   |
   *                  got \   \          v             |_    / got
   *                 reply \   _| +------------------+      / reply
   *   	       	       	    \  	  | done/output FAIL |     /
   *                         \    +------------------+    /
   *                          \                          /
   *                           _|                      |_
   *                             (..... got reply ....)
   *                              /                   \
   *        need child query/ies /                     \ no child query
   *                            /                       \
   *                          |_                         _|
   *		    +--------------+		       +----------------+
   *                | child/childw | ----------------> | done/output OK |
   *                +--------------+  children done    +----------------+
   */
};

struct adns__state {
  adns_initflags iflags;
  FILE *diagfile;
  struct { adns_query head, tail; } timew, childw, output;
  int nextid, udpsocket, tcpsocket;
  vbuf rqbuf, tcpsend, tcprecv;
  int nservers, tcpserver;
  enum adns__tcpstate { server_disconnected, server_connecting, server_ok } tcpstate;
  struct timeval tcptimeout;
  struct server {
    struct in_addr addr;
  } servers[MAXSERVERS];
};

/* From setup.c: */

void adns__vdiag(adns_state ads, const char *pfx, adns_initflags prevent,
		 int serv, const char *fmt, va_list al);
void adns__debug(adns_state ads, int serv, const char *fmt, ...) PRINTFFORMAT(3,4);
void adns__warn(adns_state ads, int serv, const char *fmt, ...) PRINTFFORMAT(3,4);
void adns__diag(adns_state ads, int serv, const char *fmt, ...) PRINTFFORMAT(3,4);

int adns__vbuf_ensure(vbuf *vb, int want);
int adns__vbuf_append(vbuf *vb, const byte *data, int len);
int adns__vbuf_malloc(vbuf *vb, size_t len);
/* 1=>success, 0=>realloc failed */
void adns__vbuf_appendq(vbuf *vb, const byte *data, int len);
void adns__vbuf_init(vbuf *vb);

int adns__setnonblock(adns_state ads, int fd); /* => errno value */

/* From submit.c: */

void adns__query_nomem(adns_state ads, adns_query qu);
void adns__query_finish(adns_state ads, adns_query qu, adns_status stat);
void adns__query_fail(adns_state ads, adns_query qu, adns_status stat);

/* From query.c: */

void adns__query_udp(adns_state ads, adns_query qu, struct timeval now);
void adns__query_tcp(adns_state ads, adns_query qu, struct timeval now);
adns_status adns__mkquery(adns_state ads, const char *owner, int ol, int id,
			  const typeinfo *typei, adns_queryflags flags);

/* From reply.c: */

void adns__procdgram(adns_state ads, const byte *dgram, int len,
		     int serv, struct timeval now);

/* From types.c: */

const typeinfo *adns__findtype(adns_rrtype type);

/* From parse.c: */

int vbuf__append_quoted1035(vbuf *vb, const byte *buf, int len);

adns_status adns__get_label(const byte *dgram, int dglen, int *max_io,
			    int *cbyte_io, int *lablen_r, int *labstart_r,
			    int *namelen_io);
adns_status adns__get_domain_perm(adns_state ads, adns_query qu, int serv,
				  const byte *dgram, int dglen,
				  int *cbyte_io, int max, int *domainstart_r);
adns_status adns__get_domain_temp(adns_state ads, adns_query qu, int serv,
				  const byte *dgram, int dglen,
				  int *cbyte_io, int max, int *domainstart_r);
adns_status adns__get_rr_temp(adns_state ads, adns_query qu, int serv,
			      const byte *dgram, int dglen, int *cbyte_io,
			      int *type_r, int *class_r, int *rdlen_r, int *rdstart_r,
			      const byte *eo_dgram, int eo_dglen, int eo_cbyte,
			      int *eo_matched_r);

/* From event.c: */

void adns__tcp_broken(adns_state ads, const char *what, const char *why);
void adns__tcp_tryconnect(adns_state ads, struct timeval now);
void adns__autosys(adns_state ads, struct timeval now);

/* Useful static inline functions: */

static inline void timevaladd(struct timeval *tv_io, long ms) {
  struct timeval tmp;
  assert(ms>=0);
  tmp= *tv_io;
  tmp.tv_usec += (ms%1000)*1000000;
  tmp.tv_sec += ms/1000;
  if (tmp.tv_usec >= 1000000) { tmp.tv_sec++; tmp.tv_usec -= 1000; }
  *tv_io= tmp;
}

static inline int ctype_whitespace(int c) { return c==' ' || c=='\n' || c=='\t'; }
static inline int ctype_digit(int c) { return c>='0' && c<='9'; }
static inline int ctype_alpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' || c <= 'Z');
}

/* Useful macros */

#define LIST_INIT(list) ((list).head= (list).tail= 0)

#define LIST_UNLINK_PART(list,node,part) \
  do { \
    if ((node)->back) (node)->back->part next= (node)->part next; \
      else                        (list).head= (node)->part next; \
    if ((node)->next) (node)->next->part back= (node)->part back; \
      else                        (list).tail= (node)->part back; \
  } while(0)

#define LIST_LINK_TAIL_PART(list,node,part) \
  do { \
    (node)->part back= 0; \
    (node)->part next= (list).tail; \
    if ((list).tail) (list).tail->part back= (node); else (list).part head= (node); \
    (list).tail= (node); \
  } while(0)

#define LIST_UNLINK(list,node) LIST_UNLINK_PART(list,node,)
#define LIST_LINK_TAIL(list,node) LIST_LINK_TAIL_PART(list,node,)

#define GETIL_B(cb) (dgram[(cb)++])
#define GET_B(cb,tv) ((tv)= GETIL_B((cb)))
#define GET_W(cb,tv) ((tv)=0, (tv)|=(GETIL_B((cb))<<8), (tv)|=GETIL_B(cb), (tv))

#endif