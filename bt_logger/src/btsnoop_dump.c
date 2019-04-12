/******************************************************************************
Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <stdbool.h>

#include <private/android_filesystem_config.h>
#include <android/log.h>
#include <cutils/log.h>
#include <cutils/properties.h>

#define DEFAULT_FILE_SIZE_MB 20
#define MAX_FILE_SIZE_MB 150
#define MEGA_BYTE 1024*1024
#define MAX_SNOOP_LOG_FILES 2

#define LOGD0(t,s) __android_log_write(ANDROID_LOG_DEBUG, t, s)

static int file_descriptor = -1;
uint32_t file_size = 0;
pthread_t snoop_client_tid = -1;
int btsnoop_socket = -1;
uint32_t btsnoop_file_size = DEFAULT_FILE_SIZE_MB * MEGA_BYTE;

#define LOCAL_SOCKET_NAME "bthcitraffic"
#define BTSNOOP_PATH "/data/misc/bluetooth/logs"
#define BTSOOP_PORT 8872
#define BTSNOOP_MAX_PACKETS_PROPERTY "persist.bluetooth.btsnoopsize"

//#define __SNOOP_DUMP_DBG__

static void snoop_log(const char *fmt_str, ...)
{
    static char buffer[1024];
    va_list ap;

    va_start(ap, fmt_str);
    vsnprintf(buffer, 1024, fmt_str, ap);
    va_end(ap);

    LOGD0("btsnoop_dump: ", buffer);
}

int btsnoop_file_name (char file_name[256])
{
    struct tm *tmp;
    time_t t;
    char time_string[64];

    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL)
    {
        snoop_log("Error : get localtime");
        return -1;
    }

    if (strftime(time_string, 64, "%Y%m%d%H%M%S", tmp) == 0)
    {
        snoop_log("Error : strftime :");
        return -1;
    }
    snprintf(file_name, 256, BTSNOOP_PATH"/hci_snoop%s.cfa", time_string);
    return 0;
}

int snoop_open_file (void)
{
    char file_name[MAX_SNOOP_LOG_FILES][256];
    int snoop_files_found = 0, old_file_index = 0;
    struct DIR* p_dir;
    struct dirent* p_dirent;

    p_dir = opendir(BTSNOOP_PATH);
    if(p_dir == NULL)
    {
        snoop_log("snoop_log_open: Unable to open the Dir entry\n");
        file_descriptor = -1;
        return -1;
    }
    while ((p_dirent = readdir(p_dir)) != NULL)
    {
        int ret;

        if ((ret = strncmp(p_dirent->d_name, "hci_snoop", strlen("hci_snoop"))) == 0)
        {
            snoop_files_found++;
        }
        else
        {
            continue;
        }
        if (snoop_files_found > MAX_SNOOP_LOG_FILES)
        {
            snoop_log("snoop_log_open: Error : More than two snoop files : Abort");
            file_descriptor = -1;
            closedir(p_dir);
            return -1;
        }
        else if (ret == 0)
        {
            strlcpy(file_name[snoop_files_found - 1], p_dirent->d_name, 256);
            if(old_file_index != (snoop_files_found-1) && strncmp(file_name[snoop_files_found-1], file_name[old_file_index], 256) < 0) {
                old_file_index = snoop_files_found - 1;
            }
#ifdef __SNOOP_DUMP_DBG__
            snoop_log("snoop_log_open: snoop file found : %s", file_name[snoop_files_found - 1]);
#endif //__SNOOP_DUMP_DBG__
        }
    }
    closedir(p_dir);
    if (snoop_files_found == MAX_SNOOP_LOG_FILES)
    {
        char del_file[256];

        /* Delete the oldest File */
        snprintf(del_file, 256, BTSNOOP_PATH"/%s", file_name[old_file_index]);
#ifdef __SNOOP_DUMP_DBG__
            snoop_log("snoop_log_open: old file to delete : %s", del_file);
#endif //__SNOOP_DUMP_DBG__
        unlink(del_file);
    }

    if (btsnoop_file_name(file_name[old_file_index]) != 0)
    {
        snoop_log("snoop_log_open: error : could not get snoop file name !!");
        return -1;
    }

    snoop_log("snoop_log_open: new file : %s", file_name[0]);
    file_descriptor = open(file_name[old_file_index], \
                              O_WRONLY|O_CREAT|O_TRUNC, \
                              S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (file_descriptor == -1)
    {
        snoop_log("snoop_log_open: Unable to open snoop log file\n");
        file_descriptor = -1;
        return -1;
    }

    file_size = 0;
    write(file_descriptor, "btsnoop\0\0\0\0\1\0\0\x3\xea", 16);
    return 0;
}

int snoop_connect_to_source (void)
{
    struct sockaddr_un serv_addr;

    int ret, retry_count = 0, addr_len;

    snoop_log("snoop_connect_to_source :");
    /* Create Socket to connect to BT Traffic source*/
    btsnoop_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (btsnoop_socket < 0)
    {
        snoop_log("Can't create client socket : %s\n", strerror(errno));
        return -1;
    }
    else
    {
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sun_family = AF_LOCAL;
        strlcpy(&serv_addr.sun_path[1], LOCAL_SOCKET_NAME, strlen(LOCAL_SOCKET_NAME) + 1);
        addr_len =  strlen(LOCAL_SOCKET_NAME) + 1;
        addr_len += sizeof(serv_addr.sun_family);
        do
        {
            ret = connect(btsnoop_socket, (struct sockaddr *)&serv_addr, addr_len);
            if (ret < 0)
            {
                snoop_log("Can't connect to BT traffic source : %s\n",strerror(errno));
                retry_count++;
                sleep (1);
            }
        } while((ret < 0) && (retry_count < 10));

        if (ret < 0)
        {
            close(btsnoop_socket);
            return -1;
        }

        snoop_log("Connected to bthcitraffic : sock fd : %d", btsnoop_socket);
        return btsnoop_socket;
    }
}

