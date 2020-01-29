#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sr_protocol.h"
#include "sr_utils.h"

uint16_t
cksum (const void *_data, int len)
{
  const uint8_t *data = _data;
  uint32_t sum;

  for (sum = 0;len >= 2; data += 2, len -= 2)
    sum += data[0] << 8 | data[1];
  if (len > 0)
    sum += data[0] << 8;
  while (sum > 0xffff)
    sum = (sum >> 16) + (sum & 0xffff);
  sum = htons (~sum);
  return sum ? sum : 0xffff;
}

/*
  Prints out formatted Ethernet address, e.g. 00:11:22:33:44:55
 */
void printEthAddr(uint8_t *addr) {
  int pos = 0;
  for (; pos < ETHER_ADDR_LEN; pos++) {
    uint8_t cur = addr[pos];
    if (pos > 0) {
      fprintf(stderr, ":");
    }
    fprintf(stderr, "%02X", cur);
  }
  fprintf(stderr, "\n");
}

/*
  Prints out fields in Ethernet header. Returns Ethernet type
 */
uint16_t printEthHeader(uint8_t *buf) {
  sr_ethernet_hdr_t *ehdr = (sr_ethernet_hdr_t *)buf;
  fprintf(stderr, "Ethernet Header:\n");
  fprintf(stderr, "\tDestination: ");
  printEthAddr(ehdr->ether_dhost);
  fprintf(stderr, "\tSource: ");
  printEthAddr(ehdr->ether_shost);
  fprintf(stderr, "\tType: %d\n", ntohs(ehdr->ether_type));
  return ntohs(ehdr->ether_type);
}

/*
  Prints out IP address as a string from in_addr
 */
void printIPAddress(struct in_addr address) {
  char buf[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, buf, 100) == NULL) {
    fprintf(stderr,"inet_ntop error on address conversion\n");
  }
  else {
    fprintf(stderr, "%s\n", buf);
  }
}

/*
 * Prints out fields in IP header. Returns IP protocol
 */
uint8_t printIPHeader(uint8_t *buf) {
  sr_ip_hdr_t *iphdr = (sr_ip_hdr_t *)(buf);
  fprintf(stderr, "IP Header:\n");
  fprintf(stderr, "\tVersion: %d\n", iphdr->ip_v);
  fprintf(stderr, "\tHeader Length: %d\n", iphdr->ip_hl);
  fprintf(stderr, "\tType of Service: %d\n", iphdr->ip_tos);
  fprintf(stderr, "\tLength: %d\n", ntohs(iphdr->ip_len));
  fprintf(stderr, "\tID: %d\n", ntohs(iphdr->ip_id));
  if (ntohs(iphdr->ip_off) & IP_DF) {
    fprintf(stderr, "\tFragment flag: DF\n");
  }
  else if (ntohs(iphdr->ip_off) & IP_MF) {
    fprintf(stderr, "\tFragment flag: MF\n");
  }
  else if (ntohs(iphdr->ip_off) & IP_RF) {
    fprintf(stderr, "\tFragment flag: R\n");
  }
  fprintf(stderr, "\tOffset: %d\n", ntohs(iphdr->ip_off) & IP_OFFMASK);
  fprintf(stderr, "\tTTL: %d\n", iphdr->ip_ttl);
  fprintf(stderr, "\tProtocol: %d\n", iphdr->ip_p);
  /*Keep checksum in NBO*/
  fprintf(stderr, "\tChecksum: %d\n", iphdr->ip_sum);
  fprintf(stderr, "\tSource: ");
  printIPFromInt(ntohl(iphdr->ip_src));
  fprintf(stderr, "\tDestination: ");
  printIPFromInt(ntohl(iphdr->ip_dst));
  return iphdr->ip_p;
}

/*
 * Prints out ICMP header fields
 */
void printICMPHeader(uint8_t *buf) {
  sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *)(buf);
  fprintf(stderr, "ICMP Header:\n");
  fprintf(stderr, "\tType: %d\n", icmp_hdr->icmp_type);
  fprintf(stderr, "\tCode: %d\n", icmp_hdr->icmp_code);
  /* Keep checksum in NBO */
  fprintf(stderr, "\tChecksum: %d\n", icmp_hdr->icmp_sum);
}

/*
 * Prints out IP address from integer value
 */
void printIPFromInt(uint32_t ip) {
  uint32_t curOctet = ip >> 24;
  fprintf(stderr, "%d.", curOctet);
  curOctet = (ip << 8) >> 24;
  fprintf(stderr, "%d.", curOctet);
  curOctet = (ip << 16) >> 24;
  fprintf(stderr, "%d.", curOctet);
  curOctet = (ip << 24) >> 24;
  fprintf(stderr, "%d\n", curOctet);
}

/*
 * Prints out fields in ARP header
 */
void printARPHeader(uint8_t *buf) {
  sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)(buf);
  fprintf(stderr, "ARP Header\n");
  fprintf(stderr, "\tHardware Type: %d\n", ntohs(arp_hdr->ar_hrd));
  fprintf(stderr, "\tProtocol Type: %d\n", ntohs(arp_hdr->ar_pro));
  fprintf(stderr, "\tHardware Address Length: %d\n", arp_hdr->ar_hln);
  fprintf(stderr, "\tProtocol Address Length: %d\n", arp_hdr->ar_pln);
  fprintf(stderr, "\tOpcode: %d\n", ntohs(arp_hdr->ar_op));
  fprintf(stderr, "\tSender Hardware Address: ");
  printEthAddr(arp_hdr->ar_sha);
  fprintf(stderr, "\tSender IP Address: ");
  printIPFromInt(ntohl(arp_hdr->ar_sip));
  fprintf(stderr, "\tTarget Hardware Address: ");
  printEthAddr(arp_hdr->ar_tha);
  fprintf(stderr, "\tTarget IP Address: ");
  printIPFromInt(ntohl(arp_hdr->ar_tip));
}

/*
 * Prints out all possible headers, starting from Ethernet
 */
void printAllHeaders(uint8_t *buf, uint32_t length) {
  uint16_t ethType = 0;
  int minLength = sizeof(sr_ethernet_hdr_t);
  if (length >= minLength) {
    ethType = printEthHeader(buf);
  }
  
  if (ethType == ETHERTYPE_IP) {
    minLength += sizeof(sr_ip_hdr_t);
    if (length >= minLength) {
      uint8_t ip_protocol = printIPHeader(buf + sizeof(sr_ethernet_hdr_t));
      if (ip_protocol == IPPROTO_ICMP) {
	minLength += sizeof(sr_icmp_hdr_t);
	if (length >= minLength) {
	  printICMPHeader(buf + sizeof(sr_ethernet_hdr_t) +
			  sizeof(sr_ip_hdr_t));
	}
	else {
	  fprintf(stderr, "Tried to print ICMP header, but insufficient length\n");
	}
      }
    }
    else {
      fprintf(stderr, "Tried to print IP header, but insufficient length\n");
    }

  }
  else if (ethType == ETHERTYPE_ARP) {
    minLength += sizeof(sr_arp_hdr_t);
    if (length >= minLength) {
      printARPHeader(buf + sizeof(sr_ethernet_hdr_t));
    }
    else {
      fprintf(stderr, "Tried to print ARP header, but insufficient length\n");
    }
  }
  else {
    fprintf(stderr, "Unrecognized Ethernet Type: %d\n", ethType);
  }
}

