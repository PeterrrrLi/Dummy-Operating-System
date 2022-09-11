#include <stdio.h>
#include <stdlib.h>

#define PAGE_SIZE (4096)
#define SIZE      (PAGE_SIZE / sizeof(int))
#define MAX_LEVEL (50)

void stacker(int level);

int
main()
{
  stacker(1);
  printf("\nSUCCEEDED\n");
  exit(0);
}

void
stacker(int level)
{
	unsigned int array[SIZE];
	unsigned int i = 0;

	for (i=0; i<SIZE; i++) {
		array[i] = i;
	}

	for (i=0; i<SIZE; i++) {
		if (array[i] != i) {
		  printf("Level: %d: FAILED array[%d] = %u != %d\n", level, i, array[i], i);
			exit(1);
		}
	}

	printf("%d ",level);
	if (level < MAX_LEVEL) {
	  stacker(level+1);
	}
	return;
}
