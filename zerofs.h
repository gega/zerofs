#ifndef ZEROFS_H
#define ZEROFS_H

// CONFIG AREA -----------------

#ifndef ZEROFS_FLASH_SIZE_KB
#define ZEROFS_FLASH_SIZE_KB (4096)
#endif

#ifndef ZEROFS_FLASH_SECTOR_SIZE
#define ZEROFS_FLASH_SECTOR_SIZE (4096)
#endif

#ifndef ZEROFS_MAX_NUMBER_OF_FILES
#define ZEROFS_MAX_NUMBER_OF_FILES (191)
#endif

#ifndef ZEROFS_SUPER_SECTOR_SIZE
#define ZEROFS_SUPER_SECTOR_SIZE (4096)
#endif

#ifndef ZEROFS_SUPER_WRITE_GRANULARITY
#define ZEROFS_SUPER_WRITE_GRANULARITY (4)
#endif

#ifndef ZEROFS_VERIFY
#define ZEROFS_VERIFY (0)
#endif

#ifndef ZEROFS_EXTENSION_LIST
#define ZEROFS_EXTENSION_LIST \
    X("bin")                 \
    X("txt")                 \
    X("zip")
#endif

// CONFIG AREA -----------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>


#define ZEROFS_FILENAME_MAX (12)
#define ZEROFS_MAX_FILES (0xfc)

static_assert(ZEROFS_FLASH_SECTOR_SIZE<=0xffff,"Sector size must be fit in 16 bit");
static_assert(ZEROFS_MAX_NUMBER_OF_FILES<=ZEROFS_MAX_FILES,"Max number of files with this superblock structure is 0xfd");

#define ZEROFS_NUMBER_OF_SECTORS ((ZEROFS_FLASH_SIZE_KB*1024)/ZEROFS_FLASH_SECTOR_SIZE)

#define ZEROFS_SUPERBLOCK_VERSION_MAX (0xfffe)

#define ZEROFS_MAP_EMPTY    (0xfeu)
#define ZEROFS_MAP_ERASED   (0xffu)
#define ZEROFS_MAP_BAD      (0xfdu)

#define ZEROFS_SUPER_MAPPED (~(uint16_t)0)

typedef uint16_t sector_t;

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef ABS
#define ABS(a) ((a)<0 ? -(a) : (a))
#endif

// rom struct for flash access
struct zerofs_flash_access
{
  int (*fls_write)(void *ud, uint32_t addr, const uint8_t *data, uint32_t len);
  int (*fls_read)(void *ud, uint32_t addr, uint8_t *data, uint32_t len);
  int (*fls_erase)(void *ud, uint32_t addr, uint32_t len, int background);
  const uint8_t *superblock_banks;
  void *data_ud;
  void *super_ud;
};

enum zerofs_mode
{
  ZEROFS_MODE_CLOSED = 0,
  ZEROFS_MODE_READ_ONLY,
  ZEROFS_MODE_WRITE_ONLY,
  ZEROFS_MODEMAX
};

#define ZEROFS_SECTOR_MAP(zfs) ( (zfs)->sector_map ? (zfs)->sector_map : (zfs)->superblock->sector_map )

#define ZEROFS_TYPE_UNKNOWN (0)

static const char *zerofs_extensions[]=
{
    "---",
    #define X(ext) ext,
    ZEROFS_EXTENSION_LIST
    #undef X
    NULL
};

// error codes
#define ZEROFS_ERR_MAXFILES    (-2) // ZEROFS_MAX_NUMBER_OF_FILES reached
#define ZEROFS_ERR_NOTFOUND    (-3)
#define ZEROFS_ERR_READMODE    (-4)
#define ZEROFS_ERR_NOSPACE     (-5)
#define ZEROFS_ERR_OPEN        (-6)
#define ZEROFS_ERR_ARG         (-7)
#define ZEROFS_ERR_WRITEMODE   (-8)
#define ZEROFS_ERR_OVERFLOW    (-9)
#define ZEROFS_ERR_BADSECTOR   (-10)
#define ZEROFS_ERR_INVALIDNAME (-11)
#define ZEROFS_ERR_INVALIDFP   (-12)

// get sector_map index from the base of last_written
#define ZEROFS_BLOCK(zfs, i) (((zfs)->meta.last_written+(i))%ZEROFS_NUMBER_OF_SECTORS)


static_assert(sizeof(int)>=4, "int should be at least 4 bytes");

struct zerofs_namemap
{
  uint8_t name[6];              // 6bit encoded 8 char string "a-zA-Z0-9._" leading '.'s discarded
  sector_t first_sector;        // first sector address of file
  uint16_t first_offset;        // offset in the first sector
  uint16_t reserved;
  uint32_t type_len;            // type and length combined: MSB is type 3 LSB are length
};

static_assert( (sizeof(uint32_t) % ZEROFS_SUPER_WRITE_GRANULARITY) == 0, "type_len field of struct zerofs_namemap not matching to ZEROFS_SUPER_WRITE_GRANULARITY, choose a larger type!");
static_assert( (sizeof(struct zerofs_namemap) % ZEROFS_SUPER_WRITE_GRANULARITY) == 0, "struct zerofs_namemap not matching to ZEROFS_SUPER_WRITE_GRANULARITY, adjust the size with padding!");
static_assert(ZEROFS_SUPER_WRITE_GRANULARITY<=sizeof(struct zerofs_namemap), "Superblock flash write granularity shouldn't be larger than sizeof(struct zerofs_namemap)");
static_assert( (ZEROFS_NUMBER_OF_SECTORS % ZEROFS_SUPER_WRITE_GRANULARITY) == 0, "sector_map size is not matching to ZEROFS_SUPER_WRITE_GRANULARITY, add some padding bytes!");

#define ZEROFS_NM_GET_TYPE(nm) ((nm)->type_len>>24)
#define ZEROFS_NM_GET_SIZE(nm) ((nm)->type_len&0xffffff)

