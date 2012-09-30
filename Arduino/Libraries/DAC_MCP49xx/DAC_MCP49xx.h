#ifndef _DAC_MCP49x1_H
#define _DAC_MCP49x1_H

#include <SPI.h>
#include <Arduino.h>
#include <inttypes.h>

#ifndef _SPI_H_INCLUDED
#error Please include SPI.h before DAC_MCP49x1.h!
#endif

// Microchip MCP4901/MCP4911/MCP4921 DAC driver
// Thomas Backman, 2012

class DAC_MCP49x1 {
  public:

	// These are the DAC models we support
	enum Model {
		MCP4901 = 1,
		MCP4911,
		MCP4921
	};
    
    DAC_MCP49x1(Model _model, int _ss_pin, int _ldac_pin = -1);
    void setBuffer(boolean _buffer) { this->bufferVref = _buffer; }
    void setPortWrite(boolean _port_write) { this->port_write = _port_write; }
    boolean setGain(int _gain);
    boolean setSPIDivider(int _spi_divider);
    void shutdown(void);
    void output(unsigned short _out);
	void latch(void); // Actually change the output, if the LDAC pin isn't shorted to ground

  private:
    int ss_pin;
    int LDAC_pin;
    int bitwidth;
    boolean bufferVref;
    boolean gain2x; /* false -> 1x, true -> 2x */
    boolean port_write; /* use optimized port writes? won't work everywhere! */
    int spi_divider;
};

#endif
