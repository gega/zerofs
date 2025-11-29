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
#include <stdlib.h>
#include <time.h>
#include <libgen.h>

#define _XOPEN_SOURCE_EXTENDED
#include <ncurses.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <math.h>

#include "test.h"
#include "flash.h"
#include "lfs.h"


static int quit=0;
static int colors_supported=0;
static int op_delay=0;
static const wchar_t *colblocks[] = {L" ", L"_", L"▁", L"▂", L"▃", L"▄", L"▅", L"▆", L"▇", L"█"}; // 0-9


#define INVALID_ID (0xfe)
#define ERASED_ID (0xff)


////////////////////////////////////////////

void draw_update(int wait, int umap);


double simulation_factor = 1.0;
static int step_through = 1;

struct console conlog;
static lfs_t lfs;
static int height, width;
static char *test_dir;
static char *test_out;
static int draw_init=0;
static int current_file_id=INVALID_ID;
static uintptr_t stack_baseline;


// simulated flash 
static uint8_t mem_flash[4*1024*1024];  // 4MB -- 1024 blocks
static uintptr_t max_stack=0;
static uint8_t *sector_map; // one byte per block

// flash area descriptors

static const struct flash_prop flash_prop={ sizeof(mem_flash), 4096, 1, 36000.0, 600.0, 30.0, 2.5, 1.0 }; // based on BY25Q32ES datasheet, page is 256 bytes

static struct flash_area fas[]=
{
  // main flash area (slow SPI flash)
  // SPI comm slows down the operations
  // during erase reading is possible with special
  // erase-pause operation (additional slowdown)
  {
    FLASH_AREA_NFFS,
    0,
    NULL,
    mem_flash,
    sizeof(mem_flash),
    1,
    0.0,
    flash_prop
  },
  { -1, 0, NULL, NULL, 0, 0, 0.0, {0} }
};

#define LFS_NUMBER_OF_SECTORS (1024)

#include <stdint.h>
#include <string.h>
#include "lfs.h"


static char *files[400];
static int files_max=0;
static int file_find(const char *name)
{
  for(int i=0;i<ARRAY_SIZE(files);i++) if(files[i]!=NULL&&strcmp(files[i],name)==0) return(i);
  return(-1);
}
static int file_cache(const char *name, int add)
{
  int ret,i;

  if(add)
  {
    for(i=0;i<ARRAY_SIZE(files);i++) if(files[i]==NULL) break;
    if(i>=ARRAY_SIZE(files)) return(-1);
    ret=i;
    files[ret]=strdup(name);
    files_max++;
  }
  else
  {
    ret=file_find(name);
    if(ret>=0&&NULL!=files[ret])
    {
      for(i=0;i<LFS_NUMBER_OF_SECTORS;i++) if(sector_map[i]==ret) sector_map[i]=INVALID_ID;
      free(files[ret]);
      files[ret]=NULL;
    }
    files_max--;
  }
  return(ret);
}
static void files_free(void)
{
  for(int i=0;i<ARRAY_SIZE(files);i++) if(files[i]!=NULL) free(files[i]);
}

#define OFFSET(fa,blk,off) (((size_t)(blk)) * (fa)->prop.sector_size + (off))

static int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
  struct flash_area *fa=(struct flash_area *)c->context;
  int stk=(stack_baseline-((uintptr_t)&fa));
  if(stk>max_stack) max_stack=stk;
  size_t addr = OFFSET(fa, block, off);
  int ret=flash_area_read(fa, addr, buffer, size);
  return(ret>=0?0:LFS_ERR_CORRUPT);
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
  struct flash_area *fa=(struct flash_area *)c->context;
  int stk=(stack_baseline-((uintptr_t)&fa));
  if(stk>max_stack) max_stack=stk;
  size_t addr = OFFSET(fa, block, off);
  int ret=flash_area_write(fa, addr, buffer, size);
  if(current_file_id!=INVALID_ID) sector_map[addr/(fa)->prop.sector_size]=current_file_id;
  return(ret>=0?0:LFS_ERR_CORRUPT);
}

