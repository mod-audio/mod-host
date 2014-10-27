#!/usr/bin/python

import readline, socket

sock = socket.socket()
sock.connect(('localhost', 5555))
sock.settimeout(0.2)

def s(msg):
    sock.send(msg + "\0")
    return sock.recv(1024).replace("\0", "").split()[1:]

def load_egamp(instance=0):
    return s("add http://lv2plug.in/plugins/eg-amp %d" % instance)
