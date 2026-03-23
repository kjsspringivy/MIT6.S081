//
// packet buffer management
//

#define MBUF_SIZE              2048  // 一个mbuf最大物理容量
#define MBUF_DEFAULT_HEADROOM  128   // 数据报缓冲区默认预留的头部空间大小，给网络加载协议头使用

struct mbuf {
  struct mbuf  *next; // the next mbuf in the chain
  char         *head; // the current start position of the buffer
  unsigned int len;   // the length of the buffer
  char         buf[MBUF_SIZE]; // the backing store
};

char *mbufpull(struct mbuf *m, unsigned int len);  // 剥离协议头：将 head 指针向后移动 len 字节，返回head，失败返回0
char *mbufpush(struct mbuf *m, unsigned int len);  // 增加协议头：将 head 指针向前移动 len 字节，返回head，失败返回0
char *mbufput(struct mbuf *m, unsigned int len);   // 在数据包尾部追加新数据：head 指针不动，只把总长度增加 len，
char *mbuftrim(struct mbuf *m, unsigned int len);  // 将数据包尾部截断舍弃：head 指针不动，只把总长度减去 len

// The above functions manipulate the size and position of the buffer:
//            <- push            <- trim
//             -> pull            -> put
// [-headroom-][------buffer------][-tailroom-]
// |----------------MBUF_SIZE-----------------|
//
// These marcos automatically typecast and determine the size of header structs.
// In most situations you should use these instead of the raw ops above.
#define mbufpullhdr(mbuf, hdr) (typeof(hdr)*)mbufpull(mbuf, sizeof(hdr))
#define mbufpushhdr(mbuf, hdr) (typeof(hdr)*)mbufpush(mbuf, sizeof(hdr))
#define mbufputhdr(mbuf, hdr) (typeof(hdr)*)mbufput(mbuf, sizeof(hdr))
#define mbuftrimhdr(mbuf, hdr) (typeof(hdr)*)mbuftrim(mbuf, sizeof(hdr))

struct mbuf *mbufalloc(unsigned int headroom);  // 分配一个新的 mbuf，预留 headroom 字节的头部空间，失败返回0
void mbuffree(struct mbuf *m);  // 释放一个 mbuf

struct mbufq {
  struct mbuf *head;  // the first element in the queue
  struct mbuf *tail;  // the last element in the queue
};

// 网络资源缓冲队列mbufq的基本操作：初始化、入队、出队、检查是否为空 
void mbufq_pushtail(struct mbufq *q, struct mbuf *m);  // 将 mbuf m 加入队列 q 的尾部
struct mbuf *mbufq_pophead(struct mbufq *q);  // 从队列 q 的头部取出一个 mbuf
int mbufq_empty(struct mbufq *q);  // 检查队列 q 是否为空
void mbufq_init(struct mbufq *q);  // 初始化队列 q

//
// endianness support
//

static inline uint16 bswaps(uint16 val)
{
  return (((val & 0x00ffU) << 8) |
          ((val & 0xff00U) >> 8));
}

static inline uint32 bswapl(uint32 val)
{
  return (((val & 0x000000ffUL) << 24) |
          ((val & 0x0000ff00UL) << 8) |
          ((val & 0x00ff0000UL) >> 8) |
          ((val & 0xff000000UL) >> 24));
}

// Use these macros to convert network bytes to the native byte order.
// Note that Risc-V uses little endian while network order is big endian.
// 网络字节序（大端序）与主机字节序（小端序）转换
#define ntohs bswaps  // 16位整数-网络字节序 -> 主机字节序。
#define ntohl bswapl  // 32位整数-网络字节序 -> 主机字节序。
#define htons bswaps  // 16位整数-主机字节序 -> 网络字节序。
#define htonl bswapl  // 32位整数-主机字节序 -> 网络字节序。


//
// useful networking headers
//

#define ETHADDR_LEN 6  // MAC地址长度为6字节,48位

