#include "dpdk_common.h"

struct p_skeleton {
    struct ethhdr ether_hdr;
    struct ip6_hdr ip_hdr;
    struct udphdr udp_hdr;
}ether_frame ;

struct p_skeleton *gen_dpdk_packet_info(struct config *configstruct) {
    memset(&ether_frame, 0, sizeof(struct p_skeleton));
    /* *** Ethernet header *** */
    memcpy(&ether_frame.ether_hdr.h_source, &SRC_MAC, 6 * sizeof (uint8_t));
    memcpy(&ether_frame.ether_hdr.h_dest, &DST_MAC, 6 * sizeof (uint8_t));
    // Next is ethernet type code (ETH_P_IPV6 for IPv6).
    // http://www.iana.org/assignments/ethernet-numbers
    ether_frame.ether_hdr.h_proto = htons(0x86DD);
    /* *** IPv6 header *** */
    // IPv6 version (4 bits), Traffic class (8 bits), Flow label (20 bits)
    ether_frame.ip_hdr.ip6_src = configstruct->src_addr;
    ether_frame.ip_hdr.ip6_flow = htonl ((6 << 28) | (0 << 20) | 0);
    // Next header (8 bits): 17 for UDP
    ether_frame.ip_hdr.ip6_nxt = IPPROTO_UDP;
    // Hop limit (8 bits): default to maximum value
    ether_frame.ip_hdr.ip6_hops = 255;
    /* *** UDP header *** */
    ether_frame.udp_hdr.source = configstruct->src_port;
    // Static UDP checksum
    ether_frame.udp_hdr.check = 0xFFAA;
    return &ether_frame;
}

static int send_packet(uint8_t portid, struct rte_mbuf *m) {
    rte_eth_tx_buffer(0, 0, tx_buffer[0], m);
    int sent = rte_eth_tx_buffer_flush(0,0,tx_buffer[0]);
    return sent;
}

// Frame length is usually 4158
// Ethernet frame length = ethernet header (MAC + MAC + ethernet type) + ethernet data (IP header + UDP header + UDP data)
struct rte_mbuf *dpdk_assemble(pkt_rqst pkt) {
    struct ethhdr *ether_hdr;
    struct ip6_hdr *ip_hdr;
    struct udphdr *udp_hdr;

    struct rte_mbuf *frame = rte_pktmbuf_alloc(bb_pktmbuf_pool);
    if (frame == NULL)
        perror("Fatal Error could not allocate packet."),exit(1);

    char *pkt_end = rte_pktmbuf_mtod(frame, char *);
    HEADER_PTR(ether_hdr, struct ethhdr, pkt_end);
    HEADER_PTR(ip_hdr, struct ip6_hdr, pkt_end);
    HEADER_PTR(udp_hdr, struct udphdr, pkt_end);

    //Copy pre-filled variables
    rte_memcpy(ether_hdr, &ether_frame.ether_hdr, ETH_HDRLEN);
    rte_memcpy(ip_hdr, &ether_frame.ip_hdr, IP6_HDRLEN);
    rte_memcpy(udp_hdr, &ether_frame.udp_hdr,UDP_HDRLEN);

    //Set destination IP
    rte_memcpy(&ip_hdr->ip6_dst, &pkt.dst_addr, IPV6_SIZE);

    // Payload length (16 bits): UDP header + UDP data
    ip_hdr->ip6_plen = htons(UDP_HDRLEN + pkt.datalen);
    // UDP header
    // We expect the port to already be in network byte order
    udp_hdr->dest = pkt.dst_port;
    // Length of UDP datagram (16 bits): UDP header + UDP data
    udp_hdr->len =  htons(UDP_HDRLEN + pkt.datalen);
    //udp_hdr->check = udp6_checksum (iphdr, udp_hdr, (uint8_t *) data, datalen);

    // Copy our data
    rte_memcpy(pkt_end, pkt.data, pkt.datalen);
    pkt_end += pkt.datalen;
    rte_pktmbuf_append(frame, pkt_end - rte_pktmbuf_mtod(frame, char *));
    // char dst_ip[INET6_ADDRSTRLEN];
    // inet_ntop(AF_INET6,&ip_hdr->ip6_dst, dst_ip, sizeof dst_ip);
    // printf("Sending to part two %s::%d\n", dst_ip, ntohs(udp_hdr->dest));
    return frame;
}



int dpdk_send(pkt_rqst pkt) {
    //Assemble the packet
    struct rte_mbuf *frame = dpdk_assemble(pkt);
    // Commit the dpdk packet
    send_packet(0, frame);
    //rte_pktmbuf_free(frame);
    return EXIT_SUCCESS;
}

