/**/

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/time.h>

#include "internal.h"

static adns_query allocquery(adns_state ads, const char *owner, int ol,
			     int id, const typeinfo *typei,
			     adns_queryflags flags, const qcontext *ctx) {
  /* Query message used is the one assembled in ads->rqbuf */
  adns_query qu;
  adns_answer *ans;

  qu= malloc(sizeof(*qu)+ol+1+ads->rqbuf.used); if (!qu) return 0;
  
  adns__vbuf_init(&qu->ans);
  if (!adns__vbuf_ensure(&qu->ans,sizeof(adns_answer))) { free(qu); return 0; }
  ans= (adns_answer*)qu->ans.buf;
  ans->status= adns_s_ok;
  ans->type= qu->typei->type;
  ans->nrrs= 0;
  ans->rrs.str= 0;
  
  qu->state= query_udp;
  qu->next= qu->back= qu->parent= 0;
  LIST_INIT(qu->children);
  qu->siblings.next= qu->siblings.back= 0;
  qu->typei= typei;
  qu->id= id;
  qu->flags= flags;
  qu->udpretries= 0;
  qu->udpnextserver= 0;
  qu->udpsent= qu->tcpfailed= 0;
  timerclear(&qu->timeout);
  memcpy(&qu->context,ctx,sizeof(qu->context));
  memcpy(qu->owner,owner,ol); qu->owner[ol]= 0;
  qu->querymsg= qu->owner+ol+1;
  memcpy(qu->owner+ol+1,ads->rqbuf.buf,ads->rqbuf.used);
  qu->querylen= ads->rqbuf.used;
  
  return qu;
}

static int failsubmit(adns_state ads, const qcontext *ctx, adns_query *query_r,
		      adns_queryflags flags, int id, adns_status stat) {
  adns_query qu;

  ads->rqbuf.used= 0;
  qu= allocquery(ads,0,0,id,0,flags,ctx); if (!qu) return errno;
  adns__query_fail(ads,qu,stat);
  *query_r= qu;
  return 0;
}

int adns_submit(adns_state ads,
		const char *owner,
		adns_rrtype type,
		adns_queryflags flags,
		void *context,
		adns_query *query_r) {
  adns_query qu;
  adns_status stat;
  int ol, id, r;
  qcontext ctx;
  struct timeval now;
  const typeinfo *typei;

  ctx.ext= context;
  id= ads->nextid++;

  r= gettimeofday(&now,0); if (r) return errno;

  typei= adns__findtype(type);
  if (!typei) return failsubmit(ads,context,query_r,flags,id,adns_s_notimplemented);

  ol= strlen(owner);
  if (ol<=1 || ol>DNS_MAXDOMAIN+1)
    return failsubmit(ads,context,query_r,flags,id,adns_s_invaliddomain);
  if (owner[ol-1]=='.' && owner[ol-2]!='\\') { flags &= ~adns_qf_search; ol--; }

  stat= adns__mkquery(ads,owner,ol,id,typei,flags);
  if (stat) return failsubmit(ads,context,query_r,flags,id,stat);
  
  qu= allocquery(ads,owner,ol,id,typei,flags,&ctx); if (!qu) return errno;
  adns__query_udp(ads,qu,now);
  adns__autosys(ads,now);

  *query_r= qu;
  return 0;
}

int adns_synchronous(adns_state ads,
		     const char *owner,
		     adns_rrtype type,
		     adns_queryflags flags,
		     adns_answer **answer_r) {
  adns_query qu;
  int r;
  
  r= adns_submit(ads,owner,type,flags,0,&qu);
  if (r) return r;

  do {
    r= adns_wait(ads,&qu,answer_r,0);
  } while (r==EINTR);
  if (r) adns_cancel(ads,qu);
  return r;
}

void adns_cancel(adns_state ads, adns_query query) {
  abort(); /* fixme */
}