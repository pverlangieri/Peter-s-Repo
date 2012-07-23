// Peter Verlangieri fs.cpp

#include <stdio.h>
#include <iostream>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "fs.h"
#include "disk.h"

using namespace std;

//globals
struct super_block fs;
struct file_descriptor fildes[MAX_FILDES];
int *FAT;              //queue
struct dir_entry *DIR; //queue of directories
bool mounted = false;
char mountname[200];

int make_fs(char *name)
{

  int openit;
  openit =  make_disk(name);
  if(openit != 0)
    {
      fprintf(stderr,"make_fs: cannot make file\n");
      return -1;
    }
  openit = open_disk(name);
  if(openit != 0)
    {
      fprintf(stderr,"make_fs: cannot open file\n");
      return -1;
    }
  //super block is at location 0
  //fat is at 1
  fs.fat_idx = 1;
  fs.fat_len = 0; //number of fat entries

  //dir starts at 24
  fs.dir_idx = 8;
  fs.dir_len = 0; //number of files

  //data starts at 4k
  fs.data_idx = 4096;

  char buf[BLOCK_SIZE];

  //write the super block to the file
  memset(buf,0,BLOCK_SIZE);
  memcpy(buf,&fs,sizeof(struct super_block));
  block_write(0,buf);
  
  memset(buf,-1,BLOCK_SIZE);
  //initialize the directory and fat
  for(int i = 1; i < 10; i++)
    block_write(i,buf);

  close_disk();
  return 0;
}

int mount_fs(char *name)
{
  if(mounted == true)
    {
      fprintf(stderr,"mount_fs: cannot mount multiple filesystems\n");
      return -1;
    }
  if(open_disk(name) != 0)
    {
      fprintf(stderr,"mount_fs: cannot open file\n");
      return -1;
    }
  char buf[BLOCK_SIZE];
  if(block_read(0,buf) != 0)
    {
      fprintf(stderr,"mount_fs: cannot read superblock\n");
      return -1;
    }

  struct super_block *super;
  super = (super_block *) buf;
  fs.fat_idx = super->fat_idx;
  fs.fat_len = super->fat_len;
  fs.dir_idx = super->dir_idx;
  fs.dir_len = super->dir_len;
  fs.data_idx = super->data_idx;


  int size = fs.fat_len * sizeof(int);
  int blocks = 4; //(size -1)/BLOCK_SIZE + 1;

 //load FAT
  if((FAT = (int *) malloc(blocks*BLOCK_SIZE)) == NULL){
    perror("cannot allocate FAT\n");
    exit(1);
  }
  char *p;
  int cnt;
  for(cnt = 0,p = (char *) FAT; cnt < blocks; ++cnt)
    {
      block_read(fs.fat_idx + cnt, buf);
      memcpy(p,buf,BLOCK_SIZE);
      p+= BLOCK_SIZE;
    }

  //load DIR
  size = fs.dir_len * sizeof(struct dir_entry);
  blocks = 1; //(size - 1)/BLOCK_SIZE + 1;
  if((DIR = (dir_entry *) malloc(blocks*BLOCK_SIZE)) == NULL){
      perror("cannot allocate DIR\n");
      exit(1);
    }

  for(cnt = 0, p = (char *) DIR; cnt < blocks; ++cnt)
    {
      block_read(fs.dir_idx + cnt,buf);
      memcpy(p,buf,BLOCK_SIZE);
      p+= BLOCK_SIZE;
    }

  //initialize the file descriptors
  for(cnt = 0; cnt < MAX_FILDES;++cnt)
    fildes[cnt].used = FREE;

  mounted = true;
  strcpy(mountname,name);

  return 0;
}

int umount_fs(char *name)
{
  if(mounted == false)
    {
      fprintf(stderr,"umount_fs: a filesystem is not currently mounted\n");
      return -1;
    }
  if(strcmp(mountname,name))
    return -1;

  //include checks for file descriptors
  for(int i = 0; i < MAX_FILDES; i++)
    {
      if(fildes[i].used == USED)
	fprintf(stderr,"umount: warning: fd [%i] still open\n",i);
      fildes[i].used = FREE;
    }
  //write back FAT
  char *p;
  char buf[BLOCK_SIZE];
  int size = fs.fat_len * sizeof(int);
  int blocks = 4; //(size -1)/BLOCK_SIZE + 1;
  int cnt;
  for(cnt = 0,p = (char *) FAT; cnt < blocks;cnt++)
    {
      memcpy(buf,p,BLOCK_SIZE);
      block_write(fs.fat_idx + cnt,buf);
      p+=BLOCK_SIZE;
    }
  //write back Directory
  size = fs.dir_len * sizeof(int);
  blocks = 1; //(size -1)/BLOCK_SIZE + 1;
  for(cnt = 0,p = (char *) DIR; cnt < blocks;cnt++)
    {
      memcpy(buf,p,BLOCK_SIZE);
      block_write(fs.dir_idx + cnt,buf);
      p+=BLOCK_SIZE;
    }
  for(cnt = 0; cnt < MAX_FILDES;++cnt)
    fildes[cnt].used = FREE;
  free(FAT);
  free(DIR);
  if(close_disk())
    {
      fprintf(stderr,"umount_fs: cannot close disk\n");
      mounted = false;
      return -1;
    }
  mounted = false;
  return 0;
}

