/* rtfirewire/include/iso.h
 *
*  Interface and data structure of iso module
 *
 * Copyright (C)  2005 Zhang Yuchen <yuchen623@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/**
 * @ingroup iso
 * @file
 *
 * data structure and interfaces of iso module
 */

#ifndef IEEE1394_ISO_H
#define IEEE1394_ISO_H

#ifdef __KERNEL__


/*****************************************
* @defgroup iso
*
* high-level ISO interface

This API sends and receives isochronous packets on a large,
virtually-contiguous kernel memory buffer. The buffer may be mapped
into a user-space process for zero-copy transmission and reception.

There are no explicit boundaries between packets in the buffer. A
packet may be transmitted or received at any location. However,
low-level drivers may impose certain restrictions on alignment or
size of packets. (e.g. in OHCI no packet may cross a page boundary,
and packets should be quadlet-aligned)

Packet descriptor - the API maintains a ring buffer of these packet
descriptors in kernel memory (hpsb_iso.infos[]).  
*/

struct hpsb_iso;
/**
 * @ingroup iso
 * @struct hpsb_iso_packet_info
 */
struct hpsb_iso_packet_info {
	/*! address of data payload */
	unsigned char *buf;
	
	/* relative address to start of data buffer */
	__u32 offset;

	/*! length of the data payload, in bytes (not including the isochronous header) */
	__u16 len;

	/*! (recv only) the cycle number (mod 8000) on which the packet was received */
	__u16 cycle;

	/*! (recv only) channel on which the packet was received */
	__u8 channel;

	/*! 2-bit 'tag' and 4-bit 'sy' fields of the isochronous header */
	__u8 tag;
	__u8 sy;
};

enum hpsb_iso_type { HPSB_ISO_RECV = 0, HPSB_ISO_XMIT = 1 };

/*! The mode of the dma when receiving iso data. Must be supported by chip */
enum raw1394_iso_dma_recv_mode {
	HPSB_ISO_DMA_DEFAULT = -1,
	HPSB_ISO_DMA_OLD_ABI = 0,
	HPSB_ISO_DMA_BUFFERFILL = 1,
	HPSB_ISO_DMA_PACKET_PER_BUFFER = 2
};

/* N packets have been read out of the buffer, re-use the buffer space */
int  hpsb_iso_recv_release_packets(struct hpsb_iso *, unsigned int n_packets);

/* check for arrival of new packets immediately (even if irq_interval
   has not yet been reached) */
int hpsb_iso_recv_flush(struct hpsb_iso *iso);

/* returns # of packets ready to send or receive */
int hpsb_iso_n_ready(struct hpsb_iso *iso);

//~ struct hpsb_iso_packet_info *get_isopacketinfo(void *iso, int index);

#include <hosts.h>
#include <dma.h>
#include <rt1394_sys.h>

/**
 * @ingroup iso
 * @struct hpsb_iso
 * Isochronous transaction buffer 
 * including attributes setting for the iso
 * transaction&operaton on this buffer. 
 */
struct hpsb_iso {
	enum hpsb_iso_type type;

	/*! pointer to low-level driver and its private data */
	struct hpsb_host *host;
	void *hostdata;

	/*! a function to be called (from interrupt context) after
           outgoing packets have been sent, or incoming packets have
           arrived */
	void (*callback)(struct hpsb_iso *iso, void *arg);
	void *arg;

	/*! wait for buffer space */
	//wait_queue_head_t waitq;

	int speed; /*! IEEE1394_SPEED_100, 200, or 400 */
	int channel; /*! -1 if multichannel */
	unsigned int bandwidth;
	int dma_mode; /*! dma receive mode */


	/*! greatest # of packets between interrupts - controls
	   the maximum latency of the buffer */
	int irq_interval;

	/*! the buffer for packet data payloads */
	struct dma_region data_buf;

	/*!size of data_buf, in bytes (always a multiple of PAGE_SIZE) */
	unsigned int buf_size;

