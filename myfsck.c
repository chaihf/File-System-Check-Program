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

#if defined(__FreeBSD__)
#define lseek64 lseek
#endif

#define sector_size_bytes 512
#define PARTITION_SIZE_BYTES 16

extern int64_t lseek64(int, int64_t, int);

static int device;

static struct partition* parArray;

static int    parArrayCounter = 0;


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


    sector_offset = start_sector * sector_size_bytes;

    if ((lret = lseek64(device, sector_offset, SEEK_SET)) != sector_offset) {
        fprintf(stderr, "Seek to position %"PRId64" failed: "
                "returned %"PRId64"\n", sector_offset, lret);
        exit(-1);
    }

    bytes_to_read = sector_size_bytes * num_sectors;

    if ((ret = read(device, into, bytes_to_read)) != bytes_to_read) {
        fprintf(stderr, "Read sector %"PRId64" length %d failed: "
                "returned %"PRId64"\n", start_sector, num_sectors, ret);
        exit(-1);
    }
}


// void parsePartitionSector(char* buf) {
//   const int64_t MBR_offset = 446;
//   int64_t sector_offset;
//   int64_t lret;
//   const ssize_t bytes_to_read = 16;
//   ssize_t ret;
  
//   sector_offset = MBR_offset;


//   if((lret = lseek64(device, sector_offset, SEEK_SET) != sector_offset)) {
//     fprintf(stderr, "Seek to position %"PRId64" failed: 
//             returned %"PRId64"\n", sector_offset, lret);    
//     exit(-1);
//   }
  
//   struct partition* par = malloc(sizeof(char) * 16);
//   if((ret = read(device, par, bytes_to_read)) != bytes_to_read) {
//     fprintf(stderr， "Read  length %d failed:
//             returned %"PRId64"\n", bytes_to_read, ret);
//     exit(-1);
//   }
// }

int GetOnePartition (int the_sector, char* buf, int64_t offset, struct partition* parArray, int parArrayCounter) {
//
  memcpy(parArray+parArrayCounter, buf+offset, PARTITION_SIZE_BYTES);
  
  parArray[parArrayCounter].start_sect = parArray[parArrayCounter].start_sect + the_sector; 
  printf("0x%02X %d %d\n", parArray[parArrayCounter].sys_ind, parArray[parArrayCounter].start_sect,
        parArray[parArrayCounter].nr_sects);

  parArrayCounter++;
  return parArrayCounter;
}


void GetAllPartitons (char* diskname) {
  unsigned char buf[sector_size_bytes]; // A buffer with 512 bytes
  int           the_sector = 0;         // Read the first sector to get the four primary partitions

  int64_t       offset;
  parArray = (struct partition*)malloc(100 * PARTITION_SIZE_BYTES);
  if ((device = open(diskname, O_RDWR)) == -1) {
    perror("Could not open device file");
    exit(-1);
  }

  printf("Dumping sector %d:\n", the_sector);
  read_sectors(the_sector, 1, buf);


  /*
   * Get the four primary partitions
   */
  //Get the first partition
  offset = 446;
  parArrayCounter = GetOnePartition(the_sector, buf, offset, parArray, parArrayCounter);
 
  //Get the second partition
  offset += PARTITION_SIZE_BYTES;
  parArrayCounter = GetOnePartition(the_sector, buf, offset, parArray, parArrayCounter);

  //Get the third partition
  offset += PARTITION_SIZE_BYTES;
  parArrayCounter = GetOnePartition(the_sector, buf, offset, parArray, parArrayCounter);

  //Get the fourth partition
  offset += PARTITION_SIZE_BYTES;
  parArrayCounter = GetOnePartition(the_sector, buf, offset, parArray, parArrayCounter);








  
  
}








void usage(const char* progname) {
  printf("Usage: %s [options]\n", progname);
  printf("Program Options:\n");
  printf("  -p --print <partition number> -i /path/to/disk/image\n");
  printf("  -f --fix   <partition number> -i /path/to/disk/image\n");
}


void main(int argc, char** argv) {
    int opt;
    static struct option long_options[] = {
      {"print", required_argument, 0, 'p'},
      {"fix",   required_argument, 0, 'f'},
      {"input", required_argument, 0, 'i'},
      {0, 0, 0, 0}
    };


    while((opt = getopt_long(argc, argv, "i:f:p:", long_options, NULL)) != EOF) {
      switch (opt) {
        case 0:
          printf("!!!");
          break;
        case 'p':
          // print the partition table
          printf("Print partition: %d\n", atoi(optarg));
          break;
        case 'f':
          // fix the problems
          printf("fix partition: %d\n", atoi(optarg));
          break;
        case 'i':
          printf("disk image: '%s'\n", optarg);
          GetAllPartitons(optarg);
          break;
        default:
          usage(argv[0]);
          break;
      }
    }
}