/* Enqueue packets for TX and prepare them to be sent */
void dpdk_batched_send(pkt_rqst *pkts, int num_pkts, uint32_t *sub_ids) {
    struct mbuf_table *m_table;
   for (int i = 0; i < num_pkts; i++) {
        pkts[i].dst_addr.args = pkts[i].dst_addr.args | sub_ids[pkts[i].dst_addr.subid]<<16; 
        struct rte_mbuf *frame = dpdk_assemble(pkts[i]);
        rte_eth_tx_buffer(0, 0, tx_buffer[0], frame);
    }
    int sent = rte_eth_tx_buffer_flush(0,0,tx_buffer[0]);
}

static int bb_parse_portmask(const char *portmask)
{
    char *end = NULL;
    unsigned long pm;

    /* parse hexadecimal string */
    pm = strtoul(portmask, &end, 16);
    if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
        return EXIT_FAILURE;
    if (pm == 0)
        return EXIT_FAILURE;
    return pm;
}

/* Parse the argument given in the command line of the application */
static int
bb_parse_args(int argc, char **argvopt) {
    bb_enabled_port_mask = bb_parse_portmask("ffff");
    if (bb_enabled_port_mask == 0) {
        printf("invalid portmask\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void check_all_ports_link_status(uint8_t port_num, uint32_t port_mask) {
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
    uint8_t portid, count, all_ports_up, print_flag = 0;
    struct rte_eth_link link;

    printf("\nChecking link status");
    fflush(stdout);
    for (count = 0; count <= MAX_CHECK_TIME; count++) {
        all_ports_up = 1;
        for (portid = 0; portid < port_num; portid++) {
            if ((port_mask & (1 << portid)) == 0)
                continue;
            memset(&link, 0, sizeof(link));
            rte_eth_link_get_nowait(portid, &link);
            /* print link status if flag set */
            if (print_flag == 1) {
                if (link.link_status)
                    printf("Port %d Link Up - speed %u "
                            "Mbps - %s\n", (uint8_t)portid,
                            (unsigned)link.link_speed,
                            (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                            ("full-duplex") : ("half-duplex\n"));
                else
                    printf("Port %d Link Down\n",
                            (uint8_t)portid);
                continue;
            }
            /* clear all_ports_up flag if any link down */
            if (link.link_status == 0) {
                all_ports_up = 0;
                break;
            }
        }
        /* after finally printing all link status, get out */
        if (print_flag == 1)
            break;

        if (all_ports_up == 0) {
            printf(".");
            fflush(stdout);
            rte_delay_ms(CHECK_INTERVAL);
        }

        /* set the print_flag if all ports up or timeout */
        if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
            print_flag = 1;
            printf("done\n");
        }
    }
}

unsigned config_ports(uint8_t portid, uint8_t nb_ports, unsigned rx_lcore_id) {

    /* Initialize the port/queue configuration of each logical core */
    
    unsigned int nb_lcores = 0;
    struct lcore_queue_conf *qconf = NULL;
    for (portid = 0; portid < nb_ports; portid++) {
        /* skip ports that are not enabled */
        if ((bb_enabled_port_mask & (1 << portid)) == 0)
            continue;

        /* get the lcore_id for this port */
        while (rte_lcore_is_enabled(rx_lcore_id) == 0 ||
               lcore_queue_conf[rx_lcore_id].n_rx_port ==
               bb_rx_queue_per_lcore) {
            rx_lcore_id++;
            if (rx_lcore_id >= RTE_MAX_LCORE)
                rte_exit(EXIT_FAILURE, "Not enough cores\n");
        }

        if (qconf != &lcore_queue_conf[rx_lcore_id]) {
            /* Assigned a new logical core in the loop above. */
            qconf = &lcore_queue_conf[rx_lcore_id];
            nb_lcores++;
        }

        qconf->rx_port_list[qconf->n_rx_port] = portid;
        qconf->n_rx_port++;
        printf("Lcore %u: RX port %u\n", rx_lcore_id, portid);
    }
    return nb_lcores;
}


int init_ports(uint8_t portid, uint8_t nb_ports) {
    uint8_t nb_ports_available = nb_ports;
    int ret;
    for (portid = 0; portid < nb_ports; portid++) {
        struct rte_eth_rxconf rxq_conf;
        struct rte_eth_txconf txq_conf;
        struct rte_eth_conf local_port_conf = port_conf;
        struct rte_eth_dev_info dev_info;

        /* skip ports that are not enabled */
        if ((bb_enabled_port_mask & (1 << portid)) == 0) {
            printf("Skipping disabled port %u\n", portid);
            nb_ports_available--;
            continue;
        }

        /* init port */
        printf("Initializing port %u... ", portid);
        fflush(stdout);
        rte_eth_dev_info_get(portid, &dev_info);
        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
            local_port_conf.txmode.offloads |=
                DEV_TX_OFFLOAD_MBUF_FAST_FREE;
        ret = rte_eth_dev_configure(portid, 1, 1, &local_port_conf);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
                  ret, portid);

        ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
                               &nb_txd);
        if (ret < 0)
            rte_exit(EXIT_FAILURE,
                 "Cannot adjust number of descriptors: err=%d, port=%u\n",
                 ret, portid);

        rte_eth_macaddr_get(portid,&bb_ports_eth_addr[portid]);
        
        rte_eth_dev_set_mtu (portid, MTU);

        /* init one RX queue */
        fflush(stdout);
        rxq_conf = dev_info.default_rxconf;
        //rxq_conf.offloads = local_port_conf.rxmode.offloads;
        rxq_conf.rx_drop_en = 1;
        ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
                         rte_eth_dev_socket_id(portid),
                         NULL,
                         bb_pktmbuf_pool);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
                  ret, portid);

        /* init one TX queue on each port */
        fflush(stdout);
        txq_conf = dev_info.default_txconf;
        txq_conf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
        txq_conf.offloads = local_port_conf.txmode.offloads;

        ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
                rte_eth_dev_socket_id(portid),
                NULL);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
                ret, portid);

        /* Initialize TX buffers */
        tx_buffer[portid] = rte_zmalloc_socket("tx_buffer",
                RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
                rte_eth_dev_socket_id(portid));
        if (tx_buffer[portid] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
                    portid);

        rte_eth_tx_buffer_init(tx_buffer[portid], MAX_PKT_BURST);

        /* Start device */
        ret = rte_eth_dev_start(portid);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
                  ret, portid);
        printf("done: \n");
        rte_eth_promiscuous_enable(portid);
    }
    return ret;
}


