#!/usr/bin/env python

import socket, time
import subprocess as sp

# get mod-host pid
pid = sp.check_output("pgrep mod-host; exit 0", shell=True)
if pid == '':
    print 'mod-host is not running'
    exit(0)

# setup socket
s = socket.socket()
s.connect(('localhost', 5555))
s.settimeout(5)

def check_mod_host():
    if sp.check_output("pgrep mod-host; exit 0", shell=True) != pid:
        print 'mod-host died'
        exit(1)

def send_command(command):
    s.send(command)
    print 'sent:', command
    time.sleep(0.1)
    check_mod_host()

    try:
        resp = s.recv(1024)
        if resp: print 'resp:', resp
        return True

    except Exception:
        return False

plugins = [
'http://calf.sourceforge.net/plugins/Compressor',
'http://calf.sourceforge.net/plugins/Filter',
'http://calf.sourceforge.net/plugins/Filterclavier',
'http://calf.sourceforge.net/plugins/Flanger',
'http://calf.sourceforge.net/plugins/Monosynth',
'http://calf.sourceforge.net/plugins/MultiChorus',
'http://calf.sourceforge.net/plugins/Organ',
'http://calf.sourceforge.net/plugins/Phaser',
'http://calf.sourceforge.net/plugins/Reverb',
'http://calf.sourceforge.net/plugins/RotarySpeaker',
'http://calf.sourceforge.net/plugins/VintageDelay'
]

for i, plugin in enumerate(plugins):
    send_command('add %s %i' % (plugin, i))

time.sleep(5)

for i in range(len(plugins)):
    send_command('remove %i' % i)
