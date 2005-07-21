/* rt-firewire/include/highlevel.h
 *
 * Interface of highlevel application socket module. 
 *
 * Copyright (C)  2005 Zhang Yuchen <y.zhang-4@student.utwente.nl>
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
 * @ingroup highlevel
 * @file
 * 
 * data structure and interfaces of @ref highlevel "highlevel driver management module"
 */
 
#ifndef IEEE1394_HIGHLEVEL_H
#define IEEE1394_HIGHLEVEL_H

#include <linux/list.h>
#include <iso.h>

#ifdef __IN_RTFW__
#include <ieee1394_chrdev.h>
#endif

extern struct list_head hl_drivers;
	
struct hl_host_info {
	struct list_head list;
	struct hpsb_host *host; 
	size_t size;
	unsigned long key;
	void *data;
};

struct hpsb_address_serve {
        struct list_head host_list; /* per host list */

        struct list_head hl_list; /* hpsb_highlevel list */

        struct hpsb_address_ops *op;

	void *host;

        /*! first address handled and first address behind, quadlet aligned */
        u64 start, end;
};

/**
 * The above structs are internal to highlevel driver handling.  Only the
 * following structures are of interest to actual highlevel drivers.
 */

struct hpsb_highlevel {
	struct module *owner;
	const char *name;

        /*! Any of the following pointers can legally be NULL, except for
         * iso_receive which can only be NULL when you don't request
         * channels. */

        /*! New host initialized.  Will also be called during
         * hpsb_register_highlevel for all hosts already installed. */
        void (*add_host) (struct hpsb_host *host);

        /*! Host about to be removed.  Will also be called during
         * hpsb_unregister_highlevel once for each host. */
        void (*remove_host)  (struct hpsb_host *host);

        /*! Host experienced bus reset with possible configuration changes.
	 * Note that this one may occur during interrupt/bottom half handling.
	 * You can not expect to be able to do stock hpsb_reads. */
        void (*host_reset) (struct hpsb_host *host);

        /*! An isochronous packet was received.  Channel contains the channel
         * number for your convenience, it is also contained in the included
         * packet header (first quadlet, CRCs are missing).  You may get called
         * for channel/host combinations you did not request. 
	*(Use of this routine is deprecated, due to the more convenient iso module)
	*/
        void (*iso_receive) (struct hpsb_host *host, int channel,
                             quadlet_t *data, size_t length);

        /*! A write request was received on either the FCP_COMMAND (direction =
         * 0) or the FCP_RESPONSE (direction = 1) register.  The cts arg
         * contains the cts field (first byte of data). */
        void (*fcp_request) (struct hpsb_host *host, int nodeid, int direction,
                             int cts, u8 *data, size_t length);

	/*! These are initialized by the subsystem when the
	 * hpsb_higlevel is registered. */
	struct list_head hl_list;
	struct list_head irq_list;
	struct list_head addr_list;

	struct list_head host_info_list;
	rwlock_t host_info_lock;

#ifdef __IN_RTFW__	
	struct ioctl_handler *hl_ioctl;
#endif
};

struct hpsb_address_ops {
        /*!
         * Null function pointers will make the respective operation complete 
         * with RCODE_TYPE_ERROR.  Makes for easy to implement read-only 
         * registers (just leave everything but read NULL).
         *
         * All functions shall return appropriate IEEE 1394 rcodes.
         */

        /*! These functions have to implement block reads for themselves. */
        /* These functions either return a response code
           or a negative number. In the first case a response will be generated; in the 
           later case, no response will be sent and the driver, that handled the request
           will send the response itself
        */
        int (*read) (struct hpsb_host *host, struct hpsb_packet *packet, void *data,
					unsigned int length);
        int (*write) (struct hpsb_host *host, struct hpsb_packet *packet, 
					unsigned int length);

        /*! Lock transactions: write results of ext_tcode operation into
         * *store. */
        int (*lock) (struct hpsb_host *host, struct hpsb_packet *packet, quadlet_t *data);
        int (*lock64) (struct hpsb_host *host, struct hpsb_packet *packet, octlet_t *data);
};


