#include "tinyos.h"
#include "kernel_proc.h"
#include "util.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_sys.h"
//#include "kernel_pipe.c"

//douleboun ola ta test gg


static file_ops socketops = {   // file ops otan einai unbound
	.Read = NULL,
	.Write = NULL,
	.Close = USocket_Close,
	.Open = NULL
}; 


static file_ops listenerops = { // file ops otan einai listener
	.Read = NULL,
	.Write = NULL,
	.Close = (void*)listener_Close,
	.Open = NULL
}; 

static file_ops peerops = { // file ops otan einai peer
	.Read = Socket_Read,
	.Write = Socket_Write,
	.Close = Peer_Close,
	.Open = NULL
};
socb* PORT_MAP[MAX_PORT+1] = {NULL};

Fid_t sys_Socket(port_t port)
{

	FCB* fcb;
	Fid_t fid;

	if(port<0 || port > MAX_PORT){
		return -1;
	}

	int result	= FCB_reserve(1, &fid, &fcb); // desmeusi enos fcb
	if (result == 0){
		return -1;
	}
	// arxikopoiiseis enos socket
	socb* scb = (socb*) xmalloc (sizeof(socb)); // dimiourgia socket control block
	scb->state = SOCKET_UNBOUND;
	scb->port = port;
	scb->ownerfcb = fcb;
	//scb->admitted = 0;
	scb->timeoutcv = COND_INIT;
	scb->refcount=1;

	//eimerosi fcb 

	fcb->streamfunc = &socketops;
	fcb->streamobj = scb;
	
	return fid;
}





