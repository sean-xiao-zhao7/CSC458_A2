#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

enum {
	CSTATE_CLOSED,
	CSTATE_LISTEN,
	CSTATE_SYN_SENT,
	CSTATE_SYN_RCVD,
	CSTATE_ESTABLISHED,
	CSTATE_FIN_WAIT,
	CSTATE_LAST_ACK,
	CSTATE_CLOSING
};

/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    tcp_seq my_seq; // the next byte I'm going to send.

    tcp_seq peer_seq; // the next byte I'm expecting from the peer

    int peer_window_size;
    int my_window_size;

    int no_data;

    int wait_for;
    
} context_t;

#define MAX_WINDOW_SIZE 3072

typedef struct _stcp_packet
{
	STCPHeader header;
    char payload[STCP_MSS];
    size_t data_len;
} STCPPacket; 

static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/* client side */
int send_syn(mysocket_t sd, context_t *ctx);
int send_ack_to_syn_ack(mysocket_t sd, context_t *ctx, STCPPacket * p);
/* server side */
int send_syn_ack(mysocket_t sd, context_t *ctx, STCPPacket * p);

/* both sides */
int send_data_peer(mysocket_t sd, char *data, context_t *ctx, int size);
int send_ack_peer(mysocket_t sd, STCPPacket * peer_p, context_t *ctx);

int dispatch_peer(mysocket_t sd, STCPPacket *p);
int dispatch_app(mysocket_t sd, STCPPacket *p);

int send_fin(mysocket_t sd, context_t *ctx);
int send_fin_ack(mysocket_t sd, context_t *ctx, STCPPacket * peer_p);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{	
	context_t *ctx;
    ctx = (context_t *) calloc(1, sizeof(context_t));
    int has_error = 0;
    char * error;
    assert(ctx);
    
    generate_initial_seq_num(ctx);
    
    struct timespec * wait_time;
    struct timeval * tv;
    int times_sent = 0;
    unsigned int event = 0;
        
    /* 3 way handshake */
    if (is_active) {
		
    	//ctx->my_seq = ctx->initial_sequence_num;
    	
    	/* client sends SYN */
    	while(times_sent < 6) {
    		
    		// setup timer
    		tv = (struct timeval *) malloc (sizeof(struct timeval));
    		wait_time = (struct timespec *) malloc (sizeof(struct timespec));
    		
    		gettimeofday(tv, NULL);
    		wait_time->tv_sec = tv->tv_sec + 100;
    		wait_time->tv_nsec = 0;
    		
    		send_syn(sd, ctx);    	
    		// client waits for SYN-ACK
    		event = stcp_wait_for_event(sd, NETWORK_DATA, wait_time);
    		
    		if (event & NETWORK_DATA) {    			
	    		/* client sends ACK of SYN-ACK */
	    		STCPPacket *syn_ack_p = (STCPPacket *) malloc (sizeof(STCPPacket));
	    		stcp_network_recv(sd, syn_ack_p, sizeof(STCPPacket));
	    		break;
	    	} else if (event & TIMEOUT){
	    		times_sent++;
	    	}
    	}
    	
    	if (times_sent == 6) {
    		// report error to app
    		has_error = 1;
    		error = "Host unreachable\0";    	
    	}
    	
    } else if (!is_active) {
    	/* server waits for SYN */
    	event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
    	if (event & NETWORK_DATA) {

    		/* server sends SYN-ACK */
    		STCPPacket *syn_p = (STCPPacket *) malloc (sizeof(STCPPacket));
    		stcp_network_recv(sd, syn_p, sizeof(STCPPacket));
    		send_syn_ack(sd, ctx, syn_p);

    		// server set sequence # contexts
    		ctx->my_seq = ctx->initial_sequence_num;
    		ctx->peer_seq = syn_p->header.th_seq + 1;

    		/* server waits for ACK of SYN-ACK */
        	event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
        	if (event & NETWORK_DATA) {
        		STCPPacket *ack_p = (STCPPacket *) malloc (sizeof(STCPPacket));
        		stcp_network_recv(sd, ack_p, sizeof(STCPPacket));
        		/* server is ready to send data */

        		// server set sequence # contexts again
        		ctx->my_seq++; // shift one byte for the 1 byte ACK

        		/* this concludes the 3 way handshake */
        	}
    	}
    }
    
    
    if (has_error == 0) {
    	ctx->connection_state = CSTATE_ESTABLISHED;
    	stcp_unblock_application(sd);
    	ctx->done = 0;

	    /* main STCP loop */
	    control_loop(sd, ctx);
    } else {
    	has_error = 1;
    	stcp_app_send(sd, error, 18);
    }

    /* do any cleanup here */
    free(ctx);
}


