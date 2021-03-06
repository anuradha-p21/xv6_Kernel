#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

//called in net_tx_eth , which gets called from net_tx_ip, which gets called from udp.
int
e1000_transmit(struct mbuf *m)
{
    // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //printf("hi this is transmit\n");
  // 1. First ask the E1000 for the TX ring index at which it's expecting the next packet, by reading the E1000_TDT control register.
  acquire(&e1000_lock);
  int pos = regs[E1000_TDT];
  /* 2. If E1000_TXD_STAT_DD is not set in the descriptor indexed by E1000_TDT, the E1000 hasn't finished the corresponding previous transmission request, so return an error. */
  if( (tx_ring[pos].status & E1000_TXD_STAT_DD) == 0) {  //not set
   release(&e1000_lock);
   return -1;
  }
  /* 3. Otherwise, use mbuffree() to free the last mbuf that was transmitted from that descriptor (if there was one). */
  struct mbuf *temp = tx_mbufs[pos];
  if(temp) mbuffree(temp);
  
  /* 4. Then fill in the descriptor(tx_ring[pos]). m->head points to the packet's content in memory, and m->len is the packet length. */
  //we are copying from mbuf to t FIFO
  tx_ring[pos].addr = (uint64) m->head;
  tx_ring[pos].length = (uint64) m->len;
  
  /*5. Set the necessary cmd flags (look at Section 3.3 in the E1000 manual)  */
  
  /*When set, indicates the last descriptor making up the packet. One or many
descriptors can be used to form a packet.*/
  tx_ring[pos].cmd |= E1000_TXD_CMD_EOP;
  /*Report Status
When set, the Ethernet controller needs to report the status information. This ability
may be used by software that does in-memory checks of the transmit descriptors to
determine which ones are done and packets have been buffered in the transmit
FIFO. Software does it by looking at the descriptor status byte and checking the
Descriptor Done (DD) bit.*/
  tx_ring[pos].cmd |= E1000_TXD_CMD_RS;
  
  /*6. and stash away a pointer to the mbuf for later freeing.*/
  tx_mbufs[pos] = m;
  
  /*7. Finally, update the ring position by adding one to E1000_TDT modulo TX_RING_SIZE. */
  regs[E1000_TDT] = (pos+1) % TX_RING_SIZE;
  release(&e1000_lock);
  
  return 0;
}

//trap -->e1000_itr --> e1000_recv -->net_rx --> net_rx_ip -->net_rx_udp -->sockrecvudp which wakes up sockread , 
//sockread checks the mbuf, if it is empty it sleeps until woken up by sockrecvudp. 
static void
e1000_recv(void)
{
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  /* 1.  First ask the E1000 for the ring index at which the next waiting received packet (if any) is located, by fetching the E1000_RDT control register and adding one modulo RX_RING_SIZE. */
  acquire(&e1000_lock);
  int cur = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    
 /* 2. Then check if a new packet is available by checking for the E1000_RXD_STAT_DD bit in the status portion of the descriptor. If not, stop.*/
 while((rx_ring[cur].status & E1000_RXD_STAT_DD ) !=0)
 { //DD is set, as long it is set, keep receiveing. if DD is 0 then end of reception. 
 
  /* 3. Otherwise, update the mbuf's m->len to the length reported in the descriptor. */
  mbufput( rx_mbufs[cur], rx_ring[cur].length);
   
 /* 4. Deliver the mbuf to the network stack using net_rx(). */
release(&e1000_lock);
 net_rx(rx_mbufs[cur]);
  
   
 /* 5. Then allocate a new mbuf using mbufalloc() to replace the one just given to net_rx().
 Program its data pointer (m->head) into the descriptor. Clear the descriptor's status bits to zero.*/
 acquire(&e1000_lock);
 rx_mbufs[cur] = mbufalloc(0);
 rx_ring[cur].addr = (uint64) rx_mbufs[cur]->head;
 rx_ring[cur].status = 0;
   
/* 6.  Finally, update the E1000_RDT register to be the index of the last ring descriptor processed.*/
regs[E1000_RDT] = cur;
cur = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
 }
   release(&e1000_lock);
}


void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
