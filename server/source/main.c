/*
 * ctrrpc - A simple RPC server for poking the 3DS over the network.
 * -plutoo
 */

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <3ds.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>


#include "gfx.h"

#define PORT 8334
#define MAX_LINES ((240-8)/8)
#define __swap16(v) ((((v) & 0xFF) << 8) | ((v) >> 8))

typedef struct
{
    char   console[2048];
    size_t lines;
} console_t;

static console_t top;
static console_t bot;

static void
consoleClear(console_t *console)
{
    memset(console->console, 0, sizeof(console->console));
    console->lines = 0;
}

static void
renderFrame(void)
{
    u8 bluish[] = { 0, 0, 127 };

    gfxFillColor(GFX_TOP,    GFX_LEFT, bluish);
    gfxFillColor(GFX_BOTTOM, GFX_LEFT, bluish);

    gfxDrawText(GFX_TOP,    GFX_LEFT, top.console, 240-8, 0);
    gfxDrawText(GFX_BOTTOM, GFX_LEFT, bot.console, 240-8, 0);

    gfxFlushBuffers();
    gspWaitForVBlank();
    gfxSwapBuffers();
}

__attribute__((format(printf,2,3)))
static void
print(console_t  *console,
      const char *fmt, ...)
{
    static char buffer[256];
    va_list ap;
    va_start(ap, fmt);
    vsiprintf(buffer, fmt, ap);
    va_end(ap);

    size_t num_lines = 0;
    const char *p = buffer;
    while((p = strchr(p, '\n')) != NULL)
    {
        ++num_lines;
        ++p;
    }

    if(console->lines + num_lines > MAX_LINES)
    {
        p = console->console;
        while(console->lines + num_lines > MAX_LINES)
        {
            p = strchr(p, '\n');
            ++p;
            --console->lines;
        }

        memmove(console->console, p, strlen(p)+1);
    }

    strcat(console->console, buffer);
    console->lines = console->lines + num_lines;
}

int listen_socket;
u64 thread_stack[0x1000/sizeof(u64)];
Handle new_cmd_event;
Handle cmd_done_event;
Handle thread;
int thread_exit = 0;
int last_cmd = 0;
int last_cmd_result = 0;
int sock;
int enable_draw = 1;

typedef struct {
    u8 type;
    u8 tmp[3];
    u32 args[7];
} cmd_t;

cmd_t resp;

int execute_cmd(int sock, cmd_t* cmd) {
    memset(&resp, 0, sizeof(resp));

    switch(cmd->type) {
    case 0: // exit
        return 0xDEAD;

    case 1: { // read u32
        u32* p = (u32*) cmd->args[0];
        resp.args[0] = *p;
        break;
    }

    case 2: { // write u32
        u32* p = (u32*) cmd->args[0];
        *p = cmd->args[1];
        break;
    }

    case 3: { // get tls
        resp.args[0] = (u32) getThreadCommandBuffer();
        break;
    }

    case 4: { // querymem
        MemInfo info;
        PageInfo flags;
        memset(&info, 0, sizeof(info));
        memset(&flags, 0, sizeof(flags));

        int ret = svcQueryMemory(&info, &flags, cmd->args[0]);
        resp.args[0] = ret;
        resp.args[1] = info.base_addr;
        resp.args[2] = info.size;
        resp.args[3] = info.perm;
        resp.args[4] = info.state;
        resp.args[5] = flags.flags;
        break;
    }

    case 5: { // creatememblock
        u32 handle = 0;
        int ret = svcCreateMemoryBlock(&handle, cmd->args[0],
            cmd->args[1], cmd->args[2], cmd->args[3]);

        resp.args[0] = ret;
        resp.args[1] = handle;
        break;
    }

    case 6: { // controlmem
        u32 outaddr = 0;
        int ret = svcControlMemory(&outaddr, cmd->args[0], cmd->args[1],
            cmd->args[2], cmd->args[3], cmd->args[4]);

        resp.args[0] = ret;
        resp.args[1] = outaddr;
        break;
    }

    case 7: { // getservicehandle
        Handle handle = 0;
        int ret = srvGetServiceHandle(&handle, (const char*) &cmd->args[0]);

        resp.args[0] = ret;
        resp.args[1] = handle;
        break;
    }

    case 8: { // syncrequest
        int ret = svcSendSyncRequest(cmd->args[0]);

        resp.args[0] = ret;
        break;
    }

    case 9: { // closehandle
        int ret = svcCloseHandle(cmd->args[0]);

        resp.args[0] = ret;
        break;
    }

    case 10: { // getctrulibhandle
        switch(cmd->args[0]) {

        case 0: { // gsp_handle
            extern Handle gspGpuHandle;
            resp.args[0] = gspGpuHandle;
            break;
        }
        }

        break;
    }

    case 11: { // malloc/free
        char* p = NULL;

        switch(cmd->args[0]) {
        case 0: { // normal-mem
            p = malloc(0x100);
            break;
        }

        case 1: { // linear-mem
            p = linearAlloc(0x100);
            break;
        }

        case 2: { // free normal-mem
            free((void*)cmd->args[1]);
            break;
        }

        case 3: { // free linear-mem
            linearFree((void*)cmd->args[1]);
            break;
        }
        }

        resp.args[0] = (u32) p;
        break;
    }

    case 12: { // enable/disable drawing
        enable_draw = cmd->args[0];
        break;
    }

    default:
        return 0xDEAD; // unknown cmd
    }

    return 0;
}

