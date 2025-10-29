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
#include <locale.h>

#include "test.h"
#include "flash.h"



static uint8_t ram_sector_map[1024];
static int quit=0;
static const wchar_t *colblocks[] = {L" ", L"_", L"▁", L"▂", L"▃", L"▄", L"▅", L"▆", L"▇", L"█"}; // 0-9

////////////////////////////////////////////

void draw_update(int wait, int umap);


double simulation_factor = 1.0;
static int step_through = 1;
static int op_delay=0;

struct console conlog;
static struct zerofs zfs;
static int height, width;
static char *test_dir;
static char *test_out;
static int draw_init=0;
static int colors_supported=0;

// simulated flash 
static uint8_t mem_flash[4*1024*1024];  // 4MB -- 1024 blocks
static uint8_t mem_super[2 * 4096];     // 8KB -- 2    blocks

// flash area descriptors
static struct flash_area fas[] =
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
    { sizeof(mem_flash), 4096, 1, 36000.0, 600.0, 30.0, 2.5, 1.0, 100 } // based on BY25Q32ES datasheet, page is 256 bytes
  },
  // superblock area (fast MCU flash on nRF52832)
  // during erase and program, the cpu 
  // halts (no flash access in any kind, 
  // no instruction fetch)
  {
    FLASH_AREA_SUPER,
    0,
    NULL,
    mem_super,
    sizeof(mem_super),
    0,
    0.0,
    { sizeof(mem_super), 4096, 4, 80000.0, 67.5/4*4096, 67.5/4, 67.5/4, 0.0, 100000 } // random public sources
  },
  { -1, 0, NULL, NULL, 0, 0, 0.0, {0} }
};
static struct flash_area fa[2];


int fls_write(void *ud, uint32_t addr, const uint8_t *data, uint32_t len)
{
  return flash_area_write(ud, addr, data, len);
}

int fls_read(void *ud, uint32_t addr, uint8_t *data, uint32_t len)
{
  return flash_area_read(ud, addr, data, len);
}

int fls_erase(void *ud, uint32_t addr, uint32_t len, int background)
{
  return flash_area_erase(ud, addr, len);
}

#define ZEROFS_EXTENSION_LIST \
    X("csv")                  \
    X("qla")                  \
    X("qli")
#define ZEROFS_VERIFY (0)

#define ZEROFS_IMPLEMENTATION
#include "zerofs.h"


static struct zerofs_flash_access fac=
{
  fls_write, fls_read, fls_erase,
  mem_super,
  &fa[0],&fa[1]
};


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
    for (int i = 0; i < 255; i++)
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
  if(i<0||i>=ZEROFS_MAP_BAD)
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
        if(n_map[i] == ZEROFS_MAP_ERASED) str = "  ";
        else if(n_map[i] == ZEROFS_MAP_EMPTY) str = "..";
        else if(n_map[i] == ZEROFS_MAP_BAD) str = "++";
        else snprintf(str, sizeof(buf), "%02x", n_map[i]);
        {
          // draw wear based on FLASH_ERASE_CYCLE
          if(NULL!=fa[0].wear)
          {
            int w=fa[0].wear[i];
            if(w>=0)
            {
              int b=(int)(w/(fa[0].prop.lifecycle/9.0));
              if(b>9) b=9;
              if(b<0) b=0;
              mvaddwstr(y + yy + 2, x + xx + 5, colblocks[b]);
            }
          }
        }
        if(p_map[i] == n_map[i])
        {
            attron(A_REVERSE);
        }
        set_color(n_map[i]);
        mvprintw (y + yy + 2, x + xx + 6, str);
        set_color(-1);
        if(p_map[i] == n_map[i])
        {
            attroff(A_REVERSE);
        }
    }
}

