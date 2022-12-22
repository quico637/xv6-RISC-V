// FLAGS DEFINED TO OPEN A FILE

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400


// FLAGS DEFINED TO MAP A FILE INTO CURRENT PROCESS VIRTUAL ADDRESSES

#define PROT_READ       0x001
#define PROT_WRITE      0x002
#define PROT_RW		    0x003
#define PROT_EXEC       0x006
#define MAP_SHARED      0x004
#define MAP_PRIVATE     0x005

// define ret val

#define MAP_FAILED ((char *) -1)
