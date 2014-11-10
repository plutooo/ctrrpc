#!/usr/bin/python2

"""
  ctrrpc - A simple RPC client for poking the 3DS over the network.
  -plutoo

  Example (from a python2 terminal):
    > import ctrrpc
    > r = ctrrpc.ctrrpc(ip='<ip>')
    > r.querymem(0x100000)     # Query kernel for info about memory mapped @ 0x100000.
    > r.r32(r.gettls())        # Read from thread-local-storage cmd-buffer.
"""

import socket
import sys
import struct

class ctrrpc:
    s=None

    # Connect to rpc.
    def __init__(self, ip='65.22.33.112', port=8334, debug=False):
        self.s=socket.socket()
        self.s.connect((ip, port))
        self.debug = debug

    # Decode response.
    def d(self, x):
        return struct.unpack('<BBBBIIIIIII', x)

    # Send request.
    def c(self, cmd, args):
        args = list(args)
        if len(args) > 7:
            raise Exception('max len(args) == 7')
        while len(args) != 7:
            args.append(0)
        self.s.send(struct.pack('<BBBBIIIIIII', cmd,0,0,0,
                           args[0],args[1],args[2],args[3],
                           args[4],args[5],args[6]))
        r = self.s.recv(32)

        if self.debug:
            print 'RESPONSE:', r.encode('hex')

        return r

    # Read 32-bits from addr.
    def r32(self, addr):
        r = self.c(1, (addr,))
        return self.d(r)[4]

    # Write 32-bits to addr.
    def w32(self, addr, w):
        self.c(2, (addr, w))

    # svcQueryMemory.
    def querymem(self, addr):
        r = self.c(4, (addr,))
        fields = self.d(r)
        print 'base:', hex(fields[5]), ", size:", hex(fields[6])
        return {'ret':  fields[4], 'base':fields[5],
                'size': fields[6], 'perm':fields[7],
                'state':fields[8], 'flags':fields[9] }

    # svcCreateMemoryBlock.
    def creatememblock(self, addr, size, myperm, otherperm):
        r = self.c(5, (addr,size,myperm,otherperm))
        fields = self.d(r)
        return { 'ret': fields[4], 'handle': fields[5] }

    # svcControlMemory.
    def controlmem(self, addr0, addr1, size, op, perm):
        r = self.c(6, (addr0,addr1,size,op,perm))
        fields = self.d(r)
        return { 'ret': fields[4], 'addr': fields[5] }

    # Get ptr to thread-local-storage.
    def gettls(self):
        r = self.c(3, ())
        return self.d(r)[4]

    # Get service handle.
    def getservicehandle(self, name):
        if len(name) > 8:
            raise Exception('too long service name')
        name = name.encode('hex')
        while len(name) != 16:
            name = name + '00'
        namelo = int(name[0:8], 16)
        namehi = int(name[8:16], 16)

        namelo = struct.unpack('>I', struct.pack('<I', namelo))[0]
        namehi = struct.unpack('>I', struct.pack('<I', namehi))[0]

        r = self.c(7, (namelo, namehi))
        fields = self.d(r)
        return { 'ret': fields[4], 'handle': fields[5] }

    # svcSendSyncRequest.
    def syncrequest(self, handle):
        r = self.c(8, (handle,))
        fields = self.d(r)

        return { 'ret': fields[4] }

    # svcCloseHandle.
    def closehandle(self, handle):
        r = self.c(9, (handle,))
        fields = self.d(r)

        return { 'ret': fields[4] }

    # Get internal ctrulib handle.
    def gethandle(self, name):
        if name == 'gsp':
            r = self.c(10, (0,))
            fields = self.d(r)
            return fields[4]
        else:
            raise Exception('unknown pre-defined handle')

    # malloc/free/linearAlloc/linearFree.
    def malloc(self, sz):
        r = self.c(11, (0, sz))
        fields = self.d(r)
        return fields[4]
    def linearalloc(self, sz):
        r = self.c(11, (1, sz))
        fields = self.d(r)
        return fields[4]
    def free(self, ptr):
        self.c(11, (2, ptr))
    def linearfree(self, ptr):
        self.c(11, (3, ptr))

    # Enable/disable drawing by app (useful when poking GPU).
    def enable_drawing(self):
        self.c(12, (1,))
    def disable_drawing(self):
        self.c(12, (0,))

    def __del__(self):
        self.s.send(struct.pack('<BBBBIIIIIII', 0,0,0,0,0,0,0,0,0,0,0))
        self.s.close()