static_assert(sizeof(struct zerofs_namemap)==16, "struct zerofs_namemap length should be 16");

struct zerofs_metadata
{
  uint16_t last_written;			              // last written block
  uint16_t last_written_len;		                      // length of the last written block or 0 if no data
  uint16_t version;                                           // choose the smaller on boot
  uint16_t padding;
};

static_assert( (sizeof(struct zerofs_metadata) % ZEROFS_SUPER_WRITE_GRANULARITY) == 0, "struct zerofs_metadata not matching to ZEROFS_SUPER_WRITE_GRANULARITY, adjust the size with padding!");

// superblock on flash
// must be mapped to RAM, fields cannot be written directly
struct zerofs_superblock
{
  uint8_t sector_map[ZEROFS_NUMBER_OF_SECTORS];               // block map each block has a file or free/partial/erased
  struct zerofs_namemap namemap[ZEROFS_MAX_NUMBER_OF_FILES];  // from 1
  struct zerofs_metadata meta;
};

// RAM instance of zerofs
struct zerofs
{
  const struct zerofs_superblock *superblock; 	// read only struct in flash
  uint8_t *sector_map;				// sector_map when read/write mode enabled (RAM)
  uint8_t last_namemap_id;			// on boot look for the last non-FF namemap entry
  uint8_t bank;
  uint8_t background;                           // TODO: combine this and bank to a flags field
#if (ZEROFS_VERIFY!=0)
  uint8_t verify;
  uint8_t verify_cnt;
#endif
  struct zerofs_metadata meta;
  const struct zerofs_flash_access *fls;	// flash access struct in rom
  sector_t erased_max;
};

static_assert(sizeof(struct zerofs_superblock)<=ZEROFS_FLASH_SECTOR_SIZE, "Superblock too large, reduce ZEROFS_MAX_NUMBER_OF_FILES!");

#define ZEROFS_FILE_NOMORE (1<<0)

struct zerofs_file
{
  struct zerofs *zfs;
  uint8_t id;
  enum zerofs_mode mode;
  sector_t sector;
  uint16_t pos;
  uint8_t type;
  uint32_t size;
  uint8_t flags;
};

#define zerofs_file_len(fp, lenp) ((*(lenp))=(fp)->size)

#endif


#ifdef ZEROFS_IMPLEMENTATION


int zerofs_init(struct zerofs *zfs, const struct zerofs_flash_access *fls_acc)
{
  int i;

  if(NULL==zfs||NULL==fls_acc) return(ZEROFS_ERR_ARG);

  memset(zfs, 0, sizeof(struct zerofs));
  zfs->bank=0;
  zfs->fls=fls_acc;
  zfs->superblock=(const struct zerofs_superblock *)(zfs->fls->superblock_banks + (zfs->bank*ZEROFS_SUPER_SECTOR_SIZE));
#if (ZEROFS_VERIFY!=0)
  zfs->verify_cnt=zfs->verify=ZEROFS_VERIFY;
#endif
  zfs->sector_map=NULL;
  zfs->meta.last_written=zfs->superblock->meta.last_written;
  zfs->meta.last_written_len=zfs->superblock->meta.last_written_len;
  zfs->meta.version=zfs->superblock->meta.version;
  zfs->last_namemap_id=0;
  zfs->erased_max=0;
  for(i=0;i<ZEROFS_MAX_NUMBER_OF_FILES;i++) if(zfs->superblock->namemap[i].type_len!=0&&zfs->superblock->namemap[i].type_len!=0xffffffff) zfs->last_namemap_id=i+1;

  return(0);
}

// is zerofs in read only mode?
int zerofs_is_readonly_mode(struct zerofs *zfs)
{
  if(NULL==zfs) return(ZEROFS_ERR_ARG);
  return(NULL==zfs->sector_map);
}

// copy superblock to the secondary flash sector and 
// update the sector_map and compact the namemap entries
static void zerofs_repack_superblock(struct zerofs *zfs)
{
  static const char zero[6];
  struct zerofs_namemap nm;
  uint32_t addr;
  int id,of,j,ni,valid;

  if(NULL==zfs||zerofs_is_readonly_mode(zfs)) return;
  int nb=zfs->bank^1;
  // erase secondary superblock sector
  zfs->fls->fls_erase(zfs->fls->super_ud, nb*ZEROFS_SUPER_SECTOR_SIZE, ZEROFS_SUPER_SECTOR_SIZE, 0);
  // program the namemap and skip the deleted items
  addr = offsetof(struct zerofs_superblock, namemap);
  for(of=id=ni=0;id<=zfs->last_namemap_id;id++)
  {
    valid=1;
    if(memcmp(&zfs->superblock->namemap[id].name, zero, sizeof(zero))==0) valid=0;
    else if(ZEROFS_NM_GET_SIZE(&zfs->superblock->namemap[id])==0) valid=0;
    else if(zfs->superblock->namemap[id].type_len==0xffffffff) valid=0;
    if(!valid)
    {
      // id 'id' deleted, decrement all larger ids in sector_map
      for(j=0;j<ZEROFS_NUMBER_OF_SECTORS;j++) if(zfs->sector_map[j]<ZEROFS_MAP_BAD&&zfs->sector_map[j]>(id-of)) --zfs->sector_map[j];
      of++;
    }
    else
    {
      // program the name entry
      memcpy(&nm, &zfs->superblock->namemap[id], sizeof(struct zerofs_namemap));
      zfs->fls->fls_write(zfs->fls->super_ud, addr+(nb*ZEROFS_SUPER_SECTOR_SIZE), (uint8_t *)&nm, sizeof(struct zerofs_namemap));
      addr+=sizeof(struct zerofs_namemap);
      ni++;
    }
  }
  // update the free slot for the next namemap entry
  zfs->last_namemap_id=ni;
  // program the updated RAM sector map
  zfs->fls->fls_write(zfs->fls->super_ud, nb*ZEROFS_SUPER_SECTOR_SIZE, zfs->sector_map, sizeof(zfs->superblock->sector_map));
  // copy the metadata fields from RAM if present
  zfs->meta.version--;
  if(zfs->meta.version==0) zfs->meta.version=ZEROFS_SUPERBLOCK_VERSION_MAX;
  addr=offsetof(struct zerofs_superblock, meta);
  zfs->fls->fls_write(zfs->fls->super_ud, addr+(nb*ZEROFS_SUPER_SECTOR_SIZE), (uint8_t *)&zfs->meta, sizeof(struct zerofs_metadata) );
  // if we reset the version, we erase the other superblock too
  if(zfs->meta.version==ZEROFS_SUPERBLOCK_VERSION_MAX) zfs->fls->fls_erase(zfs->fls->super_ud, zfs->bank*ZEROFS_SUPER_SECTOR_SIZE, ZEROFS_SUPER_SECTOR_SIZE, 0);
  zfs->bank=nb;
  zfs->superblock=(const struct zerofs_superblock *)(zfs->fls->superblock_banks + (zfs->bank*ZEROFS_SUPER_SECTOR_SIZE));
}