// an Ethernet packet header (start of the packet).
// 以太网帧头部结构体，包含目的MAC地址、源MAC地址和以太类型字段
struct eth {
  uint8  dhost[ETHADDR_LEN];  // 目的MAC地址
  uint8  shost[ETHADDR_LEN];  // 源MAC地址
  uint16 type;            // 以太类型字段，指示上层协议类型，如IP、ARP等
} __attribute__((packed));

// 以太类型字段值定义
#define ETHTYPE_IP  0x0800 // Internet protocol 
#define ETHTYPE_ARP 0x0806 // Address resolution protocol

// an IP packet header (comes after an Ethernet header).
// IP协议头部结构体
struct ip {
  uint8  ip_vhl; // version << 4 | header length >> 2 版本
  uint8  ip_tos; // type of service 服务类型
  uint16 ip_len; // total length 数据包总长度，包括IP头部和数据部分
  uint16 ip_id;  // identification 标识符，用于数据包分片和重组
  uint16 ip_off; // fragment offset field 分片偏移字段，用于数据包分片和重组
  uint8  ip_ttl; // time to live 生存时间，数据包在网络中可以经过的最大路由数
  uint8  ip_p;   // protocol 协议字段，指示上层协议类型，如TCP、UDP等
  uint16 ip_sum; // checksum 校验和，用于验证IP头部的完整性
  uint32 ip_src, ip_dst; // source and dest address 源IP地址和目的IP地址
};

#define IPPROTO_ICMP 1  // Control message protocol 控制消息协议
#define IPPROTO_TCP  6  // Transmission control protocol 传输控制协议
#define IPPROTO_UDP  17 // User datagram protocol 用户数据报协议

// 将4个8位的IP地址段组合成一个32位的IP地址
#define MAKE_IP_ADDR(a, b, c, d)           \
  (((uint32)a << 24) | ((uint32)b << 16) | \
   ((uint32)c << 8) | (uint32)d)

// a UDP packet header (comes after an IP header).
struct udp {
  uint16 sport; // source port
  uint16 dport; // destination port
  uint16 ulen;  // length, including udp header, not including IP header
  uint16 sum;   // checksum
};

// an ARP packet (comes after an Ethernet header).
struct arp {
  uint16 hrd; // format of hardware address
  uint16 pro; // format of protocol address
  uint8  hln; // length of hardware address
  uint8  pln; // length of protocol address
  uint16 op;  // operation

  char   sha[ETHADDR_LEN]; // sender hardware address
  uint32 sip;              // sender IP address
  char   tha[ETHADDR_LEN]; // target hardware address
  uint32 tip;              // target IP address
} __attribute__((packed));

#define ARP_HRD_ETHER 1 // Ethernet 

enum {
  ARP_OP_REQUEST = 1, // requests hw addr given protocol addr
  ARP_OP_REPLY = 2,   // replies a hw addr given protocol addr
};

// an DNS packet (comes after an UDP header).
struct dns {
  uint16 id;  // request ID

  uint8 rd: 1;  // recursion desired
  uint8 tc: 1;  // truncated
  uint8 aa: 1;  // authoritive
  uint8 opcode: 4; 
  uint8 qr: 1;  // query/response
  uint8 rcode: 4; // response code
  uint8 cd: 1;  // checking disabled
  uint8 ad: 1;  // authenticated data
  uint8 z:  1;  
  uint8 ra: 1;  // recursion available
  
  uint16 qdcount; // number of question entries
  uint16 ancount; // number of resource records in answer section
  uint16 nscount; // number of NS resource records in authority section
  uint16 arcount; // number of resource records in additional records
} __attribute__((packed));

struct dns_question {
  uint16 qtype;
  uint16 qclass;
} __attribute__((packed));
  
#define ARECORD (0x0001)
#define QCLASS  (0x0001)

struct dns_data {
  uint16 type;
  uint16 class;
  uint32 ttl;
  uint16 len;
} __attribute__((packed));
