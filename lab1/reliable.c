
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include "rlib.h"

/*Define state of client side and server side*/

#define WAITING_INPUT_DATA             0          //ready state to transmit packet
#define WAITING_ACK_PACKET             1
#define WAITING_EOF_ACK_PACKET         2
#define CLIENT_END_CONNECTION          3


/*Define state of server side*/
#define WAITING_PACKET                 4
#define WAITING_BUFFER_AVAILABLE       5 
#define SERVER_END_CONNECTION          6


/*Define info packet size*/
#define ACK_PACKET_SIZE                8
#define MAX_DATA_PACKET_SIZE           500
#define MAX_DATA_HEADER_PACKET_SIZE    512
#define MIN_DATA_PACKET_SIZE           12
#define EOF_PACKET_SIZE                12


/*Declare function prototype*/
int check_packet_corrupted(packet_t *pkt, size_t n);
void convert_packet_to_host_byte_order(packet_t *pkt);
void convert_packet_to_network_byte_order(packet_t *pkt);
void handle_data_packet(rel_t *ReliableState, packet_t *pkt);
void handle_ack_packet(rel_t *ReliableState, packet_t *pkt);
packet_t *create_data_packet(rel_t *ReliableState);
void create_and_send_ack_packet(rel_t *ReliableState);
uint16_t calculateChecksum();
void get_info_packet_last_sent_from_client(rel_t *ReliableState, packet_t *pkt);
void save_info_packet_last_received_in_server(rel_t *ReliableState, packet_t *pkt);
int make_buffer_available(rel_t *ReliableState);
void restranmit_packet(rel_t *ReliableState);
int get_time_last_transmission();


/*Declate struct for client side*/
typedef struct clientSide{
  int clientState;                          //State of client side
  uint32_t SeqnoPrevSent;                   //Used to number packets sent
  struct timeval lastTranmissionTime;       //timeout for retransmission
  
  // both of these field used for parameter for function conn_sendpkt();
  
  size_t lenLastPacketSent;               // save length of packet sent in previous time
  packet_t *pkt;                            //save a copy of previous packet
}clientSide;


/*Declare struct for server side*/
typedef struct serverSide {
  int serverState;
  uint32_t SeqnoPrevReceived;             //field to save seqno received in server side.
  uint32_t SeqnoNext; 

  /*Both of these field used for flow control*/
  uint32_t sizeDataPacketReceived;
  uint32_t numberByteRemainingMustBeFlushed;
  uint16_t output[MAX_DATA_PACKET_SIZE];
  packet_t *pkt;
}serverSide;

struct reliable_state {
  rel_t *next;      /* Linked list for traversing all connections */
  rel_t **prev;

  conn_t *c;      /* This is the connection object */

  /* Add your own data fields below this */
  int timeout;        /*Tells you what your retransmission timer should be,
                  in milliseconds.*/

  serverSide server;
  clientSide client;
};
rel_t *rel_list;





/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
      const struct config_common *cc)
{
  rel_t *r;

  r = xmalloc (sizeof (*r));
  memset (r, 0, sizeof (*r));

  if (!c) {
    c = conn_create (r, ss);
    if (!c) {
      free (r);
      return NULL;
    }
  }

  r->c = c;
  r->next = rel_list;
  r->prev = &rel_list;
  if (rel_list)
    rel_list->prev = &r->next;
  rel_list = r;

  /* Do any other initialization you need here */

  r->timeout = cc->timeout;

  r->client.clientState = WAITING_INPUT_DATA;
  r->client.SeqnoPrevSent = 0;

  
  r->server.serverState = WAITING_PACKET;
  r->server.SeqnoPrevReceived = 0;
  r->server.SeqnoNext = 1; 
  r->server.sizeDataPacketReceived = 0;
  r->server.numberByteRemainingMustBeFlushed = 0;

  return r;
}

void
rel_destroy (rel_t *r)
{
  if (r->next)
    r->next->prev = r->prev;
  *r->prev = r->next;
  conn_destroy (r->c);

  /* Free any other allocated memory here */
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
     const struct sockaddr_storage *ss,
     packet_t *pkt, size_t len)
{
}


/*server side when receiving data packet*/

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
  /*check packet is corrupted or not */
  if(check_packet_corrupted(pkt,n)){
    return;
  }

  /*Convert packet to host byte order*/
  convert_packet_to_host_byte_order(pkt);

  if(n == ACK_PACKET_SIZE){
    handle_ack_packet(r,pkt);     //if received ack knowdlege -> can be client or server
  }
  else{
    handle_data_packet(r,pkt);    // must be server
  }

}

/*client Get the data to transmit to the server*/

