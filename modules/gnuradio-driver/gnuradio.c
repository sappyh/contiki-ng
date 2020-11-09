#include <zmq.h>


#include "dev/nullradio.h"
#include "net/netstack.h"
#include "os/lib/crc16.h"
#include "net/packetbuf.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "GRADIO"
#define LOG_LEVEL LOG_LEVEL_DBG

// TX PORT: PUB PORT
#ifndef PUB_CONF_PORT
#define PUB_PORT 52001
#else 
#define PUB_PORT PUB_CONF_PORT
#endif

// RX PORT: SINK PORT
#ifndef SINK_CONF_PORT
#define SINK_PORT 52002
#else 
#define SINK_PORT SINK_CONF_PORT
#endif

static uint8_t tx_buf[PACKETBUF_SIZE + 2];

static char publish_buf[PACKETBUF_SIZE + 3];
// static uint8_t rx_buf[PACKETBUF_SIZE + 2];

static void *publisher, *sink, *context;
PROCESS(gnuradio_process, "GNURadio driver \n");

/*---------------------------------------------------------------------------*/
static int
init(void)
{
  LOG_INFO("Initializing the gnuradio driver \n");
  /* Create context */
  context = zmq_ctx_new ();

  // Have to truncate the string to a char array
  char endpoint[20];
  sprintf (endpoint, "tcp://*:%d", PUB_PORT);


  /* Create pub socket */
  publisher = zmq_socket (context, ZMQ_PUB);
  int rc = zmq_bind (publisher, endpoint);
  
  if(rc == -1)
    LOG_INFO("Error creating publisher \n");

  sprintf (endpoint, "tcp://*:%d", SINK_PORT);

  /* Create pub socket */
  sink = zmq_socket (context, ZMQ_PULL);
  int rc1 = zmq_bind (sink, endpoint);

  if(rc1 == -1)
    LOG_INFO("Error creating sink \n");

  LOG_DBG("The endpoint is: %s \n", endpoint);
 
  /* Start process */
  process_start(&gnuradio_process, NULL);

  return 0;
}
/*---------------------------------------------------------------------------*/
static int
prepare(const void *payload, unsigned short payload_len)
{
  LOG_INFO("prepare %u bytes\n", payload_len);
  memcpy(tx_buf, payload, MIN(sizeof(tx_buf), payload_len));
  LOG_DBG("Received message with interface id: %d \n",packetbuf_attr(PACKETBUF_ATTR_INTERFACE_ID));
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
transmit(unsigned short transmit_len)
{
  uint16_t checksum;

  LOG_INFO("transmit %u bytes\n", transmit_len);
  
  /* Compute checksum */
  checksum = crc16_data(tx_buf, transmit_len, 0);
  /* Add checksum */
  tx_buf[transmit_len] = checksum & 0xFF;
  tx_buf[transmit_len+1] = checksum >> 8;

  sprintf(publish_buf, "%c%*s", (char)packetbuf_attr(PACKETBUF_ATTR_INTERFACE_ID),(int)transmit_len+2, (char*)tx_buf);
  LOG_DBG("Message sent: \n");
  LOG_DBG("%d\n", (uint8_t)publish_buf[0]);
  zmq_send(publisher, (void *)publish_buf, transmit_len+3, ZMQ_DONTWAIT);

  return RADIO_TX_OK;
}
/*---------------------------------------------------------------------------*/
static int
grsend(const void *payload, unsigned short payload_len)
{
  prepare(payload, payload_len);
  return transmit(payload_len);
}
/*---------------------------------------------------------------------------*/
static int
radio_read(void *buf, unsigned short buf_len)
{
  // int ret = 0;
  // struct timeval timeout;
  // static fd_set readfds;

  // timeout.tv_sec = 0;
  // timeout.tv_usec = 0;
  // FD_ZERO(&readfds);
  // FD_SET(udp_socket, &readfds);

  // ret = select(udp_socket + 1, &readfds, NULL, NULL, &timeout);

  // if(ret != 0) {
  //   if(FD_ISSET(udp_socket, &readfds)) {
  //     uint16_t checksum;
  //     /* Receive in dedicated buf where we're guaranteed to have enough
  //        space for CRC */
  //     ret = recvfrom(udp_socket, rx_buf, sizeof(rx_buf), 0, NULL, 0);
  //     /* Verify checksum */
  //     if(ret >= 2 && (ret - 2) <= buf_len) {
  //       checksum = crc16_data(rx_buf, ret-2, 0);
  //       if(((uint8_t *)rx_buf)[ret-2] == (checksum & 0xFF)
  //         && ((uint8_t *)rx_buf)[ret-1] == checksum >> 8) {
  //           LOG_INFO("received %u bytes\n", ret-2);
  //           memcpy(buf, rx_buf, ret-2);
  //           return ret-2;
  //       }
  //     }
  //   }
  // }

  return 0;
}
/*---------------------------------------------------------------------------*/
static int
channel_clear(void)
{
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
receiving_packet(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
pending_packet(void)
{
  // int ret = 0;
  // struct timeval timeout;
  // static fd_set readfds;

  // timeout.tv_sec = 0;
  // timeout.tv_usec = 0;
  // FD_ZERO(&readfds);
  // FD_SET(udp_socket, &readfds);

  // ret = select(udp_socket + 1, &readfds, NULL, NULL, &timeout);

  // return ret != 0;
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
on(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
off(void)
{
  zmq_close (sink);
  zmq_close (publisher);
  zmq_ctx_destroy (context);
  return 0;
}
/*---------------------------------------------------------------------------*/
static radio_result_t
get_value(radio_param_t param, radio_value_t *value)
{
  return RADIO_RESULT_NOT_SUPPORTED;
}
/*---------------------------------------------------------------------------*/
static radio_result_t
set_value(radio_param_t param, radio_value_t value)
{
  return RADIO_RESULT_NOT_SUPPORTED;
}
/*---------------------------------------------------------------------------*/
static radio_result_t
get_object(radio_param_t param, void *dest, size_t size)
{
  return RADIO_RESULT_NOT_SUPPORTED;
}
/*---------------------------------------------------------------------------*/
static radio_result_t
set_object(radio_param_t param, const void *src, size_t size)
{
  return RADIO_RESULT_NOT_SUPPORTED;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(gnuradio_process, ev, data)
{
  static struct etimer periodic_timer;
  int len;
  PROCESS_BEGIN();


  etimer_set(&periodic_timer, 1);

  LOG_INFO("gnuradio_process: started\n");

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);

    packetbuf_clear();
    //packetbuf_set_attr(PACKETBUF_ATTR_TIMESTAMP, last_packet_timestamp);
    len = radio_read(packetbuf_dataptr(), PACKETBUF_SIZE);
    packetbuf_set_datalen(len);

    if(len > 0) {
      NETSTACK_MAC.input();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
const struct radio_driver gnuradio_driver =
  {
    init,
    prepare,
    transmit,
    grsend,
    radio_read,
    channel_clear,
    receiving_packet,
    pending_packet,
    on,
    off,
    get_value,
    set_value,
    get_object,
    set_object
  };
/*--------------------------------------------------------------------------*/