int sys_Listen(Fid_t sock)
{
	FCB* Sfcb = get_fcb(sock);

	if(Sfcb == NULL || Sfcb->streamobj == NULL ) return -1;
	

	if (Sfcb->streamfunc != &socketops) return -1; // an einai diaforetiko apo unbound to state tou socket p theme na ginei listening



	socb* scb = (socb*) Sfcb->streamobj;

	if(scb->port == NOPORT) return -1; //an den exei port
		
	
	
	if(scb->state != SOCKET_UNBOUND) return -1; // an den einai unbound
		
	//if(PORT_MAP[scb->port]->state == SOCKET_LISTENER) return -1;

	if(PORT_MAP[scb->port] != NULL) return -1; //an uparxei allos listener mesa sto port
		
	
	//arxikopoiiseis
	scb->state = SOCKET_LISTENER;  //allagi tou state se listener
	scb->licb.req_available = COND_INIT;
	rlnode_init(&scb->licb.queue,NULL);	
	Sfcb->streamfunc = &listenerops;
	PORT_MAP[scb->port] = scb;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{

	if(lsock<0) return NOFILE;

	if(lsock>MAX_FILEID) return NOFILE;



	FCB* Sfcb = get_fcb(lsock);

   

	if(Sfcb == NULL ) return NOFILE;
	

	if (Sfcb->streamfunc != &listenerops) return NOFILE; // an einai diaforetiko apo listener to state tou socket p theme
	
	socb* listener = (socb*) Sfcb->streamobj;
	if( listener == NULL ) return -1;
	if (listener->state != SOCKET_LISTENER)	return NOFILE;



	port_t port = listener->port;

	if(PORT_MAP[port]!=listener) return NOFILE;


	listener->refcount++;


	while ((is_rlist_empty(&listener->licb.queue)) && (PORT_MAP[port] != NULL)){  //oso den uparxoun request stin lista kai uparxei listener sto port perimene.
		kernel_wait(&listener->licb.req_available,SCHED_SOCKET);
	}

	if (PORT_MAP[port] == NULL) return NOFILE; //an exei kanei close o listener
	

	rlnode* reqNode= rlist_pop_front(&listener->licb.queue); // pairnei to request apo tin lista
	rNode* request= reqNode->obj;
	
	socb* requestPeer=request->req_soc;
	Fid_t reqPeerID =request->fid;

	Fid_t peerID=sys_Socket(listener->port);
	if(peerID == NOFILE) return NOFILE;

	FCB* peerFCB = get_fcb(peerID);

	socb* peer=peerFCB->streamobj;  


	if(peer == NULL) return NOFILE;
	if(peer->state==SOCKET_PEER) return -1;
	peer->state=SOCKET_PEER;


	

	if(requestPeer == NULL) return NOFILE;

	

	if(peerFCB == NULL || peerFCB->streamobj == NULL) return NOFILE;

	
	if (peerFCB->streamfunc != &socketops){  //an to state tou neou socket den einai unbound
		return NOFILE;
	}
    

	
	//newsocb->admitted = 0;
	
	//dimiourgia kai arxikopoiisi ton 2 pipes pou theme gia na ginei h metafora pliroforion.
	pipe_cb* pipe1 = (pipe_cb*) xmalloc (sizeof(pipe_cb));
	pipe_cb* pipe2 = (pipe_cb*) xmalloc (sizeof(pipe_cb));

	pipe1->has_space = COND_INIT;
	pipe1->has_data = COND_INIT;
	//pipe1->BUFFER = (char*) xmalloc (PIPE_BUFFER_SIZE);
	pipe1->r_position = 0; 
	pipe1->w_position= 0;
	pipe1->current_pipe_bytes = 0;
	pipe1->r= peerFCB;
	pipe1->w= Sfcb; 

	pipe2->has_space = COND_INIT;
	pipe2->has_data = COND_INIT;
	//pipe2->BUFFER = (char*) xmalloc (sizeof(char)*PIPE_BUFFER_SIZE);
	pipe2->r_position = 0; 
	pipe2->w_position= 0;
	pipe2->current_pipe_bytes = 0;
	pipe2->r= Sfcb;
	pipe2->w= peerFCB; 



	peerFCB->streamfunc = &peerops;
	requestPeer->ownerfcb->streamfunc = &peerops;

	//enosi ton 2 sockets me deta 2 pipes
	peer->peecb.readpipe = pipe1;
	peer->peecb.writepipe = pipe2;

	request->req_soc->peecb.readpipe= pipe2;
	request->req_soc->peecb.writepipe = pipe1;

	//tora pou exei oloklirothei h sundesi to connecting socket enimeronetai oti einai apodexto
	request->admitted=1;

	kernel_broadcast(&listener->licb.req_available); //ksipnima tou listener
	request->admitted=1;
	listener->refcount--;
	if(listener->refcount ==0) free(listener);

	//printf("\n\n\n\n\n\n\n\n\n%d\n\n\n\n\n\n\n\n\n\n",reqPeerID);
	return peerID;
	//return peerID;
}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	
	
	FCB* Sfcb = get_fcb(sock);

	if (port<=NOPORT || port >=MAX_PORT) return -1;

	
	if(Sfcb == NULL ) return -1;
	
	//if(Sfcb->streamobj == NULL ) return -1;
	if (Sfcb->streamfunc != &socketops) return -1;
	

	socb* scb = (socb*) Sfcb->streamobj;

	socb* listener = PORT_MAP[port];


	if (listener == NULL) return -1;
	
	if (scb->state != SOCKET_UNBOUND)	return -1;

	if (listener->state != SOCKET_LISTENER) return -1;
	
	rNode* r_node = (rNode*)xmalloc(sizeof(rNode));
	r_node->req_soc=scb;
	r_node->fid=sock;
	rlnode_init(&r_node->node, r_node);
	r_node->admitted=0;
	r_node->cv=COND_INIT;
	rlist_push_back(&(PORT_MAP[port]->licb.queue), &r_node->node);
	kernel_broadcast(&(PORT_MAP[port]->licb.req_available));
	//printf("wololo\nwololo\nwololo\nwololo\nwololo\nwololo\nwololo\nwololo\n");
	int timeOut;

	while (r_node->admitted==0){
		timeOut = kernel_timedwait(&r_node->cv, SCHED_PIPE, timeout*1000);
			if(!timeOut) break ;
		

	}
	if(r_node->admitted==0){
	if (timeOut==0) return -1;
		}

/*
	//dimiourgia enos request kai eisagogi tou stin lista me ta request
	rlnode_init(&scb->licb.queue, scb);
	rlist_push_back(&listener->licb.queue, &scb->licb.queue);
	kernel_broadcast(&listener->licb.req_available); // ksipnima listener
	kernel_timedwait(&scb->timeoutcv, SCHED_SOCKET, timeout); //anamoni me xroniki diarkeia timeout

	if (scb->admitted == 0) return -1;
*/

//  r_node = NULL;

	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode mode)
{
	FCB* Sfcb = get_fcb(sock);

	if(Sfcb == NULL || Sfcb->streamobj == NULL ) return -1;
	

	if (Sfcb->streamfunc != &peerops) return 1;  //elegxos an einai peer
	

	socb* scb = (socb*) Sfcb->streamobj;




	if(mode>3 || mode<1 ) return -1;

	if(mode == SHUTDOWN_READ){ //thelei na kleisei ton reader
		int result = pipe_reader_close(scb->peecb.readpipe);  
		//printf("\n\n\n\n%mpaino se case shudown read\n\n\n\n\n");
		if (result == -1) {	
			return -1;
		}
		scb->peecb.readpipe = NULL;	
		return 0;
		
	
	}

	if(mode == SHUTDOWN_WRITE){ //thelei na kleisei ton writer
		int result = pipe_writer_close(scb->peecb.writepipe);
		if (result == -1) {	
			return -1;
		}
		scb->peecb.writepipe =  NULL;	
		return 0;
	}

	if(mode == SHUTDOWN_BOTH){ //thelei na kleisei kai ton reader kai ton writer
		int result1 = pipe_reader_close(scb->peecb.readpipe);
		int result2 = pipe_writer_close(scb->peecb.writepipe);
		if (result1 == -1 || result2 == -1) {	
			return -1;
		}
		scb->peecb.readpipe = NULL;
		scb->peecb.writepipe =  NULL;	
		return 0;
	}
	return -1;
}




