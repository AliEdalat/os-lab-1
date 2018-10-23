#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

void swap(int *xp, int *yp)
{
    int temp = *xp;
    *xp = *yp;
    *yp = temp;
}

void bubble_sort(int* arr, int* indices, int n)
{
    int i, j;
    for (i = 0; i < n-1; i++)
    {
        for (j = 0; j < n-i-1; j++)
        {
            if (arr[j] > arr[j+1])
            {
                swap(&arr[j], &arr[j+1]);
                swap(&indices[j], &indices[j+1]);
            }
        }
    }
}

void write_to_file(char** argv, int* indices, int argc)
{
    int fd, i;

    unlink("result.txt");
    fd = open("result.txt", O_CREATE | O_WRONLY);

    if (fd < 0)
    {
        printf(1, "sort: result.txt could not be opened.");
        exit();
    }

    for (i = 0; i < argc - 1; ++i)
    {
        write(fd, argv[indices[i] + 1], strlen(argv[indices[i] + 1]));
	    write(fd, " ", 1);
    }
    
    write(fd, "\n", 1);
}

int
main(int argc, char* argv[])
{
    int i;
    int nums[argc - 1];
    int indices[argc - 1];

    if(argc <= 1){
        printf(1, "sort: no arguments passed.");
        exit();
    }

    for(i = 1; i < argc; i++) {
        nums[i - 1] = atoi(argv[i]);
        indices[i - 1] = i - 1;
    }

    bubble_sort(nums, indices, argc - 1);

    write_to_file(argv, indices, argc);

    printf(1, "pid: %d\n", getpid());

    exit();
}