cmd_t cmd;

void conn_main() {
    APP_STATUS status;
    u32 it = 0;
    int ret = 0;
    int first = 1;
    int exiting = 0;

    while((status = aptGetStatus()) != APP_EXITING)
    {
        hidScanInput();
        consoleClear(&bot);

        print(&bot, "frame: %08x\n", it);
        print(&bot, "ret: %08x\n", ret);
        print(&bot, "last_cmd: %02x\n", last_cmd & 0xFF);

        if(!first) {
            u32 bytes_read = 0;

            while(1) {
                ret = recv(sock, &cmd, sizeof(cmd), 0);
                if(ret < 0) {
                    if(ret == -EWOULDBLOCK)
                        continue;
                    break;
                }

                bytes_read += ret;
                if(bytes_read == sizeof(cmd)) {
                    svcSignalEvent(new_cmd_event);
                    svcWaitSynchronization(cmd_done_event, U64_MAX);
                    svcClearEvent(cmd_done_event);

                    send(sock, &resp, sizeof(resp), 0);

                    if(last_cmd_result == 0xDEAD)
                        exiting = 1;
                    break;
                }
            }
        }

        first = 0;
        it++;

        if(enable_draw)
            renderFrame(); 

        u32 keys = hidKeysUp();
        if(keys & KEY_A || exiting)
            break;     
    }
}

void cmd_thread_func() {
    while(1) {
        svcWaitSynchronization(new_cmd_event, U64_MAX);
        svcClearEvent(new_cmd_event);

        if(thread_exit) svcExitThread();

        last_cmd = cmd.type;
        last_cmd_result = execute_cmd(sock, &cmd);

        svcSignalEvent(cmd_done_event);
    }
}


/*----------------*/
int main(int argc, char *argv[])
{
    APP_STATUS status;

    srvInit();
    aptInit(APPID_APPLICATION);
    gfxInit();
    hidInit(NULL);
    fsInit();

    svcCreateEvent(&new_cmd_event, 0);
    svcCreateEvent(&cmd_done_event, 0);
    svcCreateThread(&thread, cmd_thread_func, 0x0,
                    (u32*)((char*)thread_stack + sizeof(thread_stack)),
                    0x31, 0xfffffffe);

    int where = 0;
    u32 ret = SOC_Initialize((u32*)0x08000000, 0x48000);

    if(ret == 0) {
        listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_socket == -1) {
            where = 1;
            ret = SOC_GetErrno();
        }
        else {
            u32 tmp = fcntl(listen_socket, F_GETFL);
            fcntl(listen_socket, F_SETFL, tmp | O_NONBLOCK);

            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = __swap16(PORT);
            addr.sin_addr.s_addr = INADDR_ANY;

            ret = bind(listen_socket, (struct sockaddr *)&addr, sizeof(addr));
            if(ret != 0) {
                where = 2;
                ret = SOC_GetErrno();
            }
            else {
                ret = listen(listen_socket, 1);
                if(ret == -1) {
                    ret = SOC_GetErrno();
                    where = 3;
                }
            }
        }

    }

    u32 it = 0;
    int accept_errno = 0;
    int first = 1;


    while((status = aptGetStatus()) != APP_EXITING)
    {
        hidScanInput();
        consoleClear(&top);

        print(&top, "newver\n");
        print(&top, "ret: %08x, where: %d\n", ret, where);
        print(&top, "frame: %08x\n", it);
        u32 ip = gethostid();
        print(&top, "ip: %d.%d.%d.%d\n", ip & 0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
                
        if(accept_errno != 0) print(&top, "accept returned errno %d\n", accept_errno);

        if(!first) {
            int s = accept(listen_socket, NULL, NULL);
            if(s == -1) {
                int err = SOC_GetErrno();

                if(err != -EWOULDBLOCK)
                    accept_errno = err;
            }
            else {
                sock = s;
                conn_main();
                closesocket(sock);
            }
        }

        it++;
        first = 0;
        if(enable_draw)
            renderFrame(); 

        u32 keys = hidKeysUp();
        if(keys & KEY_A)
            break;
    }

    thread_exit = 1;
    svcSignalEvent(new_cmd_event);
    svcWaitSynchronization(thread, U64_MAX);
    svcCloseHandle(thread);

    svcCloseHandle(new_cmd_event);
    svcCloseHandle(cmd_done_event);

    SOC_Shutdown();
    fsExit();
    hidExit();
    gfxExit();
    aptExit();
    srvExit();
    return 0;
}
