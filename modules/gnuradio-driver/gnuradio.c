#include <zmq.h>
#include <assert.h>

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#include "dev/nullradio.h"
#include "net/netstack.h"
#include "os/lib/crc16.h"
#include "net/packetbuf.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "GRADIO"
#define LOG_LEVEL LOG_LEVEL_NONE

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

static uint8_t tx_buf[PACKETBUF_SIZE + 3];
static uint8_t rx_buf[PACKETBUF_SIZE + 3];

static void *publisher, *sink, *context;
zmq_pollitem_t item;

PROCESS(gnuradio_process, "GNURadio driver \n");

/*---------------------------------------------------------------------------*/
static void cleanup(int signo)
{
  //TODO: Cleanup not working properly
  zmq_close(sink);
  zmq_close(publisher);
  zmq_ctx_destroy(context);
  fprintf(stderr,"Exiting \n");
  exit(0);
}

static int
init(void)
{
  LOG_INFO("Initializing the gnuradio driver \n");
  /* Create context */
  context = zmq_ctx_new();

  // Have to truncate the string to a char array
  char endpoint[20];
  sprintf(endpoint, "tcp://*:%d", PUB_PORT);

  /* Create pub socket */
  publisher = zmq_socket(context, ZMQ_PUB);
  int rc = zmq_bind(publisher, endpoint);

  if (rc == -1)
    LOG_INFO("Error creating publisher \n");

  sprintf(endpoint, "tcp://*:%d", SINK_PORT);

  /* Create pub socket */
  sink = zmq_socket(context, ZMQ_PULL);
  int rc1 = zmq_bind(sink, endpoint);

  if (rc1 == -1)
    LOG_INFO("Error creating sink \n");

  LOG_DBG("The endpoint is: %s \n", endpoint);

  //Set Linger period for the sockets
  int linger_period = 1;
  int rc2 = zmq_setsockopt(publisher, ZMQ_LINGER, &linger_period, sizeof(int));

  assert(rc2 == 0);

  int rc3 = zmq_setsockopt(sink, ZMQ_LINGER, &linger_period, sizeof(int));

  assert(rc3 == 0);

  item = (zmq_pollitem_t){sink, 0, ZMQ_POLLIN, 0};

  signal(SIGHUP, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);

  /* Start process */
  process_start(&gnuradio_process, NULL);

  return 0;
}
/*---------------------------------------------------------------------------*/
static int
prepare(const void *payload, unsigned short payload_len)
{
  LOG_INFO("prepare %u bytes\n", payload_len);
  tx_buf[0] = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_INTERFACE_ID);
  memcpy(&tx_buf[1], payload, MIN(sizeof(tx_buf), payload_len));
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
transmit(unsigned short transmit_len)
{
  uint16_t checksum;

  LOG_INFO("transmit %u bytes\n", transmit_len);

  /* Compute checksum */
  checksum = crc16_data(&tx_buf[1], transmit_len, 0);
  /* Add checksum */
  tx_buf[transmit_len + 1] = checksum & 0xFF;
  tx_buf[transmit_len + 2] = checksum >> 8;

  LOG_DBG("Message sent on interface: %d \n", (uint8_t)tx_buf[0]);
  zmq_send(publisher, (void *)tx_buf, transmit_len + 3, ZMQ_DONTWAIT);

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
  int ret = 0;
  ret = zmq_recv(sink, rx_buf, PACKETBUF_SIZE + 3, ZMQ_DONTWAIT);
  if (ret > 0)
  {
    uint16_t checksum;
    uint8_t iid;

    //Set iid
    iid = rx_buf[0];
    packetbuf_set_attr(PACKETBUF_ATTR_INTERFACE_ID, iid);
    LOG_DBG("Received message on interface: %d \n", iid);

    //Check checksum and put the data into the packetbuf
    if (ret >= 2 && (ret - 3) <= buf_len)
    {
      checksum = crc16_data(&rx_buf[1], ret - 3, 0);
      if (((uint8_t *)rx_buf)[ret - 2] == (checksum & 0xFF) && ((uint8_t *)rx_buf)[ret - 1] == checksum >> 8)
      {
        LOG_INFO("received %u bytes\n", ret - 3);
        memcpy(buf, &rx_buf[1], sizeof(uint8_t) * ret - 3);
        return ret - 3;
      }
    }
  }

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
  int ret = 0;
  ret = zmq_poll(&item, 1, 0);
  return ret != 0;
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
  zmq_close(sink);
  zmq_close(publisher);
  zmq_ctx_destroy(context);
  printf("Exiting \n");
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

  while (1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);

    packetbuf_clear();

    //
    len = radio_read(packetbuf_dataptr(), PACKETBUF_SIZE);
    packetbuf_set_datalen(len);

    if (len > 0)
    {
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
        set_object};
/*--------------------------------------------------------------------------*/