int zerofs_readonly_mode(struct zerofs *zfs, uint8_t *sector_map)
{
  if(NULL==zfs) return(ZEROFS_ERR_ARG);
  
  if(NULL==sector_map&&NULL!=zfs->sector_map)
  {
    // SET READ MODE
    zerofs_repack_superblock(zfs);
  }
  zfs->sector_map=sector_map;
  if(NULL!=sector_map)
  {
    // SET WRITE MODE
    memcpy(sector_map, zfs->superblock->sector_map, sizeof(zfs->superblock->sector_map));
    uint8_t *sm=sector_map;
    // mark all background erased sectors erased
    for(int i=0; i<zfs->erased_max; i++) if(sm[ZEROFS_BLOCK(zfs, i)]==ZEROFS_MAP_EMPTY) sm[ZEROFS_BLOCK(zfs, i)]=ZEROFS_MAP_ERASED;
    zfs->erased_max=0;
  }
  
  return(0);
}

int zerofs_format(struct zerofs *zfs)
{
  if(NULL==zfs) return(ZEROFS_ERR_ARG);

  zfs->sector_map=NULL;
  zfs->meta.last_written=0;
  zfs->meta.last_written_len=0;
  zfs->last_namemap_id=0;
  zfs->fls->fls_erase(zfs->fls->super_ud, 0, ZEROFS_SUPER_SECTOR_SIZE, 0);
  zfs->fls->fls_erase(zfs->fls->super_ud, ZEROFS_SUPER_SECTOR_SIZE, ZEROFS_SUPER_SECTOR_SIZE, 0);

  return(0);
}

// helper to convert a char to the 6bit encoded version
static inline uint8_t str6bit(char a)
{
  if(a>='a'&&a<='z') return(2+a-'a');
  if(a>='A'&&a<='Z') return(2+26+a-'A');
  if(a>='0'&&a<='9') return(2+2*26+a-'0');
  if(a=='-') return(1);
  return(0);
}

static inline int zerofs_get_type(const char *extension)
{
  int ret=ZEROFS_TYPE_UNKNOWN;
  int i;

  assert(extension);
  
  for(i=0; NULL!=zerofs_extensions[i]; i++)
  {
    if(zerofs_extensions[i][0]>extension[0]) break;
    if(zerofs_extensions[i][0]==extension[0])
    {
      if(zerofs_extensions[i][1]==extension[1] && zerofs_extensions[i][2]==extension[2]) { ret=i; break; }
    }
  }

  return(ret);
}

// code/decode name
// if str is empty, decode the encoded name to str
// otherwise encode str to encoded array
// length is fixed 8 char and 6 byte encoded
static int zerofs_name_codec(char *str, uint8_t *encoded, uint8_t *type)
{
  static const char hexdigits[]="_-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; // 64 char

  assert(str&&encoded);

  if(str[0]=='\0')
  {
    // decode
    str[0]=hexdigits[encoded[0]&0x3f];
    str[1]=hexdigits[encoded[1]&0x3f];
    str[2]=hexdigits[encoded[2]&0x3f];
    str[3]=hexdigits[ (encoded[0]&0xc0)>>2 | (encoded[1]&0xc0)>>4 | (encoded[2]&0xc0)>>6 ];
    str[4]=hexdigits[encoded[3]&0x3f];
    str[5]=hexdigits[encoded[4]&0x3f];
    str[6]=hexdigits[encoded[5]&0x3f];
    str[7]=hexdigits[ (encoded[3]&0xc0)>>2 | (encoded[4]&0xc0)>>4 | (encoded[5]&0xc0)>>6 ];
    str[8]='\0';
  }
  else
  {
    // encode
    uint8_t x;
    char n[8];
    int j,i;
    for(i=0;str[i]!='.'&&str[i]!='\0';i++);  // find '.'
    if(str[i]=='\0'||i>8) return(ZEROFS_ERR_INVALIDNAME);
    if(NULL!=type) *type=zerofs_get_type(&str[i+1]);
    for(j=7,--i;i>=0;--i) n[j--]=str[i]; // copy basename to n[]
    for(;j>=0;--j) n[j]='_'; // fill rest of n[] with '_'
    x=str6bit(n[3]);
    encoded[0]=str6bit(n[0]) | ((x&0x30)<<2);
    encoded[1]=str6bit(n[1]) | ((x&0x0c)<<4);
    encoded[2]=str6bit(n[2]) | ((x&0x03)<<6);
    x=str6bit(n[7]);
    encoded[3]=str6bit(n[4]) | ((x&0x30)<<2);
    encoded[4]=str6bit(n[5]) | ((x&0x0c)<<4);
    encoded[5]=str6bit(n[6]) | ((x&0x03)<<6);
  }
  return(0);
}