static int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
  struct flash_area *fa=(struct flash_area *)c->context;
  int stk=(stack_baseline-((uintptr_t)&fa));
  if(stk>max_stack) max_stack=stk;
  size_t addr = OFFSET(fa, block, 0);
  int ret=flash_area_erase(fa, addr, fa->prop.sector_size);
  sector_map[addr/(fa)->prop.sector_size]=ERASED_ID;
  return(ret>=0?0:LFS_ERR_CORRUPT);
}

static int lfs_sync(const struct lfs_config *c)
{
    (void)c;
    return(0);
}

// Configure the filesystem
#define LITTLEFS_CACHE_SIZE (512)
static const struct lfs_config lfs_cfg =
{
    .read  = lfs_read,
    .prog  = lfs_prog,
    .erase = lfs_erase,
    .sync  = lfs_sync,

    .read_size      = 1,
    .prog_size      = flash_prop.write_granularity,
    .block_size     = flash_prop.sector_size,
    .block_count    = flash_prop.size / flash_prop.sector_size,
    .block_cycles   = 500,
    .cache_size     = LITTLEFS_CACHE_SIZE,
    .lookahead_size = 16,
    .context        = (void *)&fas[0],
};

// lfs end


#define ZEROFS_EXTENSION_LIST \
    X("csv")                  \
    X("qla")                  \
    X("qli")


#define ZEROFS_IMPLEMENTATION
#include "zerofs.h"


static_assert(ZEROFS_NUMBER_OF_SECTORS == 1024, "change ram_sector_map and flash area sizes before adjusting sector count!");


// tui tools

static void set_color(int i)
{
  static int init=0;
  double lum;
  #define LUM(r, g, b) (0.2126*((r)/1000.0f) + 0.7152*((g)/1000.0f) + 0.0722*((b)/1000.0f))
  #define LUM_LIMIT (0.6f)

  if(!colors_supported) return;
  if(!init)
  {
    srand(COLOR_SEED);
    for (int i = 0; i < 800; i++)
    {
      do
      {
        short r1 = rand() % 1001, g1 = rand() % 1001, b1 = rand() % 1001;
        short r2 = rand() % 1001, g2 = rand() % 1001, b2 = rand() % 1001;
        short fg = 16 + i * 2, bg = 16 + i * 2 + 1; // just stay within range
        lum=fabs(LUM(r1,g1,b1)-LUM(r2,g2,b2));
        if(fg < COLORS && bg < COLORS && lum>=LUM_LIMIT)
        {
            init_color(fg, r1, g1, b1);
            init_color(bg, r2, g2, b2);
            init_pair(i + 1, fg, bg);
        }
      } while(lum<LUM_LIMIT);
    }
    init=1;
  }
  if(i<0||i>=ARRAY_SIZE(files))
  {
    attrset(COLOR_PAIR(0));
    use_default_colors();
  }
  else
  {
    attron(COLOR_PAIR(i+1));
  }
}


static void draw_map(uint8_t *p_map, uint8_t *n_map, int size, int x, int y, int col)
{
    int i, xx, yy;
    char buf[5];
    char *str;

    for(yy = i = 0; i < size; i += col, yy++)
    {
        snprintf(buf, sizeof(buf), "%04x", i);
        mvprintw(y + yy + 2, x, buf);
    }
    for(i = 0; i < col; i++)
    {
        snprintf(buf, sizeof(buf), "%02x", (uint8_t) i);
        mvprintw(y, 6 + x + (i * 3), buf);
    }
    for(xx = yy = i = 0; i < size; i++)
    {
        xx = 3 * (i % col);
        yy = i / col;
        str = buf;
        if(n_map[i] == ERASED_ID) str = "  ";
        else if(n_map[i] == INVALID_ID) str = "..";
        else snprintf(str, sizeof(buf), "%02x", n_map[i]);
        {
          // draw wear based on FLASH_ERASE_CYCLE
          if(NULL!=fas[0].wear)
          {
            int w=fas[0].wear[i];
            if(w>=0)
            {
              int b=(int)(w/(fas[0].prop.lifecycle/9.0));
              if(b>9) b=9;
              if(b<0) b=0;
              mvaddwstr(y + yy + 2, x + xx + 5, colblocks[b]);
            }
            else str = "xx";
          }
        }
        if(p_map[i] != n_map[i])
        {
            attron(A_REVERSE);
        }
        if(files[n_map[i]]!=NULL) set_color(n_map[i]);
        mvprintw(y + yy + 2, x + xx + 6, str);
        set_color(-1);
        if(p_map[i] != n_map[i])
        {
            attroff(A_REVERSE);
        }
    }
}