int dir_list_file(char * name)
{
  for(int i = 0; i < 64; i++)
    {
      if(!strcmp(DIR[i].name,name))
	return i;
    }
  return -1;
}
int get_free_filedes()
{
  for(int i = 0; i < MAX_FILDES; i++)
    if(fildes[i].used == FREE)
      return i;
  return -1;
}

int fs_open(char *name)
{
  int file;
  file = dir_list_file(name);
  if(file == -1)
    {
      fprintf(stderr,"fs_open: cannot find file %s\n",name);
      return -1;
    }
  int fd;
  fd = get_free_filedes();
  if(fd == -1)
    {
      fprintf(stderr,"fs_open: cannot open more than 32 filedescriptors\n");
      return -1;
    }
  fildes[fd].used = USED;
  fildes[fd].file = file;
  fildes[fd].offset = 0;
  DIR[file].ref_cnt++; //increase references to file
  return fd;
}

int fs_close(int fd)
{
  if(fd < 0 || fd >= MAX_FILDES || fildes[fd].used)
    return -1;
  
  DIR[fildes[fd].file].ref_cnt--;
  fildes[fd].used = FREE;
  return 0;
}


int get_free_FAT()
{
  for(int i = 0; i < 4096; i++)
    if(FAT[i] == FREE)
      return i;
  fprintf(stderr,"get_free_FAT: YOU ARE TOO FAT, use less disk space\n");
  return -1;
}

void dir_create_file(char *name)
{
  int placement = -1;
  if(fs.dir_len > 63)
    {
      fprintf(stderr,"dir_create_file: cannot create more than 64 files\n");
      return;
    }
  for(int i = 0; i < 64; i++)
    {
      if(DIR[i].used == FREE)
	{
	  placement = i;
	  break;
	}
    }
  if(placement == -1)
    {
      fprintf(stderr,"dir_create_file:too many files\n");
      return;
    }
  DIR[placement].used = USED;
  strcpy(DIR[placement].name,name);
  DIR[placement].size = 0;
  int temp = get_free_FAT();
  DIR[placement].head = temp;
  FAT[temp] = FEOF;
  DIR[placement].ref_cnt = 0;
  fs.dir_len++;
}

int fs_create(char*name)
{
  for(int i = 0; i < 20; i++)
    {
      if(i == 16)
	{
	  fprintf(stderr,"file name too long");
	  return -1;
	}
      if(name[i] == '\0')
	break;
    }
  int file;
  file = dir_list_file(name);
  if(file < 0)
    {
      if(fs.dir_len > 63)
	{
	  fprintf(stderr,"fs_create: cannot create more than 64 files\n");
	  return -1;
	}
      dir_create_file(name);
      return 0;
    }
  else{
    fprintf(stderr,"fs_create: file [%s] already exists\n",name);
    return -1;    
  }
}

int dir_delete_file(char *name)
{
  int file = dir_list_file(name);
  if(file < 0)
    return -1;
  if(DIR[file].ref_cnt > 0)
    {
      fprintf(stderr,"dir_delete_file: file [%s] is in use: ref_cnt [%i]\n",name,DIR[file].ref_cnt);
      return -1;
    }
  DIR[file].used = FREE;
  DIR[file].size = 0;
  int head = DIR[file].head;
  int next = FAT[head];
  int temp = next;
  while(head != FEOF)
    {
      next = FAT[head];
      FAT[head] = FREE;
      head = next;
    }
  fs.dir_len--;

}


int fs_delete(char* name)
{
  int file;
  file = dir_list_file(name);
  if(file >= 0)
    {
      return dir_delete_file(name);
    }
  else{
    fprintf(stderr,"fs_delete: file [%s] does not exist\n",name);
    return -1;
  }
}