// look for available sector for data
static int zerofs_find_free_block(struct zerofs *zfs)
{
  int ret=-1;
  int i,fre=-1;
  const uint8_t *sm;
  sector_t sec;

  assert(zfs);

  sm=ZEROFS_SECTOR_MAP(zfs);
  for(i=0;i<ZEROFS_NUMBER_OF_SECTORS;i++)
  {
    sec=ZEROFS_BLOCK(zfs, i);
    if(sm[sec]==ZEROFS_MAP_ERASED) break;
    if(fre<0&&sm[sec]==ZEROFS_MAP_EMPTY) fre=sec;
  }
  if(i<ZEROFS_NUMBER_OF_SECTORS) ret=sec;
  else if(fre>=0) ret=fre;
  
  return(ret);
}

// look for a specific type of sector
static int zerofs_find_sector_type(struct zerofs *zfs, sector_t from, uint8_t type)
{
  int ret;
  int i;
  const uint8_t *sm;

  assert(zfs);

  sm=ZEROFS_SECTOR_MAP(zfs);
  for(i=1;i<ZEROFS_NUMBER_OF_SECTORS;i++)
  {
    if(sm[(from+i)%ZEROFS_NUMBER_OF_SECTORS]==type) break;
  }
  ret=(from+i)%ZEROFS_NUMBER_OF_SECTORS;
  if(sm[ret]!=type) ret=-1;

  return(ret);
}

// look for the given name in the namemap (ignores other field in nm)
static uint8_t zerofs_namemap_find_name(struct zerofs *zfs, struct zerofs_namemap *nm, uint8_t type)
{
  uint8_t ret=ZEROFS_MAP_EMPTY;
  uint8_t *p;
  unsigned long offs;

  if(NULL==zfs||NULL==nm) return(ret);

  p=(uint8_t *)zfs->superblock->namemap;
  do
  {
    p=memmem(p, sizeof(zfs->superblock->namemap), nm->name, sizeof(nm->name));
    offs=(unsigned long)(p-(uint8_t *)zfs->superblock->namemap);
    ret=(uint8_t)(offs / sizeof(struct zerofs_namemap));
  } while(p!=NULL && ZEROFS_NM_GET_TYPE(&nm[ret])!=type && ( (offs % sizeof(struct zerofs_namemap)) != 0));
  if(p==NULL) ret=ZEROFS_MAP_EMPTY;

  return(ret);
}

// look for the given first_sector in the namemap (ignores other field in nm)
static uint8_t zerofs_namemap_find_sector(struct zerofs *zfs, sector_t first)
{
  uint8_t ret=ZEROFS_MAP_EMPTY;
  int i;
  
  if(NULL==zfs) return(ret);
  
  for(i=0;i<zfs->last_namemap_id;i++) if(zfs->superblock->namemap[i].type_len!=0&&first==zfs->superblock->namemap[i].first_sector) break;
  if(i<zfs->last_namemap_id) ret=i;

  return(ret);
}

/*
struct zerofs_fp *zerofs_open(const char *name);                                        - RO read only open of a file
  1. search for the name in namemap
  2. we have the id and the beginning sector as well as the length
  3. get the MAP[] value for the id with zerofs_super_get_map() -> found_id
    if it is a) SHARED - fp->pos = first two bytes of the sector
             b) == id  - fp->pos = 0
             c) else   - calculate the last sector part of the file here
                         => zerofs_super_lookup_id(found_id)->size % 4096 => fp->pos
  4. fp->sector=beginning sector
     fp->mode=RO
     fp->id=id
*/
int zerofs_open(struct zerofs *zfs, struct zerofs_file *fp, const char *name)
{
  int ret=ZEROFS_ERR_OPEN;
  int id;
  struct zerofs_namemap nm;
  sector_t sc;
  uint16_t of;
  uint8_t type;
  
  if(NULL==zfs||NULL==fp||NULL==name) return(ZEROFS_ERR_ARG);

  memset(fp, 0, sizeof(struct zerofs_file));
  fp->zfs=zfs;
  // 1.
  ret=zerofs_name_codec((char *)name, nm.name, &type);
  if(0==ret)
  {
    nm.type_len=type<<24;
    id=zerofs_namemap_find_name(zfs, &nm, type);
    // 2.
    if(id!=ZEROFS_MAP_EMPTY)
    {
      // 3.
      sc=zfs->superblock->namemap[id].first_sector;
      of=zfs->superblock->namemap[id].first_offset;
      fp->pos=of;
      // 4.
      fp->sector=sc;
      fp->id=id;
      fp->mode=ZEROFS_MODE_READ_ONLY;
      fp->size=ZEROFS_NM_GET_SIZE(&zfs->superblock->namemap[id]);
      fp->type=ZEROFS_NM_GET_TYPE(&zfs->superblock->namemap[id]);
      ret=0;
    }
    else ret=ZEROFS_ERR_NOTFOUND;
  }

  return(ret);
}

// find an available name slot
// if the last one is not available anymore, call
// zerofs_repack_superblock() and try to find one
// that way
// return -1 if no more available
static int zerofs_namemap_find_slot(struct zerofs *zfs)
{
  int ret=-1;

  if(!zerofs_is_readonly_mode(zfs))
  {
    ret=zfs->last_namemap_id;
    ++zfs->last_namemap_id;
    if(zfs->last_namemap_id>=ZEROFS_MAX_NUMBER_OF_FILES)
    {
      zerofs_repack_superblock(zfs);
      ret=zfs->last_namemap_id;
      if(zfs->last_namemap_id>=ZEROFS_MAX_NUMBER_OF_FILES) ret=-1;
    }
  }
  
  return(ret);
}