static void draw_status(lfs_t *lfs, int step, int w)
{
    char str[120];
    struct lfs_fsinfo fsi;
    attron(A_REVERSE);

    lfs_fs_stat(lfs, &fsi);

    int l = snprintf(str, sizeof(str), " %5d block_cnt=%d name_max=%d ", step, fsi.block_count, fsi.name_max);
    mvprintw(0, 0, str);
    str[0] = ' ';
    str[1] = '\0';
    for(int i = l; i < w; i++) mvprintw(0, i, str);

    attroff(A_REVERSE);
}

static void draw_files(lfs_t *lfs, int x, int y, int w, int h)
{
    static const int width=20;
    int id;
    int xx, yy;
    char str1[width + 1];
    char str2[width + 1];
    int valid;

    xx = 0;
    yy = 0;
    for(id = 0; id < ARRAY_SIZE(files); id++)
    {
        valid=1;
        if(files[id]!=NULL)
        {
            snprintf(str1, sizeof(str1), "%02x      %12s", id, files[id]);
            snprintf(str2, sizeof(str2), "                    ");
        }
        else
        {
            valid=0;
            snprintf(str1, sizeof(str1), "%02x                  ", id);
            snprintf(str2, sizeof(str2), "                    ");
        }
        if(!colors_supported&&valid) attron(A_REVERSE);
        if(valid) set_color(id);
        mvprintw(y + yy, x + xx, str1);
        mvprintw(y + yy + 1, x + xx, str2);
        if(!colors_supported&&valid) attroff(A_REVERSE);
        set_color(-1);
        yy += 2;
        if(yy > h)
        {
            xx += width + 2;
            yy = 0;
            if(xx > (w-width)) break;
        }
    }
}

static void draw_console(int x, int y, int w, int h)
{
    int yy, i, l, f;
    char line[220];

    for(i = conlog.disp-1, yy = y + h; yy > y; yy--, --i)
    {
        if(i < 0) i=ARRAY_SIZE(conlog.line) - 1;
        l = 0;
        if(conlog.line[i] != NULL)
        {
            snprintf(line, MIN(sizeof(line) - 1, w), conlog.line[i]);
            mvprintw(yy, x, line);
            l = strlen(line);
        }
        f = MIN(w - l, sizeof(line) - 1);
        memset(line, ' ', f);
        line[f] = '\0';
        mvprintw(yy, x + l, line);
    }
}

void draw_update(int wait, int umap)
{
    static uint8_t p_map[LFS_NUMBER_OF_SECTORS];
    static long cycle = 0;
    uint8_t *map;

    if(!draw_init) return;
    if(cycle == 0) memset(p_map, 0xff, sizeof(p_map));
    ++cycle;
    draw_status(&lfs, 0, width);
    map = sector_map;
    if(umap) draw_map(p_map, map, sizeof(p_map), 0, 2, 32);
    memcpy(p_map, map, sizeof(p_map));
    draw_console(0, 36, 32 * 3 + 5, height - 2 - 36);
    draw_files(&lfs, 32 * 3 + 5 + 2, 2, width - (32 * 3 + 5 + 2), height - 2);

    refresh();

    if(wait&&step_through)
    { 
        int ch=0;
        while((ch = getch()) != ' ' && ch!=10 && ch!='q')
        {
            switch(ch)
            {
                case KEY_UP:
                    conlog.disp--;
                    if(conlog.line[conlog.disp]==NULL) conlog.disp++;
                    draw_console(0, 36, 32 * 3 + 5, height - 2 - 36);
                    break;
                case KEY_DOWN:
                    conlog.disp++;
                    if(conlog.disp>conlog.pos) conlog.disp=conlog.pos;
                    draw_console(0, 36, 32 * 3 + 5, height - 2 - 36);
                    break;
            }
            refresh();
        }
        if(ch=='q') quit=1;
    }
    else
    {
      if(wait&&op_delay>0) usleep(op_delay*1000);
    }
}

// lua procedures

static int l_getch(lua_State *L)
{
  int ch=getch();
  lua_pushinteger(L, ch);
  return(1);
}

