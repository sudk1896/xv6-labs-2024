#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

#define PORTS (1<<16)

struct packet_queue{
  uint32 ip_src; // Source IP
  char* buf; // UDP payload including header
  int len;
  struct packet_queue* next;
};

struct packet_queue* udp_queue[(1<<16)];
struct packet_queue* head[(1<<16)];
int pkt_count[(1<<16)];
int udp_procs[(1<<16)];

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;
static struct spinlock udplock;
int chan;

void
netinit(void)
{
  initlock(&netlock, "netlock");
  initlock(&udplock, "udplock");
  memset(udp_queue, 0, sizeof(udp_queue));
  memset(head, 0, sizeof(head));
  memset(udp_procs, 0, sizeof(udp_procs));
  for(int i=0;i<PORTS;i++){ 
    pkt_count[i] = 0; 
  }
  chan = 0;
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int sport;
  argint(0, &sport);
  acquire(&udplock);
  udp_queue[sport] = 0;
  head[sport] = 0;
  pkt_count[sport] = 0;
  ++udp_procs[sport];
  release(&udplock);
  return 0;
}

int add_packet(int port, char* buf, uint32 ip_src, int len){ 
  if(pkt_count[port]==16){ 
    kfree((void*)buf);
    printf("Port %d packet queue is full, dropping pkt\n", port);
    return -1;
  }
  struct packet_queue* pq = (struct packet_queue*)kalloc();
  pq->buf = buf;
  pq->len = len;
  pq->next = 0;
  pq->ip_src = ip_src;
  ++pkt_count[port];
  if(udp_queue[port]==0){ 
    head[port] = pq;
    udp_queue[port] = pq;
  }
  else{
    udp_queue[port]->next = pq;
  }
  udp_queue[port] = pq;
  printf("Added packet to port %d Pck count %d \n", port, pkt_count[port]);
  return 0;
}

struct packet_queue* remove_packet(int port){
  if(head[port]==0 || pkt_count[port]==0){
    printf("Tried to remove a packet from empty Q\n");
    return 0;
  }
  else{
    struct packet_queue* pkt = head[port];
    struct packet_queue* nxt_pkt = pkt->next;
    --pkt_count[port];
    if(pkt_count[port]==0) udp_queue[port]=0;
    head[port] = nxt_pkt;
    return pkt;
  }
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  
  int dport, maxlen;
  uint64 src;
  uint64 sport;
  uint64 buf;
  argint(0, &dport);
  argint(4, &maxlen);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &buf); 
  
  acquire(&udplock);
  while(pkt_count[dport]==0){
    printf("Sleeping, no packets\n");
    sleep(&chan, &udplock);
  }
  
  printf("Processing a packet\n"); 
  struct packet_queue* pkt = remove_packet(dport);
  struct udp* udp = (struct udp*)pkt->buf;
  struct proc* p = myproc();
  //uint64 port = (uint64)udp->sport; 
  printf("Recv IP: %x\n",pkt->ip_src);
  int ip_bytes = copyout(p->pagetable, src, (char*)(&pkt->ip_src), sizeof(pkt->ip_src));
  printf("IP butes copied %d\n", ip_bytes);
  int port_bytes = copyout(p->pagetable, sport, (char*)(&udp->sport), sizeof(udp->sport));
  printf("Port num %d port bytes copied %d\n", udp->sport, port_bytes);
  char* udp_payload = (char*)(udp + 1);
  printf("UDP payload: %s\n", udp_payload);
  int payload_bytes = maxlen < (udp->ulen - sizeof(udp_payload)) ? maxlen : (udp->ulen - sizeof(udp_payload));
  printf("UDP size w header %d copy bytes %d\n", udp->ulen, payload_bytes);
  copyout(p->pagetable, buf, udp_payload, payload_bytes);
  release(&udplock); 
  kfree((void*)pkt);
  if(payload_bytes < 0 || port_bytes < 0 || ip_bytes < 0) return -1;
  return payload_bytes;
  
  return 0;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  int res = e1000_transmit(buf, total);
  //printf("Sent pkt %s\n", payload);
  return res;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  struct eth* eth = (struct eth*)buf;
  struct ip* ip = (struct ip*)(eth + 1);
  printf("protocol %d\n", ip->ip_p);  
  if(ip->ip_p==IPPROTO_UDP){
    struct udp* udp = (struct udp*)(ip + 1);
    uint16 dport = ntohs(udp->dport); 
    if(udp_procs[dport]>0){
      uint32 ip_src = ntohl(ip->ip_src);
      printf("Src IP %x\n", ip_src);
      udp->sport = ntohs(udp->sport);
      udp->ulen = ntohs(udp->ulen);
      acquire(&udplock);
      add_packet(dport, (char*)udp, ip_src, udp->ulen);
      wakeup(&chan);
      release(&udplock);
    }else{
      kfree((void*)buf);
      printf("No process listening on dport, %d\n", dport);
    }
  } else{
    printf("Not UDP packet, so dropping\n");
    kfree((void*)buf);
  }
  
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
