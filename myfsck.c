#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include "genhd.h"
#include "ext2_fs.h"

#if defined(__FreeBSD__)
#define lseek64 lseek
#endif

#define SECTOR_SIZE_BYTES     512
#define PARTITION_SIZE_BYTES  16
#define PARTITION_SIZE        1024

extern int64_t lseek64(int, int64_t, int);

static int device;

static struct partition* parArray;

static int    parArrayCounter = 0;

static int extend_base = 0;


/* read_sectors: read a specified number of sectors into a buffer.
 *
 * inputs:
 *   int64 start_sector: the starting sector number to read.
 *                       sector numbering starts with 0.
 *   int numsectors: the number of sectors to read.  must be >= 1.
 *   int device [GLOBAL]: the disk from which to read.
 *
 * outputs:
 *   void *into: the requested number of sectors are copied into here.
 *
 * modifies:
 *   void *into
 */
void read_sectors (int64_t start_sector, unsigned int num_sectors, void *into)
{
    ssize_t ret;
    int64_t lret;
    int64_t sector_offset;
    ssize_t bytes_to_read;


    sector_offset = start_sector * SECTOR_SIZE_BYTES;

    if ((lret = lseek64(device, sector_offset, SEEK_SET)) != sector_offset) {
        fprintf(stderr, "Seek to position %"PRId64" failed: "
                "returned %"PRId64"\n", sector_offset, lret);
        exit(-1);
    }

    bytes_to_read = SECTOR_SIZE_BYTES * num_sectors;

    if ((ret = read(device, into, bytes_to_read)) != bytes_to_read) {
        fprintf(stderr, "Read sector %"PRId64" length %d failed: "
                "returned %"PRId64"\n", start_sector, num_sectors, ret);
        exit(-1);
    }
}



int GetOnePartition (int the_sector, char* buf, int64_t offset) {
//
  memcpy(parArray+parArrayCounter, buf+offset, PARTITION_SIZE_BYTES);


  if (parArray[parArrayCounter].sys_ind == DOS_EXTENDED_PARTITION) {
    // If this partition is an extended one but still the primary partition, 
    // the counter keeps on increment    
    if(parArrayCounter <= 3) {
      extend_base = parArray[parArrayCounter].start_sect;
      parArrayCounter++;  
    } else {
      parArray[parArrayCounter].start_sect = parArray[parArrayCounter].start_sect + extend_base;
    }
    return 1;
  } else {
    parArray[parArrayCounter].start_sect = parArray[parArrayCounter].start_sect + the_sector;
    if(parArray[parArrayCounter].sys_ind != 0x00 || parArrayCounter <= 3 )
      parArrayCounter++;
    return 0;
  }
}


// Assume each sector only has one partition that could be extended
void GetAllPartitons (char* diskname) {
  unsigned char buf[SECTOR_SIZE_BYTES]; // A buffer with 512 bytes
  int           the_sector = 0;         // Read the first sector to get the four primary partitions  
  int64_t       offset;
  int           extendIndex = 0;
  parArray = (struct partition*)malloc(100 * PARTITION_SIZE_BYTES);
  if ((device = open(diskname, O_RDWR)) == -1) {
    perror("Could not open device file");
    exit(-1);
  }

  // printf("Dumping sector %d:\n", the_sector);
  read_sectors(the_sector, 1, buf);


  /*
   * Get the four primary partitions
   */  
  offset = 446;
  int primary_co = 4;
  while (primary_co != 0) {
    int stat = GetOnePartition(the_sector, buf, offset);
    // printf("!!!!!!!!!%d\n",stat);
    if(stat) {
      // printf("extended partition\n");
      extendIndex = parArrayCounter - 1;
    }
    offset += PARTITION_SIZE_BYTES;
    primary_co --;
  }

  /*
   * Get the extended partitions if existed
   */ 
  while(extendIndex != 0) {
    int logical_co = 2;
    // get the sector to go from extendIndex
    the_sector = parArray[extendIndex].start_sect;

    // reset extendIndex to 0
    extendIndex = 0;

    // read the target sector
    read_sectors(the_sector, 1, buf);

    // start from 446 offset
    offset = 446;
    while(logical_co != 0) {
      int stat = GetOnePartition(the_sector, buf, offset);
      if(stat) {
        // printf("extended partition\n");
        extendIndex = parArrayCounter;
      }
      offset += PARTITION_SIZE_BYTES;
      logical_co --;
    }
  }  
}

/*
 * Get the magic number of a partition
 */
void Get_Magicnumber(int parIndex) {
  const int64_t superblock_offset = 1024;
  unsigned char buf[PARTITION_SIZE];

  // Offset 2 sectors;
  int start_sector = parArray[parIndex-1].start_sect + superblock_offset/SECTOR_SIZE_BYTES;
  read_sectors(start_sector, 2, buf);

  struct ext2_super_block* super_block;
  super_block = malloc(sizeof(struct ext2_super_block));
  memcpy(super_block, buf, sizeof(struct ext2_super_block));

  printf("Magic number of partiton %d: 0x%02x\n", parIndex, super_block->s_magic);

  free(super_block);
}


void Translate_Inode_To_Sector () {

}

void Locate_Inode_In_Bitmap() {

}

void Locate_Root_Inode() {

}

void Read_Directory() {

}

void Print_File_Inode() {

}


void Print_All_partitions() {
    int i;
    for(i = 0; i < parArrayCounter; i++) {
        printf("0x%02X %d %d\n", parArray[i].sys_ind, parArray[i].start_sect,
        parArray[i].nr_sects);
    }  
}



void usage(const char* progname) {
  printf("Usage: %s [options]\n", progname);
  printf("Program Options:\n");
  printf("  -p --print <partition number> -i /path/to/disk/image\n");
  printf("  -f --fix   <partition number> -i /path/to/disk/image\n");
  exit(-1);
}


void main(int argc, char** argv) {
    int opt;
    static struct option long_options[] = {
      {"print", required_argument, 0, 'p'},
      {"fix",   required_argument, 0, 'f'},
      {"input", required_argument, 0, 'i'},
      {0, 0, 0, 0}
    };

    int partition_num = 0;
    while((opt = getopt_long(argc, argv, "i:f:p:", long_options, NULL)) != EOF) {
      switch (opt) {
        case 'p':
          // print the partition table          
          partition_num = atoi(optarg);
          // printf("Print partition: %d\n", partition_num);
          break;
        case 'f':
          // fix the problems
          partition_num = atoi(optarg);
          // printf("fix partition: %d\n", partition_num);
          break;
        case 'i':
          // printf("disk image: '%s'\n", optarg);
          GetAllPartitons(optarg);
          break;
        default:
          usage(argv[0]);          
          break;
      }      
    }



    // if (partition_num > parArrayCounter || partition_num <= 0)
    //   printf("%d\n", -1);
    // else {
    //   printf("0x%02X %d %d\n", parArray[partition_num-1].sys_ind, parArray[partition_num-1].start_sect,
    //   parArray[partition_num-1].nr_sects);      
    // }

    Get_Magicnumber(1);

    free(parArray);



}