static int l_write(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int chunk = luaL_checkinteger(L, 2);
    char path[PATH_MAX];
    uint8_t *data;
    uint8_t *p;
    int st=-1;
    int l;

    strcpy(path, test_dir);
    int len = strlen(path);
    if(len + strlen(name) < PATH_MAX)
    {
        strcat(path, "/");
        strcat(path, name);
        FILE *f = fopen(path, "rb");
        if(NULL != f)
        {
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            fseek(f, 0, SEEK_SET);
            data = malloc(len);
            if(len == fread(data, 1, len, f))
            {
              lfs_file_t fp;
              lfs_remove(&lfs, name);
              file_cache(name,0);
              current_file_id=file_cache(name,1);
              uint8_t filebuf[LITTLEFS_CACHE_SIZE];
              struct lfs_file_config cfg = { .buffer = filebuf };
              st = lfs_file_opencfg(&lfs, &fp, name, LFS_O_RDWR | LFS_O_CREAT, &cfg);
              if(st >= 0)
              {
                // write in chunk buffer size
                p=data;
                l=len;
                while(l>0&&st>=0)
                {
                  st = lfs_file_write(&lfs, &fp, p, MIN(l,chunk));
                  p+=MIN(l,chunk);
                  l-=MIN(l,chunk);
                }
                if(st>0) st=0;
                if(st == 0) CONSOLE(&conlog, "%s() FILE '%s' [%d] WRITTEN\n", __FUNCTION__, name, len);
                else CONSOLE(&conlog, "ERROR %s() zerofs_write error: %d\n", __FUNCTION__, st);
                lfs_file_close(&lfs, &fp);
                if(st!=0) lfs_remove(&lfs, name);
              }
              else CONSOLE(&conlog, "ERROR %s() zerofs_createe error: %d\n", __FUNCTION__, st);
              current_file_id=INVALID_ID;
              draw_update(1,1);
            }
            else CONSOLE(&conlog, "ERROR %s() read error '%s'\n", __FUNCTION__, path);
            fclose(f);
            free(data);
        }
        else CONSOLE(&conlog, "ERROR %s() file '%s' not found\n", __FUNCTION__, path);
    }
    else CONSOLE(&conlog, "ERROR %s() path too long\n", __FUNCTION__);

    if(!quit) lua_pushinteger(L, st);
    
    return((quit?luaL_error(L, "Interrupted"):1));
}

static int l_verify(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char path[PATH_MAX];
    uint8_t *data, *data2;
    int st=-1;

    draw_update(0,0);

    strcpy(path, test_dir);
    int len = strlen(path);
    if(len + 1 + strlen(name) < PATH_MAX)
    {
        strcat(path, "/");
        strcat(path, name);
        FILE *f = fopen(path, "rb");
        if(NULL != f)
        {
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            fseek(f, 0, SEEK_SET);
            data = calloc(len,1);
            if(len == fread(data, 1, len, f))
            {
                lfs_file_t fp;
                draw_update(0,0);
                data2 = calloc(len,1);
                uint8_t filebuf[LITTLEFS_CACHE_SIZE];
                struct lfs_file_config cfg = { .buffer = filebuf };
                st = lfs_file_opencfg(&lfs, &fp, name, LFS_O_RDONLY, &cfg);
                if(st == 0)
                {
                  st = lfs_file_read(&lfs, &fp, data2, len);
                  if(st >= 0)
                  {
                    int i;
                    for(i=0;i<len;i++) if(data[i]!=data2[i]) break;
                    if(i>=len) { st=0; CONSOLE(&conlog, "%s() '%s' VERIFIED OK\n", __FUNCTION__, name); }
                    else { st=-1; CONSOLE(&conlog, "ERROR %s() '%s' differ at char %d\n", __FUNCTION__, name, i); }
                  }
                  else CONSOLE(&conlog, "ERROR %s() zerofs_read error: %d\n", __FUNCTION__, st);
                  lfs_file_close(&lfs, &fp);
                }
                else CONSOLE(&conlog, "ERROR %s() zerofs_open error: %d\n", __FUNCTION__, st);
                draw_update(1,0);
                free(data2);
            }
            else CONSOLE(&conlog, "ERROR %s() read error '%s'\n", __FUNCTION__, path);
            free(data);
            fclose(f);
        }
        else CONSOLE(&conlog, "ERROR %s() file '%s' not found\n", __FUNCTION__, path);
    }
    else CONSOLE(&conlog, "ERROR %s() path too long\n", __FUNCTION__);

    if(!quit) lua_pushinteger(L, st);

    return((quit?luaL_error(L, "Interrupted"):1));
}

