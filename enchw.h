#ifndef ENCHW_H
#define ENCHW_H

#include <cstdint>
//#include <cstddef>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19

class enchw_device_t{

public:
constexpr enchw_device_t(unsigned int rate=20'000'000)
  : rate{rate}
{ }

void setup(){
    spi_init(SPI_PORT, rate);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
}

static void select(){
   asm volatile("nop \n nop \n nop");
   gpio_put(PIN_CS, 0); // Active low
   asm volatile("nop \n nop \n nop");
}
static void unselect(){
   asm volatile("nop \n nop \n nop");
   gpio_put(PIN_CS, 1);
   asm volatile("nop \n nop \n nop");
}
static uint8_t exchangebyte(const uint8_t out){
   uint8_t in;
   spi_write_read_blocking(SPI_PORT, &out, &in, 1);
   return in;
}

private:

const unsigned int rate;

};

#undef SPI_PORT
#undef PIN_MISO
#undef PIN_CS
#undef PIN_SCK
#undef PIN_MOSI

#endif // ENCHW_H
