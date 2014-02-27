#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/types.h>
#include "genhd.h"
#include "ext2_fs.h"

#if defined(__FreeBSD__)
#define lseek64 lseek
#endif

#define SECTOR_SIZE_BYTES     512
#define PARTITION_SIZE_BYTES  16
#define PARTITION_SIZE        1024
#define SUPERBLOCK_OFFSET     1024
#define SUPERBLOCK_SIZE       1024
#define ROOT_INODE            2
#define BLOCK_GROUP_DESC      32
#define INODE_SIZE            128


void pass1(int parIndex);

void pass2(int parIndex);

void pass3(int parIndex);

extern int64_t lseek64(int, int64_t, int);

static int device;

static struct partition* parArray;

static int    parArrayCounter = 0;

static int    extend_base = 0;

static char*  self_reference = ".";

static char*  parent_reference = "..";

static char*  lost_found = "lost+found";

int BLOCKSIZE = 1024;

int BLOCK_SECTOR_RATIO = 2;

struct inode_location {
  unsigned int sect_num;
  unsigned int offset_within_sect;
};


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

/* write_sectors: write a buffer into a specified number of sectors.
 *
 * inputs:
 *   int64 start_sector: the starting sector number to write.
 *                  sector numbering starts with 0.
 *   int numsectors: the number of sectors to write.  must be >= 1.
 *   void *from: the requested number of sectors are copied from here.
 *
 * outputs:
 *   int device [GLOBAL]: the disk into which to write.
 *
 * modifies:
 *   int device [GLOBAL]
 */