static void draw_status(struct zerofs *zfs, int step, int w)
{
    char str[120];
    attron(A_REVERSE);

    int l = snprintf(str, sizeof(str), " %5d lw: $%04x [%5d] lastname: $%02x  speed: %5.1f  step: %s", step, zfs->meta.last_written, zfs->meta.last_written_len, zfs->last_namemap_id, 
                                                                                             simulation_factor, (step_through?"ON ":"OFF"));

    mvprintw(0, 0, str);
    str[0] = ' ';
    str[1] = '\0';
    for(int i = l; i < w; i++)
        mvprintw(0, i, str);

    attroff(A_REVERSE);
}

static void draw_files(struct zerofs *zfs, int x, int y, int w, int h)
{
    static const int width=20;
    int id;
    const struct zerofs_namemap *nm;
    int xx, yy;
    char str1[width + 1];
    char str2[width + 1];
    char name[8 + 1];
    int l;
    int valid;

    nm = zfs->superblock->namemap;
    xx = 0;
    yy = 0;
    for(id = 0; id < ZEROFS_MAX_NUMBER_OF_FILES; id++)
    {
        valid=1;
        l = (unsigned)ZEROFS_NM_GET_SIZE(&nm[id]);
        if(l > 9999999) l = 0;
        if(l==0) valid=0;
        name[0] = 0;
        zerofs_name_codec(name, (uint8_t *) nm[id].name, NULL);
        const char *ext="   ";
        uint16_t sec=nm[id].first_sector;
        uint16_t off=nm[id].first_offset;
        if(ZEROFS_NM_GET_TYPE(&nm[id])<ARRAY_SIZE(zerofs_extensions)) ext=zerofs_extensions[ZEROFS_NM_GET_TYPE(&nm[id])];
        if(strcmp("________",name)==0) { valid=0; strcpy(name,"        "); }
        if(sec!=0xffff)
        {
            snprintf(str1, sizeof(str1), "%02x %04x %8s.%3s", id, sec, name, ext);
            snprintf(str2, sizeof(str2), "   %04x      %7d", off, l);
        }
        else
        {
            snprintf(str1, sizeof(str1), "%02x      %8s.%3s", id, name, ext);
            snprintf(str2, sizeof(str2), "             %7d", l);
        }
        if(valid) attron(A_REVERSE);
        if(valid) set_color(id);
        mvprintw(y + yy, x + xx, str1);
        mvprintw(y + yy + 1, x + xx, str2);
        if(valid) attroff(A_REVERSE);
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
    static uint8_t p_map[ZEROFS_NUMBER_OF_SECTORS];
    static long cycle = 0;
    uint8_t *map;

    if(!draw_init) return;
    if(cycle == 0) memset(p_map, 0xff, sizeof(p_map));
    ++cycle;
    draw_status(&zfs, 0, width);
    map = (uint8_t *) ZEROFS_SECTOR_MAP(&zfs);
    if(umap) draw_map(p_map, map, sizeof(p_map), 0, 2, 32);
    memcpy(p_map, map, sizeof(p_map));
    draw_console(0, 36, 32 * 3 + 5, height - 2 - 36);
    draw_files(&zfs, 32 * 3 + 5 + 2, 2, width - (32 * 3 + 5 + 2), height - 2);

    refresh();

    if(wait&&step_through)
    {
        int ch=0;
        nodelay(stdscr, FALSE);
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
                case '<':
                  simulation_factor-=log2(simulation_factor);
                  if(simulation_factor<1.001f) simulation_factor=0.0f;
                  draw_status(&zfs, 0, width);
                  break;
                case '>':
                  simulation_factor+=log2(simulation_factor);
                  if(simulation_factor>1000.0f) simulation_factor=1000.0f;
                  draw_status(&zfs, 0, width);
                  break;
                case 's':
                  step_through^=1;
                  draw_status(&zfs, 0, width);
                  break;
                
            }
            refresh();
        }
        if(ch=='q') quit=1;
    }
    else
    {
      nodelay(stdscr, TRUE);
      int ch=0;
      ch = getch();
      switch(ch)
      {
        case '<':
          simulation_factor-=log2(simulation_factor);
          if(simulation_factor<1.001f) simulation_factor=0.0f;
          break;
        case '>':
          simulation_factor+=log2(simulation_factor);
          if(simulation_factor>1000.0f) simulation_factor=1000.0f;
          break;
        case 's':
          step_through^=1;
          break;
      }
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
                struct zerofs_file fp;
                st = zerofs_create(&zfs, &fp, name);
                if(st == 0)
                {
                    // write in chunk buffer size
                    p=data;
                    l=len;
                    while(l>0&&st==0)
                    {
                      st = zerofs_write(&fp, p, MIN(l,chunk));
                      p+=MIN(l,chunk);
                      l-=MIN(l,chunk);
                    }
                    if(st == 0)
                    {
                        st = zerofs_close(&fp);
                        if(st == 0) CONSOLE(&conlog, "%s() FILE '%s' [%d] WRITTEN\n", __FUNCTION__, name, len);
                        else CONSOLE(&conlog, "ERROR %s() zerofs_close error: %d\n", __FUNCTION__, st);
                    }
                    else CONSOLE(&conlog, "ERROR %s() zerofs_write error: %d\n", __FUNCTION__, st);
                }
                else CONSOLE(&conlog, "ERROR %s() zerofs_create error: %d\n", __FUNCTION__, st);
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
                struct zerofs_file fp;
                draw_update(0,0);
                data2 = calloc(len,1);
                st = zerofs_open(&zfs, &fp, name);
                if(st == 0)
                {
                  st = zerofs_read(&fp, data2, len);
                  if(st == 0)
                  {
                    int i;
                    for(i=0;i<len;i++) if(data[i]!=data2[i]) break;
                    if(i>=len) { st=0; CONSOLE(&conlog, "%s() '%s' VERIFIED OK\n", __FUNCTION__, name); }
                    else { st=-1; CONSOLE(&conlog, "ERROR %s() '%s' differ at char %d\n", __FUNCTION__, name, i); }
                  }
                  else CONSOLE(&conlog, "ERROR %s() zerofs_read error: %d\n", __FUNCTION__, st);
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
        zerofs_readonly_mode(&zfs, NULL);
        CONSOLE(&conlog, "%s() READ MODE ENABLED\n", __FUNCTION__);
    }
    else if(strcmp("write", mode) == 0)
    {
        zerofs_readonly_mode(&zfs, ram_sector_map);
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
            if(NULL != conlog.line[x])
                fprintf(f, "%d: %s", x, conlog.line[x]);
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
    int st;

    st=zerofs_delete(&zfs, name);
    CONSOLE(&conlog, "%s() '%s' st=%d\n", __FUNCTION__, name, st);
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
        for(int i = 0; i < l; i++)
            test_out[i] = bn[i];
        strcat(test_out, ".out");
    } else
        test_out = strdup("OUT");

    // flash empty state is FF
    memset(mem_flash, 0xff, sizeof(mem_flash));
    memset(mem_super, 0xff, sizeof(mem_super));

    lua_State *L = luainit();

    setlocale(LC_ALL, "");
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

    flash_area_open(FLASH_AREA_NFFS, &fa[0], &fas[0]);
    flash_area_open(FLASH_AREA_SUPER, &fa[1], &fas[1]);
    zerofs_init(&zfs, &fac);
    zerofs_format(&zfs);
    
    draw_init=1;
    draw_update(0,1);

    if(luaL_dofile(L, argv[1]))
    {
        CONSOLE(&conlog, "lua error %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        step_through=1;
        draw_update(1,1);
        quit=0;
        flash_area_close(&fa[0]);
        flash_area_close(&fa[1]);
    }
    else
    {
      flash_area_close(&fa[0]);
      flash_area_close(&fa[1]);
      CONSOLE(&conlog, "%s max_stack=%ld\n", "TEST PASSED", 0L);
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

    return(0);
}
