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
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "flash.h"
#include "test.h"


int badblock=0;


static double prob_bad(int wear, int lifecycle)
{
    double x = (double)wear / lifecycle;
    if (x < 0) x = 0;
    if (x > 2) x = 2;
    return (!!badblock)*exp(8 * (x - 1));
}

static double stddev(int *arr, int n)
{
    if (n <= 1) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += arr[i];
    double mean = sum / n;

    double sq_diff = 0.0;
    for (int i = 0; i < n; i++)
    {
        double diff = arr[i] - mean;
        sq_diff += diff * diff;
    }

    return sqrt(sq_diff / n);
}


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
            int *wear = calloc(fa->prop.size/fa->prop.sector_size,sizeof(int));
            fa->wear = wear;
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
    uint32_t i;

    if(NULL != fa && fa->open)
    {
        if( ((addr % fa->prop.write_granularity) == 0) && ((len % fa->prop.write_granularity) == 0) )
        {
            if((addr + len) <= fa->size)
            {
                for(i = 0; i < len; i++) if(fa->flash[addr + i] != 0xff) break;
                if(i < len) CONSOLE(&conlog, "%s() FLASH %d WARNING WRITING TO DIRTY AREA SECTOR %03x ADDR 0x%x\n", __FUNCTION__, fa->id, (addr/fa->prop.sector_size), addr);
                for(i = 0; i < len; i++) fa->flash[addr + i] &= data[i];
                ret = len;
                CONSOLE(&conlog, "%s() FLASH %d WRITE SECTOR %03x ADDR 0x%x %d bytes\n", __FUNCTION__, fa->id, (addr/fa->prop.sector_size), addr, len);
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
            if(fa->wear[(addr / fa->prop.sector_size)]>=0) memcpy(data, &fa->flash[addr], len);
            else memset(data, 0x55, len);
            ret = len;
            CONSOLE(&conlog, "%s() FLASH %d READ [w=%d] 0x%x %d bytes\n", __FUNCTION__, fa->id, fa->wear[(addr / fa->prop.sector_size)], addr, len);
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
                int w=++fa->wear[(addr / fa->prop.sector_size)];
                if(((double)rand() / RAND_MAX) < prob_bad(w, fa->prop.lifecycle)) fa->wear[(addr / fa->prop.sector_size)]*=-1;
                CONSOLE(&conlog, "%s() FLASH %d ERASE [w=%d] SECTOR %03x\n", __FUNCTION__, fa->id, fa->wear[(addr / fa->prop.sector_size)], (addr / (fa->prop.sector_size)));
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
        if(NULL!=fa->wear)
        {
            double sum=0.0, ave=0.0;
            int max, min, i;
            int N=(fa->prop.size/fa->prop.sector_size);
            min=fa->wear[0];
            max=fa->wear[0];
            for(i=0;i<N;i++)
            {
              sum+=fa->wear[i];
              if(fa->wear[i]<min) min=fa->wear[i];
              if(fa->wear[i]>max) max=fa->wear[i];
            }
            ave=sum/N;
            CONSOLE(&conlog,"avg=%7.2f stddev=%7.2f min=%d max=%d\n",ave, stddev(fa->wear, N), min, max);
            free(fa->wear);
        }
        fa->elapsed=0.0;
        fa->open=0;
    }
    return (0);
}

