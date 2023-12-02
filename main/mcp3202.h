#include "driver/spi_master.h"

typedef struct {
	int	_bits;
	int	_channels;
	int _input;
	spi_device_handle_t _handle;
} MCP_t;

enum MCP_INPUT {
	MCP_SINGLE,
	MCP_DIFF
};

#define SPI_MASTER_FREQ_1M      (APB_CLK_FREQ/80)
#define PEAK_TOL 10

void mcpInit(MCP_t * dev, int16_t input);
unsigned char mcpReadData(MCP_t * dev, int16_t channel, uint16_t samps[], int16_t SAMP_N);