/*
int zerofs_fs_unlink(const char *name);                                                 - delete a file
  1. lookup the name with zerofs_super_lookup_name() if not found -> ERROR FILE NOT FOUND
  2. we have id, size, beginning available
  3. if the MAP entry == id change this to EMPTY get_map/set_map
     otherwise leave it alone
  4. delete record from name_map (set first char to 0)
  5. if last_name_id was invalid, set it to id (<-- skip this step, we cannot write in the middle of namemap)
  6. look for all of this ids from MAP and set all of them to EMPTY
  7. check the last sector, it maybe the beginning of another file:
     if there is one:
     a) look for an ERASED / EMPTY sector just like we did in creat/4/b if ERASED not found but there is EMPTY we should erase it.
     b) write the offset to the beginning of the sector (the remaining length of the file) <-- not here, 7.f should write it to "last"
     c) copy all the bytes from the end of file to the end of sector to the new sector
     d) set last sector in MAP to EMPTY
     e) set the beginning sector of the found entry to the newly allocated sector <-- namemap is RO flash, we need to restore original sector map
     f) do 7.b. here to 'last' <-- no need
*/
static int zerofs_delete_by_id(struct zerofs *zfs, int id)
{
  int ret=0;
  uint8_t *sm;
  sector_t sec;
  int i,last;
  static const struct zerofs_namemap zero;

  // 1. in zerofs_delete()
  if(id!=ZEROFS_MAP_EMPTY)
  {
    // 3.
    sm=(uint8_t *)ZEROFS_SECTOR_MAP(zfs); // valid because we are in write mode, superblock is in RAM
    sector_t from=zfs->superblock->namemap[id].first_sector;
    // 4.
    uint32_t addr = (id*(sizeof(struct zerofs_namemap))) + offsetof(struct zerofs_superblock, namemap);
    zfs->fls->fls_write(zfs->fls->super_ud, addr+(zfs->bank*ZEROFS_SUPER_SECTOR_SIZE), ((uint8_t *)&zero), sizeof(zero));
    // 6.
    for(last=-1,i=0;i<ZEROFS_NUMBER_OF_SECTORS;i++)
    {
      sec=(i+from)%ZEROFS_NUMBER_OF_SECTORS;
      if(sm[sec]==id)
      {
        last=sec;
        sm[sec]=ZEROFS_MAP_EMPTY;
      }
    }
    // 7.
    if(last>=0)
    {
      sm[last]=zerofs_namemap_find_sector(zfs, last);
    }
  }
  else ret=ZEROFS_ERR_NOTFOUND;
  
  return(ret);
}

int zerofs_delete(struct zerofs *zfs, const char *name)
{
  int ret=0;
  struct zerofs_namemap nm;
  int id;
  uint8_t type;

  if(NULL==zfs||NULL==name) return(ZEROFS_ERR_ARG);

  if(!zerofs_is_readonly_mode(zfs))
  {
    // 1.
    ret=zerofs_name_codec((char *)name, nm.name, &type);
    if(0==ret)
    {
      nm.type_len=type<<24;
      id=zerofs_namemap_find_name(zfs, &nm, type);
      ret=zerofs_delete_by_id(zfs, id);
    }
  }
  else ret=ZEROFS_ERR_READMODE;
  
  return(ret);
}

/*
struct zerofs_fs *zerofs_create(const char *name);                                     - WO write only creation of a file
  0. check if file exists and delete if it is
  1. look for empty record at the end of the namemap, this is most likely the last_name_id if valid.
     if not valid, look from the index 0 and look for record where name[0]='\0' -> this case triggers superblock sync at the
     end of the operation and set last_name_id when records compacted (not supported yet)
  2. increment last_name_id
  3. fill name in the new record
  4. look for a beginning sector:
     a) if last_written is valid and last_w_length < 4096 
        than beginning = last_written and fp->pos=last_w_length
     b) if not valid or missing; look for available sector in MAP with zerofs_super_get_map()
        find i) ERASED or if none available ii) EMPTY --> if none of them available -> ERROR-DISK-FULL
        fp->pos=0
  5. if beginning sector EMPTY -> erase it with flash_erase()    
  6. fill the namemap record with the beginning sector 
  7. fp->id = id
     fp->sector = beginning
     fp->mode = WO
*/
int zerofs_create(struct zerofs *zfs, struct zerofs_file *fp, const char *name)
{
  int ret=0;
  struct zerofs_namemap nm;

  if(NULL==zfs||NULL==fp||NULL==name) return(ZEROFS_ERR_ARG);

  if(!zerofs_is_readonly_mode(zfs))
  {
    // 0 delete old file here
    zerofs_delete(zfs, name);
    memset(fp, 0, sizeof(struct zerofs_file));
    fp->zfs=zfs;
    // 1
    int id=zerofs_namemap_find_slot(zfs);
    if(id>=0)
    {
      // 2 <-- handled in zerofs_namemap_find_slot()
      // 3
      ret=zerofs_name_codec((char *)name, nm.name, &fp->type);
      if(0==ret)
      {
        // 4
        if(zfs->meta.last_written_len>0 && zfs->meta.last_written_len<ZEROFS_FLASH_SECTOR_SIZE)
        {
          // 4.0 set nomore flag to prevent multiple starter files in the same sector
          fp->flags|=ZEROFS_FILE_NOMORE;
          // 4.a)
          nm.first_sector=fp->sector=zfs->meta.last_written;
          nm.first_offset=fp->pos=zfs->meta.last_written_len;
        }
        else
        {
          // 4.b)
          int s=zerofs_find_free_block(zfs); //HURKA
          if(s>=0)
          {
            nm.first_sector=fp->sector=(uint16_t)s;
            nm.first_offset=0;
            fp->pos=0;
          }
          else ret=ZEROFS_ERR_NOSPACE;
        }
      }
      if(ret==0)
      {
        // 5.
        uint8_t *sm=(uint8_t *)ZEROFS_SECTOR_MAP(zfs);
        if(ZEROFS_MAP_EMPTY==sm[fp->sector])
        {
          zfs->fls->fls_erase(zfs->fls->data_ud, fp->sector*ZEROFS_FLASH_SECTOR_SIZE, ZEROFS_FLASH_SECTOR_SIZE, 0);
          sm[fp->sector]=ZEROFS_MAP_ERASED;
        }
        if(ZEROFS_MAP_ERASED==sm[fp->sector]) sm[fp->sector]=id;
        // 6. write name and first_sector/offset only
        nm.type_len=~0;
        zfs->fls->fls_write(zfs->fls->super_ud, (zfs->bank*ZEROFS_SUPER_SECTOR_SIZE) + ((id)*(sizeof(struct zerofs_namemap))) + offsetof(struct zerofs_superblock, namemap), (uint8_t *)&nm, sizeof(struct zerofs_namemap));
        // 7.
        fp->id=id;
        fp->mode=ZEROFS_MODE_WRITE_ONLY;
      }
    }
    else ret=ZEROFS_ERR_MAXFILES;
  }
  else ret=ZEROFS_ERR_READMODE;
  
  return(ret);
}