int USocket_Close(void* this){

	if (this == NULL){
		return -1;
	}

	socb* scb = (socb*) this;
	scb->refcount--;
	if(scb->refcount==0){
		free(scb);
	}
	//free(scb); //apeleutherosi tou socket
	return 0;
}



int listener_Close(void* this){

	if (this == NULL){
		return -1;
	}

	socb* scb = (socb*) this;
	
	PORT_MAP[scb->port] = NULL;  //enimerosi tou port se NULL 
	kernel_broadcast(&scb->licb.req_available);  //ksipnima tou listener
	scb->refcount--;
	if(scb->refcount==0){
		free(scb);
	}
	//free(scb); //apeleutherosi tou socket
	return 0;
}


int Peer_Close(void* this){

	if (this == NULL){
		return -1;
	}

	socb* scb = (socb*) this;

	if(scb->peecb.readpipe != NULL){
		pipe_reader_close(scb->peecb.readpipe); //klisimo tou reader tou pipe
	}

	if(scb->peecb.writepipe != NULL){
		pipe_writer_close(scb->peecb.writepipe); //klisimo tou writer tou pipe
	}
	scb->refcount--;
	if(scb->refcount==0){
		free(scb);
	}

	//free(scb); //apeleutherosi tou socket
	return 0;
} 


int Socket_Read(void* p, char *buf, unsigned int size){


	socb* scb = (socb*)p;


	if(scb->peecb.readpipe == NULL){

		return -1;
	}
	
	return pipe_read(scb->peecb.readpipe,buf,size); 
	
}

int Socket_Write(void* p, const char *buf, unsigned int size){
	socb* scb = (socb*)p;
	if(scb->peecb.writepipe == NULL){
		return -1;
	}


	return pipe_write(scb->peecb.writepipe,buf,size);
	
}
