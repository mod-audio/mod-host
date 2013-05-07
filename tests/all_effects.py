#!/usr/bin/env python

import socket, time
import subprocess as sp

# get mod-host pid
pid = sp.check_output("pgrep mod-host; exit 0", shell=True)
if pid == '':
    print 'mod-host is not running'
    exit(0)

def check_mod_host():
    if sp.check_output("pgrep mod-host; exit 0", shell=True) != pid:
        print 'mod-host died'
        exit(1)

# setup socket
s = socket.socket()
s.connect(('localhost', 5555))
s.settimeout(5)

def send_command(command):
    s.send(command)
    print command

    try:
        resp = s.recv(1024)
        print resp
        return True

    except Exception:
        return False


# get plugins list
plugins = sp.check_output('lv2ls').split('\n');

# add and remove the effects
for i, plugin in enumerate(plugins):
    if plugin != '':
        send_command('add %s %i' % (plugin, i))
        time.sleep(0.25)
        check_mod_host()
        time.sleep(0.25)
        send_command('remove %i' % i)
