#!/usr/bin/env python

import readline, socket

s = socket.socket()
s.connect(('localhost', 5555))
s.settimeout(0.2)

while True:
    a = raw_input('send: ')
    s.send(a)
    try:
        b = s.recv(1024)
        if b != '': print 'resp:', b
    except Exception:
            pass