/*!
 * Register highlevel driver.  The name pointer has to stay valid at all times
 * because the string is not copied.
 */
void hpsb_register_highlevel(struct hpsb_highlevel *hl);
void hpsb_unregister_highlevel(struct hpsb_highlevel *hl);

/*!
 * Register handlers for host address spaces.  Start and end are 48 bit pointers
 * and have to be quadlet aligned (end points to the first address behind the
 * handled addresses.  This function can be called multiple times for a single
 * hpsb_highlevel to implement sparse register sets.  The requested region must
 * not overlap any previously allocated region, otherwise registering will fail.
 *
 * It returns true for successful allocation.  There is no unregister function,
 * all address spaces are deallocated together with the hpsb_highlevel.
 */
u64 hpsb_allocate_and_register_addrspace(struct hpsb_highlevel *hl,
					 struct hpsb_host *host,
					 struct hpsb_address_ops *ops,
					 u64 size, u64 alignment,
					 u64 start, u64 end);
int hpsb_register_addrspace(struct hpsb_highlevel *hl, struct hpsb_host*host,
                            struct hpsb_address_ops *ops, u64 start, u64 end);

int hpsb_unregister_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
                              u64 start);


/*!
 * Enable or disable receving a certain isochronous channel through the
 * iso_receive op.
 */
int hpsb_listen_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
                         unsigned int channel);
void hpsb_unlisten_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
			 unsigned int channel);

/*! Retrieve a hostinfo pointer bound to this driver/host */
void *hpsb_get_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host);

/*! Allocate a hostinfo pointer of data_size bound to this driver/host */
void *hpsb_create_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   size_t data_size);

/*! Free and remove the hostinfo pointer bound to this driver/host */
void hpsb_destroy_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host);

/*! Set an alternate lookup key for the hostinfo bound to this driver/host */
void hpsb_set_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host, unsigned long key);

/*! Retrieve the alternate lookup key for the hostinfo bound to this driver/host */
unsigned long hpsb_get_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host);

/*! Retrieve a hostinfo pointer bound to this driver using its alternate key */
void *hpsb_get_hostinfo_bykey(struct hpsb_highlevel *hl, unsigned long key);

/*! Set the hostinfo pointer to something useful. Usually follows a call to
 * hpsb_create_hostinfo, where the size is 0. */
int hpsb_set_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host, void *data);
	
//~ int get_nodeid(void *host);

//~ u64 get_guid(void *host);

//~ u16 get_maxpayload(void *host);

//~ int get_bcchannel(void *host);

//~ unsigned char get_sspd(void *host, int i, int j);


#ifdef __IN_RTFW__

void highlevel_add_host(struct hpsb_host *host);
void highlevel_remove_host(struct hpsb_host *host);
void highlevel_host_reset(struct hpsb_host *host);

/*! these functions are called to handle transactions. They are called, when
   a packet arrives. The flags argument contains the second word of the first header
   quadlet of the incoming packet (containing transaction label, retry code,
   transaction code and priority). These functions either return a response code
   or a negative number. In the first case a response will be generated; in the
   later case, no response will be sent and the driver, that handled the request
   will send the response itself.
*/
int highlevel_read(struct hpsb_host *host, struct hpsb_packet *packet, void *buffer, unsigned int length);
int highlevel_write(struct hpsb_host *host, struct hpsb_packet *packet, unsigned int length);
int highlevel_lock(struct hpsb_host *host, struct hpsb_packet *req, quadlet_t *store);
int highlevel_lock64(struct hpsb_host *host, struct hpsb_packet *req, octlet_t *store);

void highlevel_iso_receive(struct hpsb_host *host, void *data,
                           size_t length);
void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
                           void *data, size_t length);

#endif/*__IN_RTFW__*/

#endif /* IEEE1394_HIGHLEVEL_H */
