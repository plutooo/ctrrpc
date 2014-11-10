#!/usr/bin/python2

import sys
import ctrrpc

# Connect to RPC server.
r = ctrrpc.ctrrpc()

# Request GSP service
gsp_handle = r.gethandle('gsp')

if gsp_handle == 0:
    print 'Failed to get gsp service.'
    sys.exit(1)


# GSPGPU:ReadHWRegs
# http://3dbrew.org/wiki/GSPGPU:ReadHWRegs
def gspgpu_readhwregs(addr):
    cmdbuf = r.gettls()

    r.w32(cmdbuf  , 0x40080) # Command id
    r.w32(cmdbuf+4, addr-0x1EB00000) # Reg addr
    r.w32(cmdbuf+8, 4) # Read size

    r.w32(cmdbuf+0x100, (4 << 14) | 2) # Setup output buffer
    r.w32(cmdbuf+0x104, cmdbuf-0x80)

    ret = r.syncrequest(gsp_handle)['ret']

    if ret == 0:
        print hex(addr), ':', hex(r.r32(cmdbuf-0x80))


# Dump framebuffer registers
for addr in range(0x1EF00400, 0x1EF00500, 4):
    gspgpu_readhwregs(addr)