unsigned setup_ports (uint8_t portid, uint8_t nb_ports) {
    unsigned nb_ports_in_mask = 0;
    struct rte_eth_dev_info dev_info;

    /* reset bb_dst_ports */
    for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++)
        bb_dst_ports[portid] = 0;
    uint8_t last_port = 0;
    /*
     * Each logical core is assigned a dedicated TX queue on each port.
     */
    for (portid = 0; portid < nb_ports; portid++) {
        /* skip ports that are not enabled */
        if ((bb_enabled_port_mask & (1 << portid)) == 0)
            continue;

        if (nb_ports_in_mask % 2) {
            bb_dst_ports[portid] = last_port;
            bb_dst_ports[last_port] = portid;
        }
        else
            last_port = portid;

        nb_ports_in_mask++;

        rte_eth_dev_info_get(portid, &dev_info);
    }
    if (nb_ports_in_mask % 2) {
        printf("Notice: odd number of ports in portmask.\n");
        bb_dst_ports[last_port] = last_port;
    }
    return nb_ports_in_mask;
}
// #define SRC_IP ((0<<24) + (0<<16) + (0<<8) + 0) /* src ip = 0.0.0.0 */
// #define DEST_IP ((192<<24) + (168<<16) + (1<<8) + 1) /* dest ip = 192.168.1.1 */
// #define FULL_MASK 0xffffffff /* full mask */
// #define EMPTY_MASK 0x0 /* empty mask */
// struct rte_flow *generate_ipv6_flow(uint8_t port_id, uint16_t rx_q, struct in6_addr dst_ip, struct rte_flow_error *error) {
//     struct rte_flow_attr attr;
//     struct rte_flow_item pattern[3];
//     struct rte_flow_action action[3];
//     struct rte_flow *flow = NULL;
//     struct rte_flow_action_queue queue = { .index = rx_q };
//     struct rte_flow_item_ipv4 ip4_spec;
//     struct rte_flow_item_ipv4 ip4_mask;
//     struct rte_flow_item_ipv6 ip6_spec;
//     struct rte_flow_item_ipv6 ip6_mask;
//     struct rte_flow_item_udp udp_spec;
//     struct rte_flow_item_udp udp_mask;
//     int res;
//     memset(pattern, 0, sizeof(pattern));
//     memset(action, 0, sizeof(action));
//     unsigned char subnet_mask[16] = 
//              "\xff\xff\xff\xff\xff\xff\xff\xff"
//             "\xff\xff\xff\xff\xff\xff\xff\xff";
//     /*
//      * set the rule attribute.
//      * in this case only ingress packets will be checked.
//      */
//     memset(&attr, 0, sizeof(struct rte_flow_attr));
//     attr.ingress = 1;

//     /*
//      * create the action sequence.
//      * one action only,  move packet to queue
//      */

//     action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
//     action[0].conf = &queue;
//     action[1].type = RTE_FLOW_ACTION_TYPE_END;