void
rel_read (rel_t *s)
{
  packet_t *pkt;
 

  if(s->client.clientState == WAITING_INPUT_DATA)
  {
    pkt = create_data_packet(s);
    if(pkt != NULL){
// preprocess before tranmission()
    if(pkt->len == EOF_PACKET_SIZE){
      s->client.clientState = WAITING_EOF_ACK_PACKET;
    }else{
      s->client.clientState = WAITING_ACK_PACKET;
    }
    convert_packet_to_network_byte_order(pkt);

  //Send data packet to server
    conn_sendpkt(s->c, (void *)pkt, sizeof(pkt->len));
    get_info_packet_last_sent_from_client(s, pkt);
    }
  }

}

/*Belongs to server side*/

void
rel_output (rel_t *r)
{
  if(r->server.serverState == WAITING_BUFFER_AVAILABLE){
    if(make_buffer_available(r)){
      create_and_send_ack_packet(r);
      r->server.serverState = WAITING_PACKET;
    }
  }
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
  rel_t *r;
  while(r){
    restranmit_packet(r);
    r = r->next;
  }
}



/*Che*/
int check_packet_corrupted(packet_t *pkt, size_t n)
{
  int packet_length = (int) ntohs(pkt->len);

  /*If packet length is not enough, return*/
  if(packet_length < n){
    return 1;
  }

  uint16_t checksum = pkt->cksum;


  memset(&(pkt->cksum), 0, sizeof(pkt->cksum));

  uint16_t checksumCalculated = cksum((void *)pkt, packet_length);            //CARE
  if(checksum != checksumCalculated)
    return 1;

  return 0;
}


/*CHECK THIS LINK : https://www.ibm.com/support/knowledgecenter/en/SSB27U_6.4.0/com.ibm.zvm.v640.kiml0/asonetw.htm*/
void convert_packet_to_host_byte_order(packet_t *pkt)
{
  pkt->len = ntohs(pkt->len);
  pkt->ackno = ntohl(pkt->ackno);
  pkt->cksum = ntohs(pkt->cksum);
  if(pkt->len != ACK_PACKET_SIZE){
    pkt->seqno = ntohl(pkt->seqno);
  }
}


/*CHECK THIS LINK : https://www.ibm.com/support/knowledgecenter/en/SSB27U_6.4.0/com.ibm.zvm.v640.kiml0/asonetw.htm*/
void convert_packet_to_network_byte_order(packet_t *pkt)
{
  pkt->len = htons(pkt->len);
  pkt->ackno = htonl(pkt->ackno);
  pkt->cksum = htons(pkt->cksum);
  if(pkt->len != ACK_PACKET_SIZE){
    pkt->seqno = htonl(pkt->seqno);
  }
}

void convert_ack_packet_to_network_byte_order(struct ack_packet *pkt)
{
  pkt->len = htons(pkt->len);
  pkt->ackno = htonl(pkt->ackno);
  pkt->cksum = htons(pkt->cksum);
}

/*
uint16_t calculateChecksum()
{

}
*/


/*Server side when receiving data_packet*/
void handle_data_packet(rel_t *ReliableState, packet_t *pkt)
{
  if(pkt->seqno < ReliableState->server.SeqnoPrevReceived + 1){
    create_and_send_ack_packet(ReliableState);
  }

  if((ReliableState->server.serverState == WAITING_PACKET)  && (pkt->seqno == ReliableState->server.SeqnoPrevReceived + 1)){
      if(pkt->len == EOF_PACKET_SIZE){
          conn_output(ReliableState->c, NULL, 0);
          create_and_send_ack_packet(ReliableState);
          ReliableState->server.serverState = SERVER_END_CONNECTION;
      
          if(ReliableState->client.clientState == CLIENT_END_CONNECTION){
            rel_destroy(ReliableState);
          }
      }
      
      else{
        /*flow controll : if buffer of receiver flushed all data received from client side in previous time*/
        save_info_packet_last_received_in_server(ReliableState, pkt);

        if(make_buffer_available(ReliableState)){
          create_and_send_ack_packet(ReliableState);
          ReliableState->server.serverState = WAITING_PACKET;
        }
        else{
          ReliableState->server.serverState = WAITING_BUFFER_AVAILABLE;
        }
      }
  }

}

void handle_ack_packet(rel_t *ReliableState, packet_t *pkt)
{
  //Resend ack packet to client side
  if(ReliableState->client.clientState == WAITING_ACK_PACKET){
    
    if(pkt->ackno == ReliableState->client.SeqnoPrevSent + 1){
      ReliableState->client.clientState = WAITING_INPUT_DATA;
      rel_read(ReliableState);
    }
  }

  if(ReliableState->client.clientState == WAITING_EOF_ACK_PACKET){
    if(pkt->ackno == ReliableState->client.SeqnoPrevSent + 1){
      
      ReliableState->client.clientState = CLIENT_END_CONNECTION;
      
      if(ReliableState->server.serverState == SERVER_END_CONNECTION){
        
        rel_destroy(ReliableState);
      }
    }
  }
}

