#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>


#define GEM3_MEMORY 0xff0e0000
//#define RX_DPR_MEMORY 0xa0010000
#define GEM3_MEMORY_SIZE	0x20


int main()
{
	void *map_base, *virt_addr;
	char buf[GEM3_MEMORY_SIZE];
	off_t target;
	unsigned page_size, mapped_size, offset_in_page;

	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	mapped_size = page_size = getpagesize();
	printf("page size is 0x%x\n", mapped_size);
	offset_in_page = (unsigned)GEM3_MEMORY & (page_size - 1);
	map_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd,GEM3_MEMORY  & ~(off_t)(page_size - 1));
	if(map_base == NULL) printf("mmap return NULL pointer\n");
	virt_addr = (char*)map_base + offset_in_page;
	memcpy(buf, virt_addr, GEM3_MEMORY_SIZE - 8);
	printf("First word is 0x%x\n", ((long int*)virt_addr)[0]);
	return 0;
}