//     memset(&ip4_spec, 0, sizeof(struct rte_flow_item_ipv4));
//     memset(&ip4_mask, 0, sizeof(struct rte_flow_item_ipv4));
//     ip4_spec.hdr.dst_addr = htonl(DEST_IP);
//     ip4_mask.hdr.dst_addr = EMPTY_MASK;
//     ip4_spec.hdr.src_addr = htonl(SRC_IP);
//     ip4_mask.hdr.src_addr = EMPTY_MASK;
//     pattern[0].type = RTE_FLOW_ITEM_TYPE_IPV4;
//     pattern[0].spec = &ip4_spec;
//     pattern[0].mask = &ip4_mask;
//    /*
//      * setting the third level of the pattern (ip).
//      * in this example this is the level we care about
//      * so we set it according to the parameters.
//      */
//     // memset(&ip6_spec, 0, sizeof(struct rte_flow_item_ipv6));
//     // memset(&ip6_mask, 0, sizeof(struct rte_flow_item_ipv6));
//     // memcpy(ip6_spec.hdr.dst_addr,&ether_frame.ip_hdr.ip6_src, IPV6_SIZE);
//     // memcpy(ip6_mask.hdr.dst_addr, subnet_mask, IPV6_SIZE);
//     // memcpy(ip6_spec.hdr.src_addr,&ether_frame.ip_hdr.ip6_src, IPV6_SIZE);
//     // memcpy(ip6_mask.hdr.src_addr, subnet_mask, IPV6_SIZE);

//     // pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV6;
//     // pattern[1].spec = &ip6_spec;
//     // pattern[1].mask = &ip6_mask;

//     udp_spec.hdr.src_port = ether_frame.udp_hdr.source;
//     udp_mask.hdr.src_port = 0xFFFF;
//     udp_spec.hdr.dst_port = ether_frame.udp_hdr.source;
//     udp_mask.hdr.dst_port = 0;
//     pattern[1].type = RTE_FLOW_ITEM_TYPE_UDP;
//     pattern[1].spec = &udp_spec;
//     pattern[1].mask = &udp_mask;

//     /* the final level must be always type end */
//     pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

//     res = rte_flow_validate(port_id, &attr, pattern, action, error);
//     if (!res) {
//         flow = rte_flow_create(port_id, &attr, pattern, action, error);
//         rte_flow_isolate(port_id, 1, NULL);
//     }

//     return flow;
// }


int config_dpdk() {
    struct lcore_queue_conf *qconf;
    int ret;
    uint8_t nb_ports;
    uint8_t portid, last_port;
    unsigned lcore_id, rx_lcore_id;
    unsigned int nb_mbufs;
    char *sample_input[] = {"bluebridge", "-l", "0","-n","1", "--", "-p", "ffff", NULL};
    char **argv = sample_input;
    int argc = sizeof(sample_input) / sizeof(char*) - 1;

    
    /* init EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
    argc -= ret;
    argv += ret;

    /* parse application arguments (after the EAL ones) */
    ret = bb_parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid bb arguments\n");


    //if (rte_eal_pci_probe() < 0)
    //    rte_exit(EXIT_FAILURE, "Cannot probe PCI\n");

    nb_ports = rte_eth_dev_count();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

    rx_lcore_id = 0;
    portid = 0;
    unsigned nb_ports_in_mask = setup_ports(portid ,nb_ports);


    /* Initialize the port/queue configuration of each logical core */
    unsigned int nb_lcores = config_ports(portid , nb_ports, rx_lcore_id);


    nb_mbufs = RTE_MAX(nb_ports * (nb_rxd + nb_txd + MAX_PKT_BURST +
        nb_lcores * MEMPOOL_CACHE_SIZE), 8192U);
    /* create the mbuf pool */
    bb_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", nb_mbufs,
        MEMPOOL_CACHE_SIZE, 0, MTU, rte_socket_id());
    if (bb_pktmbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

    /* Initialise each port */
    ret = init_ports(portid, nb_ports);

    check_all_ports_link_status(nb_ports, bb_enabled_port_mask);

    printf("Waiting for the interface to initialise...\n");
    sleep(2);
    printf("NIC should be ready now...\n");

    // /* create flow to filter for packets */
    // struct rte_flow_error error;
    // struct rte_flow *flow = generate_ipv6_flow(0, 0, ether_frame.ip_hdr.ip6_dst, &error);
    // if (!flow) {
    //     printf("Flow can't be created %d message: %s\n",
    //         error.type,
    //         error.message ? error.message : "(no stated reason)");
    //     rte_exit(EXIT_FAILURE, "error in creating flow");
    // }

    return ret;
}