// closes the file after write
int zerofs_close(struct zerofs_file *fp)
{
  int ret=0;
  struct zerofs *zfs;

  if(NULL==fp) return(ZEROFS_ERR_ARG);

  zfs=fp->zfs;
  if(NULL==zfs) return(ZEROFS_ERR_INVALIDFP);
  if(ZEROFS_MODE_WRITE_ONLY==fp->mode)
  {
    uint32_t type_len=(fp->type<<24) | fp->size;
    uint16_t addr=((fp->id)*(sizeof(struct zerofs_namemap))) + offsetof(struct zerofs_superblock, namemap) + offsetof(struct zerofs_namemap, type_len);

    zfs->fls->fls_write(zfs->fls->super_ud, (zfs->bank*ZEROFS_SUPER_SECTOR_SIZE) + addr, (uint8_t *)&type_len, sizeof(type_len));
    #if 0
    if( (fp->flags&ZEROFS_FILE_NOMORE)!=0 ) zfs->meta.last_written_len=0;
    #endif
  }
  fp->mode=ZEROFS_MODE_CLOSED;

  return(ret);
}

/*
int32_t zerofs_fs_read(struct zerofs_fp *fp, uint8_t *buf, uint32_t len);               - reads from a RO file
  1. read the bytes from fp->sector at offset fp->pos
  2. if reached end of sector
     look for next sector increment sector until MAP [sector] will not be fp->id again
     if overflow, start from 0
*/
int zerofs_read(struct zerofs_file *fp, uint8_t *buf, uint32_t len)
{
  int ret=0;
  int l;
  struct zerofs *zfs;
  
  if(NULL==fp||NULL==buf) return(ZEROFS_ERR_ARG);
  
  zfs=fp->zfs;
  while(len>0)
  {
    l=MIN((int)len, (ZEROFS_FLASH_SECTOR_SIZE-fp->pos));
    zfs->fls->fls_read(zfs->fls->data_ud, fp->sector*ZEROFS_FLASH_SECTOR_SIZE+fp->pos, buf, l);
    len-=l;
    buf+=l;
    fp->pos+=l;
    ret+=l;
    if(fp->pos>=ZEROFS_FLASH_SECTOR_SIZE)
    {
      fp->sector=zerofs_find_sector_type(fp->zfs, fp->sector, fp->id);
      fp->pos=0;
    }
  }
  
  return(ret);
}

/*
 * seek in read mode, pos negative: pos from the end, positive: from the beginning
 */
int zerofs_seek(struct zerofs_file *fp, int32_t pos)
{
  int ret=0;
  int i;
  const uint8_t *sm;
  int32_t dec;
  sector_t sec;
  uint16_t first_block_fill;

  if(NULL==fp) return(ZEROFS_ERR_ARG);

  if(zerofs_is_readonly_mode(fp->zfs))
  {
    if(ABS(pos)<fp->size)
    {
      pos=( pos>=0 ? pos : fp->size+pos);
      first_block_fill=ZEROFS_FLASH_SECTOR_SIZE-fp->zfs->superblock->namemap[fp->id].first_offset;
      sec=fp->zfs->superblock->namemap[fp->id].first_sector;
      if(pos > first_block_fill)
      {
        sm=ZEROFS_SECTOR_MAP(fp->zfs);
        dec=first_block_fill;
        do
        {
          for(i=0; fp->id!=sm[(i+sec)%ZEROFS_NUMBER_OF_SECTORS] && i<ZEROFS_NUMBER_OF_SECTORS; i++);
          if(i>=ZEROFS_NUMBER_OF_SECTORS) { ret=ZEROFS_ERR_OVERFLOW; break; }
          sec=(i+sec)%ZEROFS_NUMBER_OF_SECTORS;
          pos-=dec;
          dec=ZEROFS_FLASH_SECTOR_SIZE;
        } while(pos>=ZEROFS_FLASH_SECTOR_SIZE);
        if(0==ret)
        {
          fp->sector=sec;
          fp->pos=pos;
        }
      }
      else
      {
        fp->sector=sec;
        fp->pos=pos;
      }
    }
    else ret=ZEROFS_ERR_ARG;
  }
  else ret=ZEROFS_ERR_WRITEMODE;

  return(ret);
}