int read_block (int sock, unsigned char *pBuf, int len)
{
    int bytes_recv = 0, ret;
    do
    {
#ifdef __SNOOP_DUMP_DBG__
        snoop_log("read_block : waiting to read");
#endif //__SNOOP_DUMP_DBG__

        ret = recv(sock, &pBuf[bytes_recv], len - bytes_recv, 0);
#ifdef __SNOOP_DUMP_DBG__
        snoop_log("read_block : read returned %d", ret);
#endif //__SNOOP_DUMP_DBG__
        if ( (ret == -1) && (errno != EAGAIN) )
        {
            bytes_recv = ret;
            snoop_log("Error Packet header : Connection Closed : %s\n", strerror(errno));
            break;
        }
        else if (ret == 0)
        {
            snoop_log("Disconnected from bthcitraffic : Exiting...");
            close (sock);
            break;
        }
        bytes_recv += ret;
    } while(bytes_recv < len);

#ifdef __SNOOP_DUMP_DBG__
    snoop_log("bytes read = %d", bytes_recv);
#endif //__SNOOP_DUMP_DBG__
    return bytes_recv;
}

static unsigned char read_buf[1200];

int snoop_process (int sk)
{
    int bytes_recv = 0;
    uint32_t  length;

    if (file_descriptor == -1)
    {
        if (snoop_open_file() != 0)
        {
            return -1;
        }
    }

/*
    24 Bytes snoop Header
    Initial 4 bytes have the length of the HCI packet
    Read 8 bytes which have orignal length and included length
*/
    bytes_recv = read_block (sk, &read_buf[0], 8);
    if ((bytes_recv == 0) || (bytes_recv == -1))
    {
        snoop_log("Error in reading the Header : ");
        return -1;
    }

    length = read_buf[0] << 24 | read_buf[1] << 16 | read_buf[2] << 8 | read_buf[3];

#if 1
#ifdef __SNOOP_DUMP_DBG__
    snoop_log("Length of Frame %ld : byte %0x %0x %0x %0x", length,
        read_buf[0], read_buf[1], read_buf[2], read_buf[3]);

    snoop_log("File Size = %d", file_size);
#endif //__SNOOP_DUMP_DBG__

    if (file_size > btsnoop_file_size)
    {
        if (file_descriptor != -1)
        {
            close(file_descriptor);
            file_descriptor = -1;
            if (snoop_open_file() != 0)
            {
                return -1;
            }
        }
    }
#endif

/*
    Read rest of snoop header(16 Bytes) and HCI Packet
*/
    bytes_recv = read_block (sk, &read_buf[8], length + 16);
    if ((bytes_recv == 0) || (bytes_recv == -1))
    {
        snoop_log("Error reading snoop packet : ");
        return -1;
    }

    file_size += (bytes_recv + 8);

    write(file_descriptor, read_buf, bytes_recv + 8);

    return 0;
}

void *snoop_dump_thread()
{
    int sk, ret, bytes_recv;
    uint32_t input_file_size;
    snoop_log ("snoop_dump_thread starting");

    input_file_size = property_get_int32(BTSNOOP_MAX_PACKETS_PROPERTY, DEFAULT_FILE_SIZE_MB);
    if (input_file_size < DEFAULT_FILE_SIZE_MB) {
        btsnoop_file_size = DEFAULT_FILE_SIZE_MB * MEGA_BYTE;
    } else if (input_file_size > MAX_FILE_SIZE_MB){
        btsnoop_file_size = MAX_FILE_SIZE_MB * MEGA_BYTE;
    } else {
        btsnoop_file_size = input_file_size * MEGA_BYTE;
    }
    snoop_log ("input_file_size read from prop: %d MB ,btsnoop_file_size set to : %d MB ",
                          input_file_size , (btsnoop_file_size /(1024 *1024)));

    sk = snoop_connect_to_source();

/*
       16 Bytes : Read and discard snoop file header
*/
    if(sk < 0)
    {
        snoop_log("Unable to connect to socket");
        return NULL;
    }

    bytes_recv = read_block (sk, &read_buf[0], 16);
    if ((bytes_recv == 0) || (bytes_recv == -1))
    {
        snoop_log("Error in reading the snoop file Header : ");
        return NULL;
    }

    if (snoop_open_file() != 0)
    {
        return NULL;
    }

    if (sk != -1)
    {
        do
        {
            ret = snoop_process(sk);
        } while(ret != -1);
    }

    snoop_log("snoop_dump_thread  terminated");
    return NULL;
}

void start_snoop_logging ()
{
    bool snoop_thread_valid = false;

    snoop_log("starting snoop logging");
    snoop_thread_valid = (pthread_create(&snoop_client_tid, NULL, snoop_dump_thread, NULL) == 0);

    if (!snoop_thread_valid) {
        snoop_log("pthread_create failed: %s", strerror(errno));
    } else {
        snoop_log(" snoop_dump_thread is initialized");
    }
}

static void close_fd(int *fd) {
  if (*fd != -1) {
    close(*fd);
    *fd = -1;
  }
}

void snoop_thread_cleanup()
{
  close_fd(&file_descriptor);
  if (btsnoop_socket != -1) {
    shutdown(btsnoop_socket, SHUT_RDWR);
    close_fd(&btsnoop_socket);
  }
}

void stop_snoop_logging ()
{
  if ( snoop_client_tid != -1)
  {
    snoop_log(" stop_snoop_logging is called");
    snoop_thread_cleanup();
    pthread_join(snoop_client_tid, NULL);
    snoop_client_tid = -1;
  }
}
