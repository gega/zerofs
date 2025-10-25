#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_AREA_NFFS (17)
#define FLASH_AREA_SUPER (5)

struct flash_prop
{
  uint32_t size;
  int sector_size;
  int write_granularity;
  double t_sector_erase_us;
  double t_page_program_us;
  double t_byte_first_us;
  double t_byte_us;
  double t_comm_byte_us;
};

struct flash_area
{
  int id;
  int open;
  uint8_t *flash;
  uint32_t size;
  int device;
  double elapsed;
  const struct flash_prop prop;
};


int flash_area_open(int id, struct flash_area *fa, const struct flash_area *fas);
int flash_area_write(struct flash_area *fa, uint32_t addr, const uint8_t *data, uint32_t len);
int flash_area_read(struct flash_area *fa, uint32_t addr, uint8_t *data, uint32_t len);
int flash_area_erase(struct flash_area *fa, uint32_t addr, uint32_t len);
int flash_area_close(struct flash_area *fa);

#endif