int zerofs_append(struct zerofs *zfs, struct zerofs_file *fp, const char *name)
{
  int ret=0;
  struct zerofs_namemap nm;
  int id,ni;
  int pos,dec;
  int i;
  uint8_t *sm;
  sector_t sec;
  static const uint8_t buf[8]={0,0,0,0,0,0,0,0};

  if(NULL==zfs||NULL==fp||NULL==name) return(ZEROFS_ERR_ARG);

  if(!zerofs_is_readonly_mode(zfs))
  {
    memset(fp, 0, sizeof(struct zerofs_file));
    fp->zfs=zfs;
    ret=zerofs_name_codec((char *)name, nm.name, &fp->type);
    if(0==ret)
    {
      nm.type_len=fp->type<<24;
      id=zerofs_namemap_find_name(zfs, &nm, fp->type);
      if(ZEROFS_MAP_EMPTY!=id)
      {
        // find a new name slot
        ni=zerofs_namemap_find_slot(zfs);
        if(ni>=0)
        {
          sm=(uint8_t *)ZEROFS_SECTOR_MAP(fp->zfs);
          // copy existing namemap entry
          nm.first_sector=zfs->superblock->namemap[id].first_sector;
          nm.first_offset=zfs->superblock->namemap[id].first_offset;
          nm.type_len=zfs->superblock->namemap[id].type_len;
          // set size
          fp->size=ZEROFS_NM_GET_SIZE(&zfs->superblock->namemap[id]);
          // set pos
          fp->pos=(fp->size+nm.first_offset) % ZEROFS_FLASH_SECTOR_SIZE;
          // search the last sector
          pos=fp->size;
          dec=nm.first_offset;
          sec=nm.first_sector;
          do
          {
            for(i=0; id!=sm[(i+sec)%ZEROFS_FLASH_SECTOR_SIZE] && i<ZEROFS_NUMBER_OF_SECTORS; i++);
            if(i>=ZEROFS_NUMBER_OF_SECTORS) { ret=ZEROFS_ERR_OVERFLOW; break; }
            sec=(i+sec)%ZEROFS_FLASH_SECTOR_SIZE;
            pos-=dec;
            dec=ZEROFS_FLASH_SECTOR_SIZE;
          } while(pos>0);
          // allocate new sector if needed
          fp->sector=sec;
          if(0==fp->pos)
          {
            int s=zerofs_find_free_block(zfs); //HURKA
            if(s>=0)
            {
              fp->sector=(uint16_t)s;
              sm[s]=ni;
            }
            else ret=ZEROFS_ERR_NOSPACE;
          }
          if(0==ret)
          {
            // rename id in map
            for(i=0;i<ZEROFS_NUMBER_OF_SECTORS;i++) if(sm[i]==id) sm[i]=ni;
            // flash new namemap entry
            nm.type_len=~0;
            zfs->fls->fls_write(zfs->fls->super_ud, (zfs->bank*ZEROFS_SUPER_SECTOR_SIZE) + ((ni)*(sizeof(struct zerofs_namemap))) + offsetof(struct zerofs_superblock, namemap), (uint8_t *)&nm, sizeof(struct zerofs_namemap) );
            // delete old namemap entry
            uint32_t addr = (id*(sizeof(struct zerofs_namemap))) + offsetof(struct zerofs_superblock, namemap);
            zfs->fls->fls_write(zfs->fls->super_ud, addr+(zfs->bank*ZEROFS_SUPER_SECTOR_SIZE), buf, sizeof(buf));
            // set new id in opened fp
            fp->id=ni;
            fp->mode=ZEROFS_MODE_WRITE_ONLY;
          }
        }
        else ret=ZEROFS_ERR_MAXFILES;
      }
      else ret=ZEROFS_ERR_NOTFOUND;
    }
  }
  else ret=ZEROFS_ERR_READMODE;

  return(ret);
}

// https://github.com/hdtodd/CRC8-Library/blob/f81864daa56028d689d501dbc96d5aad98b7abdc/libcrc8.c#L114C1-L117C3
#if (ZEROFS_VERIFY!=0)
static uint8_t zerofs_crc8(uint8_t *msg, int len, uint8_t init)
{
  static const uint8_t CRC8Table[256] =
  {
    0x00, 0x97, 0xb9, 0x2e, 0xe5, 0x72, 0x5c, 0xcb, 0x5d, 0xca, 0xe4, 0x73, 0xb8, 0x2f, 0x01, 0x96, 0xba, 0x2d, 0x03, 0x94, 0x5f, 0xc8, 0xe6, 0x71, 
    0xe7, 0x70, 0x5e, 0xc9, 0x02, 0x95, 0xbb, 0x2c, 0xe3, 0x74, 0x5a, 0xcd, 0x06, 0x91, 0xbf, 0x28, 0xbe, 0x29, 0x07, 0x90, 0x5b, 0xcc, 0xe2, 0x75, 
    0x59, 0xce, 0xe0, 0x77, 0xbc, 0x2b, 0x05, 0x92, 0x04, 0x93, 0xbd, 0x2a, 0xe1, 0x76, 0x58, 0xcf, 0x51, 0xc6, 0xe8, 0x7f, 0xb4, 0x23, 0x0d, 0x9a, 
    0x0c, 0x9b, 0xb5, 0x22, 0xe9, 0x7e, 0x50, 0xc7, 0xeb, 0x7c, 0x52, 0xc5, 0x0e, 0x99, 0xb7, 0x20, 0xb6, 0x21, 0x0f, 0x98, 0x53, 0xc4, 0xea, 0x7d, 
    0xb2, 0x25, 0x0b, 0x9c, 0x57, 0xc0, 0xee, 0x79, 0xef, 0x78, 0x56, 0xc1, 0x0a, 0x9d, 0xb3, 0x24, 0x08, 0x9f, 0xb1, 0x26, 0xed, 0x7a, 0x54, 0xc3, 
    0x55, 0xc2, 0xec, 0x7b, 0xb0, 0x27, 0x09, 0x9e, 0xa2, 0x35, 0x1b, 0x8c, 0x47, 0xd0, 0xfe, 0x69, 0xff, 0x68, 0x46, 0xd1, 0x1a, 0x8d, 0xa3, 0x34, 
    0x18, 0x8f, 0xa1, 0x36, 0xfd, 0x6a, 0x44, 0xd3, 0x45, 0xd2, 0xfc, 0x6b, 0xa0, 0x37, 0x19, 0x8e, 0x41, 0xd6, 0xf8, 0x6f, 0xa4, 0x33, 0x1d, 0x8a, 
    0x1c, 0x8b, 0xa5, 0x32, 0xf9, 0x6e, 0x40, 0xd7, 0xfb, 0x6c, 0x42, 0xd5, 0x1e, 0x89, 0xa7, 0x30, 0xa6, 0x31, 0x1f, 0x88, 0x43, 0xd4, 0xfa, 0x6d, 
    0xf3, 0x64, 0x4a, 0xdd, 0x16, 0x81, 0xaf, 0x38, 0xae, 0x39, 0x17, 0x80, 0x4b, 0xdc, 0xf2, 0x65, 0x49, 0xde, 0xf0, 0x67, 0xac, 0x3b, 0x15, 0x82, 
    0x14, 0x83, 0xad, 0x3a, 0xf1, 0x66, 0x48, 0xdf, 0x10, 0x87, 0xa9, 0x3e, 0xf5, 0x62, 0x4c, 0xdb, 0x4d, 0xda, 0xf4, 0x63, 0xa8, 0x3f, 0x11, 0x86, 
    0xaa, 0x3d, 0x13, 0x84, 0x4f, 0xd8, 0xf6, 0x61, 0xf7, 0x60, 0x4e, 0xd9, 0x12, 0x85, 0xab, 0x3c
  };
  while(len-->0) init=CRC8Table[(init ^ *msg++)];

  return(init);
};
#endif