int fs_rdwr(int fd, void *dst, size_t nbyte,enum access_type mode)
{
  if(nbyte < 1)
    return -1;
  if(fd < 0 || fd >= MAX_FILDES || fildes[fd].used)
    {fprintf(stderr,"fs_rdwr: bad filedescriptor\n");
      return -1;
    }
     //find file from DIR[fd]
  struct dir_entry *d = &DIR[fildes[fd].file];

  //check offset and size
  off_t offs = fildes[fd].offset;
  int oldsize = d->size;
 
  if(offs + nbyte > d->size)
    {
      if(mode == READ)
	nbyte = d->size - offs;
    }
  int counter = 0;
  int numtoexpand;
  if(mode == WRITE && offs+nbyte > d->size)
    numtoexpand = (offs+nbyte)/BLOCK_SIZE - oldsize/BLOCK_SIZE;
  else
    numtoexpand = d->size/BLOCK_SIZE - oldsize/BLOCK_SIZE;
  int numblocks = (nbyte + (offs%BLOCK_SIZE))/BLOCK_SIZE + 1;
  int offsblock = offs/BLOCK_SIZE;
  int fatblock = d->head;
  char buf[BLOCK_SIZE];
  //load information from FAT
  for(int i = 0; i < offsblock; i++)
    {
      if(fatblock < 0){
	fprintf(stderr,"fs_rdwr: trying to read past end of file\n"); 
	return -1;
      }
      fatblock = FAT[fatblock];
    }
  char *bufnum = (char *)dst;
  int fsnum = offs % BLOCK_SIZE;
  int dataamt;
  int amountleft = nbyte;
  for(int i = 0; i < numblocks; i++)
    {
     
      if(fsnum+amountleft > BLOCK_SIZE){
	dataamt = BLOCK_SIZE - fsnum;
      }
      else{
	dataamt = amountleft;
      }

      // cout << "did I make it here?? numblocks: " << numblocks << " numtoexpand " << numtoexpand << " fsnum " << fsnum << endl;
      if(block_read(fatblock+4096,buf))
	{
	  fprintf(stderr,"fs_rdwr: could not read from block");
	  return -1;
	}
      if(mode == WRITE)
	{
	  //printf("buf+fsnum %u bufnum %u dataamt %u\n",buf+fsnum,bufnum,dataamt);
	  memcpy(buf+fsnum,bufnum,dataamt);
	  if(block_write(fatblock+4096,buf))
	    break;
	  counter+=dataamt;
	}
      if(mode == READ)
	{
	  counter+=dataamt;
	  memcpy(bufnum,buf+fsnum,dataamt);
	}

      if( i == (numblocks - numtoexpand -1) && numtoexpand > 0)
	{
	  //cout << "DID THE ERROR OCCUR IN HERE?" << endl;
	  int temp = get_free_FAT();
	  if(temp == -1)
	    return -1;
	  FAT[fatblock] = temp;
	  FAT[temp] = FEOF;
	  numtoexpand--;
	}
      bufnum = bufnum + BLOCK_SIZE;
      fsnum = 0;
      //cout << "fatblock " << fatblock << " FAT[fatblock]" << FAT[fatblock] << endl;
      fatblock = FAT[fatblock];
      amountleft = amountleft - dataamt;
    }
  if(offs + counter > d->size)
    {
      if(mode == WRITE)
	{
	  oldsize = d->size;
	  d->size = offs + nbyte;
	}
    }
  fildes[fd].offset += counter;
  return counter;
}

int fs_read(int fd, void *dst, size_t nbyte)
{
  return fs_rdwr(fd,dst,nbyte,READ);
}
int fs_write(int fd, void*dst,size_t nbyte)
{
  //fprintf(stderr,"fd %i, nbyte %i \n",fd,nbyte);
  return fs_rdwr(fd,dst,nbyte,WRITE);
}



int fs_get_filesize(int fd)
{
  if(fd < 0 || fd >= MAX_FILDES || fildes[fd].used)
    {fprintf(stderr,"fs_get_filesize: bad filedescriptor\n");
      return -1;
    }
  return DIR[fildes[fd].file].size;
}
int fs_lseek(int fd,off_t offset)
{

  if(fd < 0 || fd >= MAX_FILDES || fildes[fd].used)
    {fprintf(stderr,"fs_lseek: bad filedescriptor\n");
      return -1;
    }
  if(offset < 0)
    return -1;
  fildes[fd].offset = offset;
  return 0;
}

int fs_truncate(int fd, off_t length)
{
  
  if(fd < 0 || fd >= MAX_FILDES || fildes[fd].used)
    {fprintf(stderr,"fs_truncate: bad filedescriptor\n");
      return -1;
    }
  struct dir_entry *file = &DIR[fildes[fd].file];
  if(length > file->size)
    return -1;
  if(length < 0)
    return -1;

  int fullblocks = length/BLOCK_SIZE;
  int fatblock = file->head;
  for(int i = 0; i < fullblocks; i++)
    fatblock = FAT[fatblock];
  int next = FAT[fatblock];
  int temp;
  FAT[fatblock] = FEOF;
  while(next != FEOF)
    {
      temp = FAT[next];
      FAT[next] = FREE;
      next = temp;
    }

  file->size = length;
  return 0;
}