/*Server side want to receive ack = SeqnoPrevReceived + 1*/
void create_and_send_ack_packet(rel_t *ReliableState)
{
  struct ack_packet *ack_pkt;
  ack_pkt = xmalloc(sizeof(*ack_pkt));

  ack_pkt->len = ACK_PACKET_SIZE;
  ack_pkt->ackno = ReliableState->server.SeqnoPrevReceived + 1;
  memset(&(ack_pkt->cksum), 0, sizeof(ack_pkt->cksum));
  ack_pkt->cksum = cksum((void *)ack_pkt, ack_pkt->len); 

  //sending
  convert_ack_packet_to_network_byte_order(ack_pkt);
  conn_sendpkt(ReliableState->c, (packet_t *)ack_pkt, ack_pkt->len);
}

/*This function used for server side*/
packet_t *create_data_packet(rel_t *ReliableState)
{
  packet_t *pkt;
  pkt = xmalloc(sizeof(*pkt));

  /*Create data_packet with max_size*/
  int data_packet;
 
  /*Get input data from reliable site*/
  data_packet = conn_input(ReliableState->c, pkt->data,  MAX_DATA_PACKET_SIZE);
  if(data_packet == 0){
    return NULL;
  }

  /*if packet is EOF then len = 12 according to decription in rlib.h*/
  if(data_packet == -1){
    pkt->len = EOF_PACKET_SIZE;
  }
  else{
    pkt->len = data_packet + EOF_PACKET_SIZE;
  }

  pkt->ackno = (uint32_t)1;       /*set the ackno field to 1 as according to description in 
                                  https://www.scs.stanford.edu/10au-cs144/lab/reliable/reliable.html*/
  
  pkt->seqno = (uint32_t)ReliableState->client.SeqnoPrevSent + 1;    //this protocol just numbers packets

  memset(&(pkt->cksum), 0, sizeof(pkt->cksum));
  pkt->cksum = cksum((void *)pkt, (int)pkt->len);      
  return pkt;
}


/*Get info of packet received at previous time : Server side */
void get_info_packet_last_sent_from_client(rel_t *ReliableState, packet_t *pkt)
{
  ReliableState->client.SeqnoPrevSent = pkt->seqno;
  ReliableState->client.pkt = pkt;
  ReliableState->client.lenLastPacketSent = pkt->len;
  gettimeofday(&(ReliableState->client.lastTranmissionTime), NULL);   //use for retranmission
}

void save_info_packet_last_received_in_server(rel_t *ReliableState, packet_t *pkt)
{
  ReliableState->server.pkt = pkt;
  memcpy (&(ReliableState->server.output), &(pkt->data), MAX_DATA_PACKET_SIZE);

  ReliableState->server.SeqnoPrevReceived = pkt->seqno;
  ReliableState->server.sizeDataPacketReceived = pkt->len - MIN_DATA_PACKET_SIZE;
  ReliableState->server.numberByteRemainingMustBeFlushed = 0;
}

/*Flow control 
  If return 1, buffer of server side flushed all data packet received from client in previous time.*/
int make_buffer_available(rel_t *ReliableState){
  size_t buffer_space = conn_bufspace(ReliableState->c);
  if(buffer_space == 0){
    return 0;
  }

  size_t byteLeft;
  size_t gap_between_read_receive = ReliableState->server.sizeDataPacketReceived - ReliableState->server.numberByteRemainingMustBeFlushed;
  if(gap_between_read_receive < buffer_space){
    byteLeft = gap_between_read_receive;
  }else{
    byteLeft = buffer_space;
  }

  uint16_t *buffer = ReliableState->server.output;
  uint16_t offset = ReliableState->server.numberByteRemainingMustBeFlushed;
  int size_packet_output = conn_output(ReliableState->c , &buffer[offset], byteLeft);
  
  ReliableState->server.numberByteRemainingMustBeFlushed += size_packet_output;
  /*Number of data read = number of data receive in buffer*/
  if(ReliableState->server.numberByteRemainingMustBeFlushed == ReliableState->server.sizeDataPacketReceived){
    return 1;
  }

  return 0;
}

void restranmit_packet(rel_t *ReliableState)
{
  if(ReliableState->client.clientState == WAITING_ACK_PACKET || ReliableState->client.clientState == WAITING_EOF_ACK_PACKET){
    
    int time_last_transmission = get_time_last_transmission(ReliableState);
    
    if(time_last_transmission > ReliableState->timeout){
        conn_sendpkt(ReliableState->c, ReliableState->client.pkt, (ReliableState->client.lenLastPacketSent));       /////////
        gettimeofday(&(ReliableState->client.lastTranmissionTime), NULL);
    }
  }
}

int get_time_last_transmission(rel_t *ReliableState)
{
  struct timeval now;
  gettimeofday(&now,NULL);
  return (((int)now.tv_usec * 1000 + (int)now.tv_sec / 1000) - 
    ((int)((ReliableState->client.lastTranmissionTime).tv_usec) * 1000 + (int)((ReliableState->client.lastTranmissionTime).tv_sec) / 1000));
}