/*
int32_t zerofs_fs_write(struct zerofs_fp *fp, const uint8_t *buf, uint32_t len);        - write buffer to WO opened file pointer
  1. write bytes to flash with flash_write() to fp->sector, fp->pos until it is full
  2. in case of sector overflow:
     a) look for available sector with zerofs_super_get_map() search for ERASED or if not found, EMPTY
     b) if no available sector -> ERROR DISK FULL
     c) if the found sector is EMPTY -> erase with flash_erase()
     d) update fp with the new sector and pos=0
     e) update MAP with fp->id -- zerofs_super_set_map(fp->sector, fp->id)
  RETURN: written bytes
*/
int zerofs_write(struct zerofs_file *fp, uint8_t *buf, uint32_t len)
{
  int ret=0;
  int l;
  uint8_t *sm;
  struct zerofs *zfs;

  if(NULL==fp||NULL==buf) return(ZEROFS_ERR_ARG);
  
  zfs=fp->zfs;
  if(!zerofs_is_readonly_mode(zfs))
  {
    // cast away the const, safe because we are in RW mode
    // 1.
    sm=(uint8_t *)ZEROFS_SECTOR_MAP(zfs);
    while(len>0)
    {
      l=MIN(len, (ZEROFS_FLASH_SECTOR_SIZE-fp->pos));
      if(l>0)
      {
        zfs->fls->fls_write(zfs->fls->data_ud, fp->sector*ZEROFS_FLASH_SECTOR_SIZE+fp->pos, buf, l);
#if (ZEROFS_VERIFY!=0)
        if(zfs->verify>0&&--zfs->verify_cnt==0)
        {
          // verify required
          zfs->verify_cnt=zfs->verify;
          uint8_t crc=zerofs_crc8(buf,l,0);
          zfs->fls->fls_read(zfs->fls->data_ud, fp->sector*ZEROFS_FLASH_SECTOR_SIZE+fp->pos, buf, l);
          if(crc!=zerofs_crc8(buf,l,0))
          {
            sm[fp->sector]=ZEROFS_MAP_BAD;
            return(ZEROFS_ERR_BADSECTOR);
          }
        }
#endif
        len-=l;
        buf+=l;
        fp->pos+=l;
        fp->size+=l;
        zfs->meta.last_written=fp->sector;
        zfs->meta.last_written_len=fp->pos;
      }
      // 2.
      if(l==0)
      {
        // 2. remove nomore flag to let new files to start here
        fp->flags&=~ZEROFS_FILE_NOMORE;
        // 2.a.
        int s=zerofs_find_free_block(zfs);
        if(s>=0)
        {
          // 2.d.
          fp->sector=s;
          fp->pos=0;
          // 2.c.
          if(sm[fp->sector]!=ZEROFS_MAP_ERASED) zfs->fls->fls_erase(zfs->fls->data_ud, fp->sector*ZEROFS_FLASH_SECTOR_SIZE, ZEROFS_FLASH_SECTOR_SIZE, 0);
          // 2.e.
          sm[fp->sector]=fp->id;
        }
        // 2.b.
        else
        {
          for(int i=0;i<ZEROFS_NUMBER_OF_SECTORS;i++) if(sm[i]==fp->id) sm[i]=ZEROFS_MAP_EMPTY;
          fp->mode=ZEROFS_MODE_CLOSED;
          ret=ZEROFS_ERR_NOSPACE;
          break;
        }
      }
    }
  }
  else ret=ZEROFS_ERR_READMODE;
  
  return(ret);
}

int zerofs_background_erase(struct zerofs *zfs)
{
  int i;
  const uint8_t *sm;
  sector_t sc;

  if(NULL==zfs) return(ZEROFS_ERR_ARG);

  if(zerofs_is_readonly_mode(zfs))
  {
    sm=ZEROFS_SECTOR_MAP(zfs);
    for(i=zfs->erased_max;i<ZEROFS_NUMBER_OF_SECTORS;i++) if(sm[ZEROFS_BLOCK(zfs, i)]==ZEROFS_MAP_EMPTY) break;
    sc=ZEROFS_BLOCK(zfs, i);
    if(i<ZEROFS_NUMBER_OF_SECTORS && sm[sc]==ZEROFS_MAP_EMPTY)
    {
      zfs->fls->fls_erase(zfs->fls->data_ud, sc*ZEROFS_FLASH_SECTOR_SIZE, ZEROFS_FLASH_SECTOR_SIZE, 1);
      zfs->erased_max=i+1;
    }
  }
  
  return(0);
}

#endif
