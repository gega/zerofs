#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "flash.h"
#include "test.h"



int flash_area_open(int id, struct flash_area *fa, const struct flash_area *fas)
{
    int ret = -1;
    int i;

    if(fa != NULL)
    {
        for(i = 0; fas[i].id >= 0; i++) if(fas[i].id == id) break;
        if(fas[i].id == id)
        {
            memcpy(fa, &fas[i], sizeof(struct flash_area));
            fa->open = 1;
            ret = 0;
            CONSOLE(&conlog, "%s() FLASH AREA OPEN %d\n", __FUNCTION__, id);
        }
        else CONSOLE(&conlog, "ERROR %s() cannot find flash area %d\n", __FUNCTION__, id);
    }

    return(ret);
}

int flash_area_write(struct flash_area *fa, uint32_t addr, const uint8_t *data, uint32_t len)
{
    int ret = -1;

    if(NULL != fa && fa->open)
    {
        if( ((addr % fa->prop.write_granularity) == 0) && ((len % fa->prop.write_granularity) == 0) )
        {
            if((addr + len) <= fa->size)
            {
                for(uint32_t i = 0; i < len; i++) fa->flash[addr + i] &= data[i];
                ret = len;
                CONSOLE(&conlog, "%s() FLASH %d WRITE 0x%x %d bytes\n", __FUNCTION__, fa->id, addr, len);
                // delay
                double delay_us = (fa->prop.t_comm_byte_us * len) + (fa->prop.t_byte_first_us + (len - 1) * fa->prop.t_byte_us);
                fa->elapsed+=delay_us;
                usleep((long)(delay_us*simulation_factor));
                draw_update(0,1);
            }
            else CONSOLE(&conlog, "ERROR %s() address %x OVERFLOW\n", __FUNCTION__, addr);
        }
        else CONSOLE(&conlog, "ERROR %s() ALIGNMENT ERROR (g=%d) address %x size %x\n", __FUNCTION__, fa->prop.write_granularity, addr, len);
    }
    else CONSOLE(&conlog, "ERROR %s() INVALID FLASH AREA addr=%x len=%d\n", __FUNCTION__, addr, len);

    return(ret);
}

int flash_area_read(struct flash_area *fa, uint32_t addr, uint8_t *data, uint32_t len)
{
    int ret = -1;

    if(NULL != fa && fa->open)
    {
        if((addr + len) <= fa->size)
        {
            memcpy(data, &fa->flash[addr], len);
            ret = len;
            CONSOLE(&conlog, "%s() FLASH %d READ 0x%x %d bytes\n", __FUNCTION__, fa->id, addr, len);
            // delay
            double delay_us = fa->prop.t_comm_byte_us * len;
            fa->elapsed+=delay_us;
            usleep((long)(delay_us*simulation_factor));
            draw_update(0,1);
        }
        else CONSOLE(&conlog, "ERROR %s() address %x OVERFLOW\n", __FUNCTION__, addr);
    }

    return(ret);
}

int flash_area_erase(struct flash_area *fa, uint32_t addr, uint32_t len)
{
    int ret = -1;

    if(NULL != fa && fa->open)
    {
        if((addr % fa->prop.sector_size) == 0)
        {
            if((addr + len) <= fa->size)
            {
                memset(&fa->flash[addr], 0xff, len);
                ret = len;
                CONSOLE(&conlog, "%s() FLASH %d ERASE SECTOR 0x%04x\n", __FUNCTION__, fa->id, (addr / 4096));
                double delay_us = fa->prop.t_sector_erase_us;
                fa->elapsed+=delay_us;
                usleep((long)(delay_us*simulation_factor));
                draw_update(0,1);
            }
            else CONSOLE(&conlog, "ERROR %s() address %x OVERFLOW\n", __FUNCTION__, addr);
        }
        else CONSOLE(&conlog, "ERROR %s() adress %x BAD ALIGNMENT\n", __FUNCTION__, addr);
    }

    return(ret);
}

int flash_area_close(struct flash_area *fa)
{
    if(NULL != fa && fa->open)
    {
        CONSOLE(&conlog, "%s() FLASH AREA CLOSE %d elapsed = %.1f ms\n", __FUNCTION__, fa->id, fa->elapsed/1000.0);
        fa->elapsed=0.0;
        fa->open=0;
    }
    return (0);
}