	/*! # of packets in the ringbuffer */
	unsigned int buf_packets;

	/*! protects packet cursors */
	rtos_spinlock_t lock;

	/*! the index of the next packet that will be produced
	   or consumed by the user */
	int first_packet;

	/*! the index of the next packet that will be transmitted
	   or received by the 1394 hardware */
	int pkt_dma;

	/*! how many packets, starting at first_packet:
	   (transmit) are ready to be filled with data or
	   (receive)  contain received data */
	int n_ready_packets;

	/*! how many times the buffer has overflowed or underflowed */
	atomic_t overflows;

	/*! private flags to track initialization progress */
#define HPSB_ISO_STARTED  (1<<2)     //everything has been running. 
	unsigned int flags;

	/*! # of packets left to prebuffer (xmit only) */
	int prebuffer;

	/*! starting cycle for DMA (xmit only) */
	int start_cycle;

	/*! cycle at which next packet will be transmitted,
	   -1 if not known */
	int xmit_cycle;

	/*! ringbuffer of packet descriptors in regular kernel memory
	 * XXX Keep this last, since we use over-allocated memory from
	 * this entry to fill this field. */
	struct hpsb_iso_packet_info *infos;
		
	unsigned int pri;
	
	unsigned char name[32];
};

/* functions available to high-level drivers (e.g. raw1394) */

/* allocate the buffer and DMA context */

struct hpsb_iso* hpsb_iso_xmit_init(struct hpsb_host *host,
				    unsigned int data_buf_size,
				    unsigned int buf_packets,
				    int channel,
				    int speed,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*, void *), void *arg, unsigned char *name, int pri);

/* note: if channel = -1, multi-channel receive is enabled */
struct hpsb_iso* hpsb_iso_recv_init(struct hpsb_host *host,
				    unsigned int data_buf_size,
				    unsigned int buf_packets,
				    int channel,
				    int dma_mode,
				    int irq_interval,
				    void (*callback)(struct hpsb_iso*, void*), void *arg, unsigned char *name, int pri);

/* multi-channel only */
int hpsb_iso_recv_listen_channel(struct hpsb_iso *iso, unsigned char channel);
int hpsb_iso_recv_unlisten_channel(struct hpsb_iso *iso, unsigned char channel);
int hpsb_iso_recv_set_channel_mask(struct hpsb_iso *iso, u64 mask);

/* start/stop DMA */
int hpsb_iso_xmit_start(struct hpsb_iso *iso, int start_on_cycle, int prebuffer);
int hpsb_iso_recv_start(struct hpsb_iso *iso, int start_on_cycle, int tag_mask, int sync);
void hpsb_iso_stop(struct hpsb_iso *iso);

/* deallocate buffer and DMA context */
void hpsb_iso_shutdown(struct hpsb_iso *iso);

/* queue a packet for transmission. 'offset' is relative to the beginning of the
   DMA buffer, where the packet's data payload should already have been placed */
int hpsb_iso_xmit_queue_packet(struct hpsb_iso *iso, u32 offset, u16 len, u8 tag, u8 sy);

/* wait until all queued packets have been transmitted to the bus */
int hpsb_iso_xmit_sync(struct hpsb_iso *iso);

/* the following are callbacks available to low-level drivers */

/* call after a packet has been transmitted to the bus (interrupt context is OK)
   'cycle' is the _exact_ cycle the packet was sent on
   'error' should be non-zero if some sort of error occurred when sending the packet
*/
void hpsb_iso_packet_sent(struct hpsb_iso *iso, int cycle, int error);

/* call after a packet has been received (interrupt context OK) */
void hpsb_iso_packet_received(struct hpsb_iso *iso, u32 offset, u16 len,
			      u16 cycle, u8 channel, u8 tag, u8 sy);

/* call to wake waiting processes after buffer space has opened up. */
void hpsb_iso_callback(struct hpsb_iso *iso);

#endif /* __KERNEL__ */
#endif /* IEEE1394_ISO_H */