void write_sectors (int64_t start_sector, unsigned int num_sectors, void *from)
{
    ssize_t ret;
    int64_t lret;
    int64_t sector_offset;
    ssize_t bytes_to_write;


    sector_offset = start_sector * SECTOR_SIZE_BYTES;

    if ((lret = lseek64(device, sector_offset, SEEK_SET)) != sector_offset) {
        fprintf(stderr, "Seek to position %"PRId64" failed: "
                "returned %"PRId64"\n", sector_offset, lret);
        exit(-1);
    }

    bytes_to_write = SECTOR_SIZE_BYTES * num_sectors;

    if ((ret = write(device, from, bytes_to_write)) != bytes_to_write) {
        fprintf(stderr, "Write sector %"PRId64" length %d failed: "
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


struct ext2_super_block get_superblock(int parIndex) {
 // Find superblock of this partition to get inode_per_group and block_per_group
  unsigned char buf_superblock[SUPERBLOCK_SIZE];

  // Offset 2 sectors;
  int superblock_start_sector = parArray[parIndex-1].start_sect + SUPERBLOCK_OFFSET/SECTOR_SIZE_BYTES;
  read_sectors(superblock_start_sector, 2, buf_superblock);

  struct ext2_super_block* super_block = (struct ext2_super_block*)buf_superblock;
  // super_block = malloc(sizeof(struct ext2_super_block));
  // memcpy(super_block, buf_superblock, sizeof(struct ext2_super_block));

  // Set the block size everytime read superblock, as block size varies.
  BLOCKSIZE = 1024 << (super_block->s_log_block_size);

  // Set the block size and sector size ratio
  BLOCK_SECTOR_RATIO = BLOCKSIZE / SECTOR_SIZE_BYTES;

  return *super_block;
}

int Get_Inode_Counts(int parIndex) {
  struct ext2_super_block super_block = get_superblock(parIndex);

  return super_block.s_inodes_count;
}
/*
 * Get the magic number of a partition
 */
void Get_Magicnumber(int parIndex) {
  
  struct ext2_super_block super_block = get_superblock(parIndex);

  printf("Magic number of partiton %d: 0x%02x\n", parIndex, super_block.s_magic);

  // printf("inode number: %d\n", super_block->s_inodes_count);
  // printf("block number: %d\n", super_block->s_blocks_count);

  // printf("first block : %d\n", super_block->s_first_data_block);
  // printf("inodes per group: %d\n", super_block->s_inodes_per_group);
  // printf("blocks per group: %d\n", super_block->s_blocks_per_group);
  // // printf("!!belong to group: 0x%02x\n", super_block->s_block_group_nr);

  // // log1024 - 10 = 0
  // printf("block size : %d\n", super_block->s_log_block_size);

  
}


struct ext2_inode Get_Inode(int inodeIndex, int parIndex) {

  struct ext2_super_block super_block = get_superblock(parIndex);
  
  int inodes_per_group = super_block.s_inodes_per_group;

  // Get which block group this inode belongs to
  int block_group = (inodeIndex - 1) / inodes_per_group;
  int local_inode_index = (inodeIndex - 1) % inodes_per_group;

  int64_t blockgroup_offset;

  if(BLOCKSIZE == 1024)
    blockgroup_offset = SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE;

  else
    // The first group descriptor locates in the next block following superblock
    blockgroup_offset = BLOCKSIZE;

  unsigned char blockgroup_buf[BLOCKSIZE];

  // find the start sector for first block group descriptor
  int blockgroup_start_sector = parArray[parIndex-1].start_sect + blockgroup_offset/SECTOR_SIZE_BYTES;
                              
  // Read one block to get all the block group descriptor                            
  read_sectors(blockgroup_start_sector, BLOCK_SECTOR_RATIO, blockgroup_buf);  

  // Find the corresponding group descriptor according to the block_group
  struct ext2_group_desc* group_desc = (struct ext2_group_desc*)(blockgroup_buf + block_group * BLOCK_GROUP_DESC);

  // Find the start sector of inode table
  int inodetable_start_sector = parArray[parIndex-1].start_sect + group_desc->bg_inode_table * BLOCK_SECTOR_RATIO;

  // Find the sector of the target inode based on inode table start sector
  int sect_num = inodetable_start_sector + local_inode_index*INODE_SIZE / SECTOR_SIZE_BYTES;

  // Calculate the offset within the sector
  int offset_within_sect = local_inode_index*INODE_SIZE % SECTOR_SIZE_BYTES;

  // Read the whole target sector
  unsigned char inode_buf[SECTOR_SIZE_BYTES];

  read_sectors(sect_num, 1, inode_buf);

  struct ext2_inode* inode = (struct ext2_inode*)(inode_buf + offset_within_sect);

  return *inode;

}


struct ext2_inode Get_Root_Inode(int parIndex) {

    struct ext2_inode root_inode = Get_Inode(ROOT_INODE, parIndex);

    return root_inode;

}

void read_directory_recursive(__u32 i_block[], int curInode, int preInode, int parIndex, int* mark) {

  struct        ext2_dir_entry_2* dir;
  unsigned char buf_dir[BLOCKSIZE];

  // Set this inode to be 1
  mark[curInode] = 1;

  int i = 0;
  for(; i < EXT2_N_BLOCKS-3; i++) {
    if(i_block[i] != 0) {
      read_sectors(parArray[parIndex-1].start_sect + i_block[i]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
      int len = 0;
      
      if(i == 0) {
        // The first entry should be '.'
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        if(dir->inode != curInode || strcmp(dir->name, self_reference)) {
          printf("partition: %d, inode: %d, wrong self_reference: %d\n",parIndex, curInode, dir->inode);
          dir->inode = curInode;
          write_sectors(parArray[parIndex-1].start_sect + i_block[i]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
        }
        len += dir->rec_len;

        // The second entry should be '..'
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        if(dir->inode != preInode || strcmp(dir->name, parent_reference)) {
          printf("partition: %d, inode: %d, prev inode: %d, wrong parent_reference: %d\n",parIndex, curInode, preInode, dir->inode);
          dir->inode = preInode;
          write_sectors(parArray[parIndex-1].start_sect + i_block[i]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
        }
        len += dir->rec_len; 
      }     

      while(len < BLOCKSIZE) {        
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        if(dir->inode != 0 && mark[dir->inode] != 1 && dir->file_type == 2) {
          struct ext2_inode nextInode = Get_Inode(dir->inode, parIndex);      
          read_directory_recursive(nextInode.i_block, dir->inode, curInode, parIndex, mark);
        }
        len += dir->rec_len;  
      }
    } else {      
      return;            
    }
  }


}

void read_inode_recursive(__u32 i_block[], int parIndex, int* mark) {
  
  struct        ext2_dir_entry_2* dir;
  unsigned char buf_dir[BLOCKSIZE];
  // mark increament one  
  

  int i = 0;
  for(; i < EXT2_N_BLOCKS-3; i++) {
    if(i_block[i] != 0) {
      read_sectors(parArray[parIndex-1].start_sect + i_block[i]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
      int len = 0;
      
      if(i == 0) {
        // The first entry should be '.'
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        mark[dir->inode]++;
        len += dir->rec_len;

        // The second entry should be '..'
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        mark[dir->inode]++;
        len += dir->rec_len; 
      }     

      while(len < BLOCKSIZE) {        
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);
        if(dir->file_type == 2) {
          if(mark[dir->inode] == 0) {            
            // Recursion
            mark[dir->inode]++;        
            struct ext2_inode nextInode = Get_Inode(dir->inode, parIndex);      
            read_inode_recursive(nextInode.i_block, parIndex, mark);
          } else
            mark[dir->inode]++;
        } else {
          mark[dir->inode]++;
        }

        len += dir->rec_len;        
      }
    } else {      
      return;            
    }
  }

}


struct ext2_inode Get_Lost_Found_Inode(int parIndex) {
  struct ext2_inode root_inode = Get_Root_Inode(parIndex);
  struct ext2_dir_entry_2* dir;
  unsigned char buf_dir[BLOCKSIZE];
  // unsigned char lost_dir[BLOCKSIZE];
  int i = 0;
  for(; i < EXT2_N_BLOCKS-3; i++) {
    if(root_inode.i_block[i] != 0) {
      read_sectors(parArray[parIndex-1].start_sect + root_inode.i_block[i]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO,buf_dir);
      int len = 0;
      while(len < BLOCKSIZE) {
        dir = (struct ext2_dir_entry_2*) (buf_dir+len);          
        // Find lost+found dir
        if(!strcmp(dir->name, lost_found)) {          
          return Get_Inode(dir->inode, parIndex);         
        }
        len += dir->rec_len;  
      }
    } else
      return;
      
  }
}



int Write_To_Lost_Found(struct ext2_inode lostfound, int type, int inodeIndex, int parIndex) {
    struct ext2_dir_entry_2* dir;
    unsigned char buf_dir[BLOCKSIZE];

    // Change 4017 to "4017" and store in array c
    int str_len = 0;
    char c[EXT2_NAME_LEN];

    sprintf(c, "%d", inodeIndex);
    str_len = strlen(c);

    // Align to 4 bytes    
    int rec_length = (8 + str_len + 4 - 1) / 4 * 4;


    int j = 0;
    for(; j < EXT2_N_BLOCKS-3; j++) {
      if(lostfound.i_block[j] != 0) {
        read_sectors(parArray[parIndex-1].start_sect + lostfound.i_block[j]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
        int len = 0;
        while(len < BLOCKSIZE) {
          dir = (struct ext2_dir_entry_2*) (buf_dir+len);    
          if(dir->inode == 0) {
              int temp_len = dir->rec_len;
              dir->inode = inodeIndex;
              dir->rec_len = rec_length;
              dir->name_len = str_len;              
              dir->file_type = type;            
              memcpy(dir->name, c, str_len*sizeof(char));

              // Create the split for the rest unused space
              len += dir->rec_len;
              dir = (struct ext2_dir_entry_2*) (buf_dir+len);    
              dir->rec_len = temp_len - rec_length;

              // Write to the disk
              write_sectors(parArray[parIndex-1].start_sect + lostfound.i_block[j]* BLOCK_SECTOR_RATIO, BLOCK_SECTOR_RATIO, buf_dir);
              pass1(parIndex);

              // Return 1 to indicate successful write
              return 1;
          }
          len += dir->rec_len;
        }
      } else
        return 0;
    }
}

int Get_Inode_Type(__u16 i_mode) {
  __u16 mask = 0xF000;
  i_mode = mask & i_mode;
  if(i_mode == 0xC000)
    return 6;
  if(i_mode == 0xA000)
    return 7;
  if(i_mode == 0x8000)
    return 1;
  if(i_mode == 0x6000)
    return 4;
  if(i_mode == 0x4000)
    return 2;
  if(i_mode == 0x2000)
    return 3;
  if(i_mode == 0x1000)
    return 5;
}

int Check_Inode_linkcount_pass2(int inodeIndex, int parIndex, int m) {

   struct ext2_super_block super_block = get_superblock(parIndex);
  
  int inodes_per_group = super_block.s_inodes_per_group;

  // Get which block group this inode belongs to
  int block_group = (inodeIndex - 1) / inodes_per_group;
  int local_inode_index = (inodeIndex - 1) % inodes_per_group;

  int64_t blockgroup_offset;

  if(BLOCKSIZE == 1024)
    blockgroup_offset = SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE;
  else
    // The first group descriptor locates in the next block following superblock
    blockgroup_offset = BLOCKSIZE;

  unsigned char blockgroup_buf[BLOCKSIZE];

  // find the start sector for first block group descriptor
  int blockgroup_start_sector = parArray[parIndex-1].start_sect + blockgroup_offset/SECTOR_SIZE_BYTES;
                              
  // Read one block to get all the block group descriptor                            
  read_sectors(blockgroup_start_sector, BLOCK_SECTOR_RATIO, blockgroup_buf);  

  // Find the corresponding group descriptor according to the block_group
  struct ext2_group_desc* group_desc = (struct ext2_group_desc*)(blockgroup_buf + block_group * BLOCK_GROUP_DESC);

  // Find the start sector of inode table
  int inodetable_start_sector = parArray[parIndex-1].start_sect + group_desc->bg_inode_table * BLOCK_SECTOR_RATIO;

  // Find the sector of the target inode based on inode table start sector
  int sect_num = inodetable_start_sector + local_inode_index*INODE_SIZE / SECTOR_SIZE_BYTES;

  // Calculate the offset within the sector
  int offset_within_sect = local_inode_index*INODE_SIZE % SECTOR_SIZE_BYTES;

  // Read the whole target sector
  unsigned char inode_buf[SECTOR_SIZE_BYTES];

  read_sectors(sect_num, 1, inode_buf);

  struct ext2_inode* inode = (struct ext2_inode*)(inode_buf + offset_within_sect);

  if(inode->i_links_count != 0) {
      if(m == 0) {
        // create a directory or file in lost+found
        int type = Get_Inode_Type(inode->i_mode);
        struct ext2_inode lostfound = Get_Lost_Found_Inode(parIndex);
        printf("partition: %d, lost_found inode: %d, link_count: %d\n", parIndex, inodeIndex, inode->i_links_count);        
        if(Write_To_Lost_Found(lostfound, type, inodeIndex, parIndex))
          printf("partition: %d, lost_found inode: %d write to lost+found successfully!\n",parIndex, inodeIndex);
        else
          printf("partition: %d, lost_found inode: %d fail to write to lost+found \n",parIndex, inodeIndex);
        // Return 1 to represent that some line count is inconsistent.
        return 1;
      }
  }
  return 0;

}


int Check_Inode_linkcount_pass3(int inodeIndex, int parIndex, int m) {

   struct ext2_super_block super_block = get_superblock(parIndex);
  
  int inodes_per_group = super_block.s_inodes_per_group;

  // Get which block group this inode belongs to
  int block_group = (inodeIndex - 1) / inodes_per_group;
  int local_inode_index = (inodeIndex - 1) % inodes_per_group;

  int64_t blockgroup_offset;

  if(BLOCKSIZE == 1024)
    blockgroup_offset = SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE;
  else
    // The first group descriptor locates in the next block following superblock
    blockgroup_offset = BLOCKSIZE;

  unsigned char blockgroup_buf[BLOCKSIZE];

  // find the start sector for first block group descriptor
  int blockgroup_start_sector = parArray[parIndex-1].start_sect + blockgroup_offset/SECTOR_SIZE_BYTES;
                              
  // Read one block to get all the block group descriptor                            
  read_sectors(blockgroup_start_sector, BLOCK_SECTOR_RATIO, blockgroup_buf);  

  // Find the corresponding group descriptor according to the block_group
  struct ext2_group_desc* group_desc = (struct ext2_group_desc*)(blockgroup_buf + block_group * BLOCK_GROUP_DESC);

  // Find the start sector of inode table
  int inodetable_start_sector = parArray[parIndex-1].start_sect + group_desc->bg_inode_table * BLOCK_SECTOR_RATIO;

  // Find the sector of the target inode based on inode table start sector
  int sect_num = inodetable_start_sector + local_inode_index*INODE_SIZE / SECTOR_SIZE_BYTES;

  // Calculate the offset within the sector
  int offset_within_sect = local_inode_index*INODE_SIZE % SECTOR_SIZE_BYTES;

  // Read the whole target sector
  unsigned char inode_buf[SECTOR_SIZE_BYTES];

  read_sectors(sect_num, 1, inode_buf);

  struct ext2_inode* inode = (struct ext2_inode*)(inode_buf + offset_within_sect);

  if(inode->i_links_count != 0) {
    if(m != 0 && m != inode->i_links_count) {
      printf("partition: %d, inode: %d, link_count: %d, actually_link_count: %d\n", parIndex, inodeIndex, inode->i_links_count, m);        
      inode->i_links_count = m;
      write_sectors(sect_num, 1, inode_buf);
    }
  }
  return 0;

}


void pass1(int parIndex) {

  int count = Get_Inode_Counts(parIndex);
  int* mark = (int*)malloc(sizeof(int) * count);
  memset(mark, 0, sizeof(int) * count);
  //Start from the root inode (inode 2)
  struct ext2_inode root_inode = Get_Root_Inode(parIndex);

  if(root_inode.i_mode && EXT2_S_IFDIR == 0) {
      printf("root inode is not a directory!");
      exit(-1);
  } else {
    read_directory_recursive(root_inode.i_block, ROOT_INODE, ROOT_INODE, parIndex, mark);
  }

  printf("Finish pass 1 for partition %d\n", parIndex);
  free(mark);

}

void pass2(int parIndex) {
  int count = Get_Inode_Counts(parIndex);
  int* mark = (int*)malloc(sizeof(int) * count);
  int flag;

  while(1) {
    flag = 0;
    memset(mark, 0, sizeof(int) * count);
    //Start from the root inode (inode 2)
    struct ext2_inode root_inode = Get_Root_Inode(parIndex);    
    // mark[ROOT_INODE] = 1;
    read_inode_recursive(root_inode.i_block, parIndex, mark);        
    int i = 2;
    for (; i < count; i++) {
      if(Check_Inode_linkcount_pass2(i, parIndex, mark[i])) {
        flag = 1;
        break;
      }
    }    
    if(flag)
      continue;
    else
      break;
  }
  printf("Finish pass 2 for partition %d\n", parIndex);
  free(mark);  
}

void pass3(int parIndex) {
   int count = Get_Inode_Counts(parIndex);
  int* mark = (int*)malloc(sizeof(int) * count);
  memset(mark, 0, sizeof(int) * count);
  //Start from the root inode (inode 2)
  struct ext2_inode root_inode = Get_Root_Inode(parIndex);


  // mark[ROOT_INODE] = 1;
  read_inode_recursive(root_inode.i_block, parIndex, mark);

  int i = 0;
  for (; i < count; i++) {
    Check_Inode_linkcount_pass3(i, parIndex, mark[i]);
  }
  printf("Finish pass 3 for partition %d\n", parIndex);
  free(mark);   

}


void printf_inode(int inodeIndex, int parIndex) {
  struct ext2_inode node = Get_Inode(inodeIndex, parIndex);
  int type = Get_Inode_Type(node.i_mode);

  printf("inode: %d, type: %d, link_count: %d\n", inodeIndex, type, node.i_links_count);
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

    int print_partition_num = 0;
    int fix_partition_num = -1;
    while((opt = getopt_long(argc, argv, "i:f:p:", long_options, NULL)) != EOF) {
      switch (opt) {
        case 'p':
          // print the partition table          
          print_partition_num = atoi(optarg);
          // printf("Print partition: %d\n", partition_num);
          break;
        case 'f':
          // fix the problems
          fix_partition_num = atoi(optarg);
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


if(print_partition_num != 0){
    if (print_partition_num > parArrayCounter || print_partition_num < 0)
      printf("%d\n", -1);
    else {
      printf("0x%02X %d %d\n", parArray[print_partition_num-1].sys_ind, parArray[print_partition_num-1].start_sect,
      parArray[print_partition_num-1].nr_sects);      
    }
} 

if(fix_partition_num != -1) {
  if(fix_partition_num == 0) {
    int idx = 1;
    for(; idx <= parArrayCounter; idx++) {
      if(parArray[idx-1].sys_ind == LINUX_EXT2_PARTITION) {
        pass1(idx);
        pass2(idx);
        pass3(idx);
      }
    }
  } else {
    if(fix_partition_num <= parArrayCounter &&  fix_partition_num>0) {
        pass1(fix_partition_num);
        pass2(fix_partition_num);
        pass3(fix_partition_num);
    }
  }
}



    





    free(parArray);



}



