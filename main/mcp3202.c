#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <driver/spi_master.h>
#include <soc/spi_periph.h>
#include <driver/gpio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mcp3202.h"

#define TAG "MCP3202"

#define HOST_ID    HSPI_HOST
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

#define NOISE_FLOOR  20

void mcpInit(MCP_t * dev, int16_t input)
{
	esp_err_t ret;

	ESP_LOGI(TAG, "PIN_NUM_CS=%d",PIN_NUM_CS);
	gpio_reset_pin( PIN_NUM_CS );
	gpio_set_direction( PIN_NUM_CS, GPIO_MODE_OUTPUT );
	gpio_set_level( PIN_NUM_CS, 1 );

	ESP_LOGI(TAG, "PIN_NUM_MOSI=%d",PIN_NUM_MOSI);
	ESP_LOGI(TAG, "PIN_NUM_CLK=%d",PIN_NUM_CLK);
	spi_bus_config_t buscfg = {
		.sclk_io_num = PIN_NUM_CLK,
		.mosi_io_num = PIN_NUM_MOSI,
		.miso_io_num = PIN_NUM_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1
	};

	ret = spi_bus_initialize( HOST_ID, &buscfg, SPI_DMA_CH_AUTO  );
	ESP_LOGI(TAG, "spi_bus_initialize=%d",ret);
	assert(ret==ESP_OK);

	spi_device_interface_config_t devcfg={
		.clock_speed_hz = SPI_MASTER_FREQ_1M,
		.spics_io_num = PIN_NUM_CS,
		.queue_size = 1024,
		.mode = 0,
		.flags = SPI_DEVICE_NO_DUMMY,
	};

	spi_device_handle_t handle;
	ret = spi_bus_add_device( HOST_ID, &devcfg, &handle);
	ESP_LOGI(TAG, "spi_bus_add_device=%d",ret);
	assert(ret==ESP_OK);
	dev->_handle = handle;
	dev->_input = input;
    dev->_bits = 12;
    dev->_channels = 1;

    esp_rom_gpio_connect_out_signal(PIN_NUM_MOSI, spi_periph_signal[HOST_ID].spid_out, true, false);
}

unsigned char mcpReadData(MCP_t * dev, int16_t channel, uint16_t samps[], int16_t SAMP_N)
{
    static char peakFlag = 0;
    static int peakCnt = 0;
	char rbuf[SAMP_N*4];
	char wbuf[SAMP_N*4];

    memset(wbuf, 0, sizeof(rbuf));
    memset(rbuf, 0, sizeof(rbuf));
    wbuf[0] = 0x00;

    spi_transaction_t SPITransaction;
    esp_err_t ret;

    memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
    SPITransaction.length = SAMP_N * 4 * 8;
    SPITransaction.tx_buffer = wbuf;
    SPITransaction.rx_buffer = rbuf;

    for (int i = 0; i < SAMP_N *4; i++) {
        if (i%4 < 2) wbuf[i] = 0xFF;
        else wbuf[i] = 0x00;
    }
    wbuf[SAMP_N * 4 - 1] = 0x00;

	// if (channel > dev->_channels) {
	// 	ESP_LOGE(TAG, "Illegal channel %d", channel);
	// 	return 0;
	// }

    //uint64_t start = esp_timer_get_time();
	ret = spi_device_polling_transmit( dev->_handle, &SPITransaction );
    //uint64_t end = esp_timer_get_time();
    //ESP_LOGI(TAG, "Poll time: %llu",end-start);
	//assert(ret==ESP_OK); 
	//ESP_LOGI(TAG, "rbuf[0]=%02X rbuf[1]=%02X rbuf[2]=%02X", rbuf[0], rbuf[1], rbuf[2]);
    unsigned char distRet = 0;
    unsigned int maxVal = 0;
    unsigned int minVal = 9999;
    for (int i = 0; i < SAMP_N; i++) {
        samps[i] = ((rbuf[i*4]&0x1F)<<7)+(rbuf[i*4+1]>>1);
        if ((samps[i] > 4060 || samps[i] < 30) && peakFlag) {
            peakCnt++;
            //printf("%d,", samps[i]);
        }
        else if ((samps[i] > 4060 || samps[i] < 30) && !peakFlag) {
            peakCnt++;
            peakFlag = 1;
        }
        else if (samps[i] < 4060 || samps[i] > 30) {
            peakCnt = 0;
            peakFlag = 0;
        }

        if (samps[i] > maxVal) maxVal = samps[i];
        if (samps[i] < minVal) minVal = samps[i];
    }
    //printf("\n");
    if (peakCnt > PEAK_TOL) {
        peakCnt = 0;
        distRet = 1;
    }
    else if ((maxVal - minVal) <= NOISE_FLOOR) {
        for (int i = 0; i < SAMP_N; i++) {
            samps[i] = 2048;
        }
    }
    return distRet;
}