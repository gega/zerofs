/*
    BSD 2-Clause License

    Copyright (c) 2025, Gergely Gati

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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
  int lifecycle;
};

struct flash_area
{
  int id;
  int open;
  int *wear;
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