static int l_setdir(lua_State *L)
{
    const char *dir = luaL_checkstring(L, 1);

    if(NULL != dir)
    {
        if(strlen(dir) < PATH_MAX)
        {
            if(NULL != test_dir) free(test_dir);
            test_dir = strdup(dir);
            CONSOLE(&conlog, "%s() DIR SET TO '%s'\n", __FUNCTION__, test_dir);
        } else
            CONSOLE(&conlog, "ERROR %s() dir too long!\n", __FUNCTION__);
    }

    return((quit?luaL_error(L, "Interrupted"):0));
}

static int l_setmode(lua_State *L)
{
    const char *mode = luaL_checkstring(L, 1);

    if(strcmp("read", mode) == 0)
    {
        CONSOLE(&conlog, "%s() READ MODE ENABLED\n", __FUNCTION__);
    }
    else if(strcmp("write", mode) == 0)
    {
        CONSOLE(&conlog, "%s() WRITE MODE ENABLED\n", __FUNCTION__);
    }
    else CONSOLE(&conlog, "ERROR %s() unsupported mode '%s' requested\n", __FUNCTION__, mode);

    draw_update(1,1);

    return(0);
}

static int l_printdebug(lua_State *L)
{
    FILE *f;
    f = fopen(test_out, "a");
    if(NULL != f)
    {
        fprintf(f, "\n%s() at %ld\n", __FUNCTION__, time(NULL));
        for(int i = 0; i < ARRAY_SIZE(conlog.line); i++)
        {
            int x = (conlog.pos + i) % ARRAY_SIZE(conlog.line);
            if(NULL != conlog.line[x]) fprintf(f, "%d: %s", x, conlog.line[x]);
        }
        fclose(f);
    }
    return((quit?luaL_error(L, "Interrupted"):0));
}

static int l_speed(lua_State *L)
{
    simulation_factor = luaL_checknumber(L, 1);
    op_delay = luaL_checkinteger(L, 2);
    return((quit?luaL_error(L, "Interrupted"):0));
}

static int l_setstep(lua_State *L)
{
    int isbool = lua_isboolean(L, 1);
    if (!isbool) return(luaL_error(L, "expected boolean"));
    int step = lua_toboolean(L, 1);
    if(step) step_through=1;
    else step_through=0;
    CONSOLE(&conlog,"%s() STEP MODE %s\n",__FUNCTION__, (step?"ENABLED":"DISABLED"));
    return((quit?luaL_error(L, "Interrupted"):0));
}

static int l_badblock(lua_State *L)
{
    int isbad = lua_isboolean(L, 1);
    if (!isbad) return(luaL_error(L, "expected boolean"));
    badblock = !!lua_toboolean(L, 1);
    CONSOLE(&conlog,"%s() BADBLOCK SIMULATION %s\n",__FUNCTION__, (badblock?"ENABLED":"DISABLED"));
    return((quit?luaL_error(L, "Interrupted"):0));
}

static int l_delete(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

    int st = lfs_remove(&lfs, name);
    CONSOLE(&conlog, "%s() '%s' st=%d\n", __FUNCTION__, name, st);
    file_cache(name,0);
    draw_update(1,1);
    if(!quit) lua_pushinteger(L, st);
    return((quit?luaL_error(L, "Interrupted"):1));
}

static int l_erase_async(lua_State *L)
{
  int st;
  
  st = lfs_fs_gc(&lfs);
  CONSOLE(&conlog, "%s() st=%d\n", __FUNCTION__, st);
  draw_update(1,1);
  if(!quit) lua_pushinteger(L, st);
  return((quit?luaL_error(L, "Interrupted"):1));
}

void l_warn(void *ud, const char *msg, int tocont)
{
  lua_State *L=ud;
  lua_Debug ar;
  lua_getstack(L, 1, &ar);
  lua_getinfo(L, "nSl", &ar);
  int line = ar.currentline;
  CONSOLE(&conlog, "lua info line %d: %s\n", line, msg);
}