/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    
    ctx->initial_sequence_num = rand() && 255;
#endif
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    int current = 0;

    STCPPacket *ack_p;
	STCPPacket *data_p;
	STCPPacket *fin_p;
	
	char *data;
	
	tcp_seq rcvd_seq = 0;
	
	ctx->no_data = 0;			
    
    while (!ctx->done)
    {
        unsigned int event;
        
		switch (ctx->wait_for) {
			case NETWORK_DATA:
				event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
				break;
			case APP_DATA:
				event = stcp_wait_for_event(sd, APP_DATA, NULL);
				break;
			default:
				event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
		}
	
        /* the application has requested that data be sent */
        if (event & APP_DATA)
        {            
        	//TODO int total = 0;
        	
        	data = (char *) malloc (STCP_MSS);
        	current = stcp_app_recv(sd, data, STCP_MSS);
        	
        	send_data_peer(sd, data, ctx, current);
        	//ctx->wait_for = NETWORK_DATA; /* block until ACK is received */					
			free(data);	
		
		/* the network has sent data */	
        } else if (event & NETWORK_DATA) {
        	
        	/* client receives the data */
            data_p = (STCPPacket *) malloc (sizeof(STCPPacket));
            current = stcp_network_recv(sd, data_p, sizeof(STCPPacket));
            ctx->peer_seq = rcvd_seq + data_p->data_len; // the last byte of the payload + 1, the next expected byte
            
            stcp_app_send(sd, data_p->payload, data_p->data_len); // sends to application
            printf("%s", data_p->payload);
            
            if (data_p->header.th_flags & TH_ACK) {
            	/* got the ack */
            	ctx->wait_for = ANY_EVENT;
            }
            //send_ack_peer(sd, data_p, ctx);
            //ctx->wait_for = ANY_EVENT;
                        
            /*
            if (data_p->header.th_flags & TH_FIN) {
                    printf("got a fin packet\n");
                    ctx->peer_seq = rcvd_seq + current; // the last byte of the payl oad + 1, the next expected byte
                    char *src = (char *) malloc (current);
                    memcpy(src, data_p->payload, current);
                    stcp_app_send(sd, src, current); // sends to application
                    send_ack_peer(sd, data_p, ctx);
                    ctx->wait_for = ANY_EVENT;
                    printf("received from peer: %s\n", src);
            } else if (data_p->header.th_flags & TH_ACK) {
                    printf("data packet acknowledged!\n");
                    ctx->wait_for = ANY_EVENT;
            } else {
                    ctx->peer_seq = rcvd_seq + current; // the last byte of the payload + 1, the next expected byte
                    char *src = (char *) malloc (current);
                    memcpy(src, data_p->payload, current);
                    stcp_app_send(sd, src, current); // sends to application
                    send_ack_peer(sd, data_p, ctx);
                    ctx->wait_for = ANY_EVENT;
                    printf("received from peer: %s\n", src);
                    

            }*/
        /* the application has requested that connection be terminated */
        } else if (event & APP_CLOSE_REQUESTED) {
        
        	// ctx->done = 1;
        	printf("close event\n");
        }
    }
}

