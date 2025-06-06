#ifndef __KERNEL_STREAMS_H
#define __KERNEL_STREAMS_H

#include "tinyos.h"
#include "kernel_dev.h"


#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
#include "stdio.h"

/**
	@file kernel_streams.h
	@brief Support for I/O streams.


	@defgroup streams Streams.
	@ingroup kernel
	@brief Support for I/O streams.

	The stream model of tinyos3 is similar to the Unix model.
	Streams are objects that are shared between processes.
	Streams are accessed by file IDs (similar to file descriptors
	in Unix).

	The streams of each process are held in the file table of the
	PCB of the process. The system calls generally use the API
	of this file to access FCBs: @ref get_fcb, @ref FCB_reserve
	and @ref FCB_unreserve.

	Streams are connected to devices by virtue of a @c file_operations
	object, which provides pointers to device-specific implementations
	for read, write and close.

	@{
*/



/** @brief The file control block.

	A file control block provides a uniform object to the
	system calls, and contains pointers to device-specific
	functions.
 */
typedef struct file_control_block
{
  uint refcount;  			/**< @brief Reference counter. */
  void* streamobj;			/**< @brief The stream object (e.g., a device) */
  file_ops* streamfunc;		/**< @brief The stream implementation methods */
  rlnode freelist_node;		/**< @brief Intrusive list node */
} FCB;


//PIPE STUFF


typedef struct pipe_control_block{
	FCB *r, *w;
	CondVar has_space;
	CondVar has_data;
	int w_position, r_position;
	char BUFFER[PIPE_BUFFER_SIZE];
	int current_pipe_bytes;
} pipe_cb;



int pipe_writer_close (void* picb);

int pipe_reader_close (void* picb);

int pipe_write(void* pipecb_t, const char *buf, unsigned int size);

int pipe_read(void* pipecb_t, char *buf, unsigned int size);

//SOCKETS STUFF


typedef enum socket_state{
	SOCKET_UNBOUND,
	SOCKET_PEER,
	SOCKET_LISTENER
} Socket_type;






int Socket_Read(void* p, char *buf, unsigned int size);

int Socket_Write(void* p, const char *buf, unsigned int size);

int USocket_Close(void* this);

int listener_Close(void* this);

int Peer_Close(void* this);

typedef struct connection_request{
	Fid_t fid;
	socb* req_soc;
	rlnode node;
	CondVar cv;
	int admitted;
}rNode;



typedef struct listener_control_block		
{
			rlnode queue;
			CondVar req_available;
}li_cb;

typedef struct socket_control_block socb;

typedef struct peer_control_block
{
	pipe_cb* readpipe;
	pipe_cb* writepipe;
	socb* peer;
}pee_cb;

typedef struct unbound_control_block
 {
    rlnode unbound_socket;
 }u_cb;


typedef struct socket_control_block{

	Socket_type state;
	port_t port;
	FCB* ownerfcb;
	
	CondVar timeoutcv;
	uint refcount;

	union
	{

		li_cb licb;
		pee_cb peecb;
		u_cb ucb;
    	

	};

}socb;


socb* PORT_MAP[MAX_PORT+1];




/** 
  @brief Initialization for files and streams.

  This function is called at kernel startup.
 */
void initialize_files();


/**
	@brief Increase the reference count of an fcb 

	@param fcb the fcb whose reference count will be increased
*/
void FCB_incref(FCB* fcb);


/**
	@brief Decrease the reference count of the fcb.

	If the reference count drops to 0, release the FCB, calling the 
	Close method and returning its return value.
	If the reference count is still >0, return 0. 

	@param fcb  the fcb whose reference count is decreased
	@returns if the reference count is still >0, return 0, else return the value returned by the
	     `Close()` operation
*/
int FCB_decref(FCB* fcb);


/** @brief Acquire a number of FCBs and corresponding fids.

   Given an array of fids and an array of pointers to FCBs  of
   size @ num, this function will check is available resources
   in the current process PCB and FCB are available, and if so
   it will fill the two arrays with the appropriate values.
   If not, the state is unchanged (but the array contents
   may have been overwritten).

   If these resources are not needed, the operation can be
   reversed by calling @ref FCB_unreserve.

   @param num the number of resources to reserve.
   @param fid array of size at least `num` of `Fid_t`.
   @param fcb array of size at least `num` of `FCB*`.
   @returns 1 for success and 0 for failure.
*/
int FCB_reserve(size_t num, Fid_t *fid, FCB** fcb);


/** @brief Release a number of FCBs and corresponding fids.

   Given an array of fids of size @ num, this function will 
   return the fids to the free pool of the current process and
   release the corresponding FCBs.

   This is the opposite of operation @ref FCB_reserve. 
   Note that this is very different from closing open fids.
   No I/O operation is performed by this function.

   This function does not check its arguments for correctness.
   Use only with arrays filled by a call to @ref FCB_reserve.

   @param num the number of resources to unreserve.
   @param fid array of size at least `num` of `Fid_t`.
   @param fcb array of size at least `num` of `FCB*`.
*/
void FCB_unreserve(size_t num, Fid_t *fid, FCB** fcb);


/** @brief Translate an fid to an FCB.

	This routine will return NULL if the fid is not legal.

	@param fid the file ID to translate to a pointer to FCB
	@returns a pointer to the corresponding FCB, or NULL.
 */
FCB* get_fcb(Fid_t fid);


/** @} */

#endif