int l_assert(lua_State *L)
{
  const char *msg = luaL_checkstring(L, 1);
  lua_Debug ar;
  lua_getstack(L, 1, &ar);
  lua_getinfo(L, "nSl", &ar);
  int line = ar.currentline;
  CONSOLE(&conlog, "lua error line %d: %s\n", line, msg);
  const char *ret = luaL_optstring(L, 1, "test failed");
  lua_pushfstring(L, "%s", ret);
  return lua_error(L);
}

// lua admin

static int luaopen_zerofslib(lua_State *L)
{
    luaL_Reg funcs[] = {
        { "write", l_write },
        { "verify", l_verify },
        { "setmode", l_setmode },
        { "printdebug", l_printdebug },
        { "delete", l_delete },
        { "speed", l_speed },
        { "setdir", l_setdir },
        { "setstep", l_setstep },
        { "getch", l_getch },
        { "assert", l_assert },
        { "badblock", l_badblock },
        { "erase_async", l_erase_async },
        { NULL, NULL }
    };
    luaL_newlib(L, funcs);

    return (1);
}

static lua_State *luainit(void)
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopen_zerofslib);
    lua_setfield(L, -2, "zerofs");
    lua_pop(L, 2);
    lua_setwarnf(L, l_warn, L);

    return (L);
}


int main(int argc, char **argv)
{
    volatile int stack_marker;
    if(argc < 2)
    {
        printf("Usage: %s testfile.lua\n", argv[0]);
        exit(0);
    }

    CONSOLE(&conlog, "\nTEST %s %s STARTED AT %ld\n",argv[0],argv[1],time(NULL));

    char *bn = basename(argv[1]);
    char *dot = strrchr(bn, '.');
    if(dot != NULL)
    {
        int l = dot - bn;
        test_out = calloc(l + 5, 1);
        for(int i = 0; i < l; i++) test_out[i] = bn[i];
        strcat(test_out, ".out");
    }
    else test_out = strdup("OUT");
    stack_baseline=((uintptr_t)&stack_marker);
    
    if(LFS_NUMBER_OF_SECTORS!=fas[0].size/fas[0].prop.sector_size) { fprintf(stderr, "check flash size config, calculated size is %d while defined size is %d\n",fas[0].size/fas[0].prop.sector_size,LFS_NUMBER_OF_SECTORS); return(1); }
    sector_map=calloc(fas[0].size/fas[0].prop.sector_size,1);
    memset(sector_map, 0xff, fas[0].size/fas[0].prop.sector_size);

    // flash empty state is FF
    memset(mem_flash, 0xff, sizeof(mem_flash));

    lua_State *L = luainit();

    initscr();                  // Start ncurses mode
    noecho();                   // Don't echo typed chars
    curs_set(0);                // Hide the cursor
    keypad(stdscr, TRUE);       // Enable arrow keys, etc.

    use_default_colors();
    if(has_colors())
    {
        start_color();
        colors_supported=1;
    }

    getmaxyx(stdscr, height, width);

    flash_area_open(FLASH_AREA_NFFS, (struct flash_area *)&fas[0], &fas[0]);
    lfs_format(&lfs, &lfs_cfg);
    fas[0].elapsed=0; // reset time measurement, we are not measuring the format part
    lfs_mount(&lfs, &lfs_cfg);
    
    draw_init=1;
    draw_update(0,1);

    if(luaL_dofile(L, argv[1]))
    {
        CONSOLE(&conlog, "lua error %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        step_through=1;
        draw_update(1,1);
        quit=0;
    }
    else
    {
      flash_area_close(&fas[0]);
      CONSOLE(&conlog, "%s max stack=%ld\n", "TEST PASSED", 0L);
      step_through=1;
      draw_update(1,1);
    }
    l_printdebug(NULL);

    curs_set(1);
    echo();

    endwin();                   // Restore normal terminal behavior

    lua_close(L);

    for(int i = 0; i < ARRAY_SIZE(conlog.line); i++) if(NULL != conlog.line[i]) free(conlog.line[i]);
    if(NULL != test_dir) free(test_dir);
    if(NULL != test_out) free(test_out);
    files_free();
    free(sector_map);

    return(0);
}
