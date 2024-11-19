#include <stdio.h>
#include <stdlib.h>

int main(int argc, char const *argv[])
{
    // ARGUMENT VERIFICATION
    if (argc != 2) {
        printf("ERROR: expected ./httpd <SERVER PORT>\n");
        return 1;
    }

    int port = atoi(argv[1]);
    printf("port number: %i\n", port);
    if (port == 0) {
        printf("ERROR: INVALID PORT - Port must be between 1024 and 65535 [GOT: %s]\n", argv[1]);
        return 1;
    } else if (port < 1024 || port > 65535) {
        printf("ERROR: INVALID PORT - Port must be between 1024 and 65535 [GOT: %s]\n", argv[1]);
        return 1;
    }
    

    return 0;
}
