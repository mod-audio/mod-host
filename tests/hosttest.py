#!/usr/bin/python

import socket, time, subprocess

sock = socket.socket()

def connect_socket():
    sock.connect(('localhost', 5555))
    sock.settimeout(0.2)

def reset_socket():
    global sock
    sock = socket.socket()

jack = None
host = None

def run_jack():
    global jack
    if jack is None:
        jack = subprocess.Popen(["jackd", "-ddummy"])

def run_host():
    global host
    if host is None:
        host = subprocess.Popen(["./mod-host", "-v"])

def kill_jack():
    global jack
    if jack:
        jack.terminate()
    jack = None

def kill_host():
    global host
    if host:
        host.terminate()
    host = None

def reset():
    kill_host()
    kill_jack()
    run_jack()
    time.sleep(0.5)
    run_host()

def s(msg):
    sock.send(msg + "\0")
    return sock.recv(1024).replace("\0", "").split()[1:]

def load_egamp(instance=0):
    return s("add http://lv2plug.in/plugins/eg-amp %d" % instance)