/* data transfer */
int send_data_peer(mysocket_t sd, char *data, context_t *ctx, int size){
	assert(ctx);	
	
	STCPPacket * p = (STCPPacket *) malloc (sizeof(STCPPacket));	
	
	strncpy(p->payload, data, size);
	p->data_len = size;

	p->header.th_seq = ctx->my_seq + 1;
	ctx->my_seq += size;
	//TODO sp->header.th_win = MAX_WINDOW_SIZE;

	dispatch_peer(sd, p);

	return 0;
}
/*
 * Client sends ACK
 */
int send_ack_peer(mysocket_t sd, STCPPacket * peer_p, context_t *ctx){
        assert(ctx);

        STCPPacket * p = (STCPPacket *) malloc (sizeof(STCPPacket));

        p->header.th_flags |= TH_ACK;
        p->header.th_ack = ctx->peer_seq;

        dispatch_peer(sd, p);

        return 0;
}

/* 3 way handshake */

int send_syn(mysocket_t sd, context_t *ctx) {

	assert(ctx);

	ctx->my_seq = ctx->initial_sequence_num;
	
	STCPPacket *p = (STCPPacket *) malloc (sizeof(STCPPacket));
	
	p->header.th_seq = ctx->initial_sequence_num;
	p->header.th_flags |= TH_SYN;
			
	dispatch_peer(sd, p);
	
	ctx->connection_state = CSTATE_SYN_SENT;

	return 0;	
}

int send_syn_ack(mysocket_t sd, context_t *ctx, STCPPacket * peer_p) {

	assert(ctx);
	
	ctx->my_seq = ctx->initial_sequence_num;
	
	STCPPacket *p = (STCPPacket *) malloc (sizeof(STCPPacket));
		
	p->header.th_seq = ctx->initial_sequence_num;
	p->header.th_ack = peer_p->header.th_seq + 1;
	ctx->peer_seq = p->header.th_ack;
	p->header.th_flags |= TH_SYN;
	p->header.th_flags |= TH_ACK;
	
	dispatch_peer(sd, p);
	
	return 0;	
}

int send_ack_to_syn_ack(mysocket_t sd, context_t *ctx, STCPPacket * peer_p) {

	assert(ctx);
	
	STCPPacket *p = (STCPPacket *) malloc (sizeof(STCPPacket));
	
	p->header.th_ack = peer_p->header.th_seq + 1;
	ctx->peer_seq = p->header.th_ack;
	p->header.th_flags |= TH_ACK;
	
	dispatch_peer(sd, p);
	
	return 0;	
}

/* closing the connection */

/*
 * sends FIN
 */
int send_fin(mysocket_t sd, context_t *ctx) {

        assert(ctx);

        STCPPacket *fin_p = (STCPPacket *) malloc (sizeof(STCPPacket));
        fin_p->header.th_flags |= TH_FIN;
        //fin_p->header.th_seq = ctx->my_seq;
        //fin_p->header.th_off = 5;

        dispatch_peer(sd, fin_p);

        return 0;
}/*
 * sends FIN_ACK
 */
int send_fin_ack(mysocket_t sd, context_t *ctx, STCPPacket * peer_p) {

        assert(ctx);

        STCPPacket *fin_ack_p = (STCPPacket *) malloc (sizeof(STCPPacket));
        //fin_ack_p->header.th_flags |= TH_FIN;
        fin_ack_p->header.th_flags |= TH_ACK;
        //fin_ack_p->header.th_seq = peer_p->header.th_seq + 1;
        fin_ack_p->header.th_ack = ctx->peer_seq;
        ctx->peer_seq++;

        dispatch_peer(sd, fin_ack_p);

        return 0;
}
/* end of closing the connection */

/* end of 3 way handshake */

int dispatch_peer(mysocket_t sd, STCPPacket *p) {

	assert(p);	
	
	int size = 0;
	
	size = stcp_network_send(sd, p, sizeof(STCPPacket), NULL);
		
	return 0;
}

int dispatch_app(mysocket_t sd, STCPPacket *p) {

	assert(p);	
	
	stcp_app_send(sd, p, sizeof(STCPPacket));
	printf("Sent to server\n");
	
	return 0;
}

/**********************************************************************/
/* our_dprintf
 *numbers of these streams are independent of each other.
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}
