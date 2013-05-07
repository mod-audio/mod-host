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
s.settimeout(0.1)


def send_command(command):
    s.send(command)
    print 'sent:', command

    try:
        resp = s.recv(1024)
        if resp: print 'resp:', resp
        return True

    except Exception:
        return False


def check_mod_host():
    if sp.check_output("pgrep mod-host; exit 0", shell=True) == pid:
        print 'test OK'
        print
    else:
        print 'test FAIL'
        exit(1)

print 'test: invalid command'
send_command('non_valid_command %s %i')
check_mod_host()

print 'test: invalid number of parameters (few)'
send_command('add 1')
check_mod_host()

print 'test: invalid number of parameters (many)'
send_command('add 1 2 3')
check_mod_host()

print 'test: null command'
send_command('')
check_mod_host()

print 'test: add invalid effect'
send_command('add XXXX 0')
check_mod_host()

print 'test: add effect with invalid instance (string on instance field)'
send_command('add http://lv2plug.in/plugins/eg-amp XXX')
check_mod_host()

print 'test: add effect with invalid instance (string on instance field, again)'
send_command('add http://lv2plug.in/plugins/eg-amp XXX')
check_mod_host()

print 'test: add effect with invalid instance (big number)'
send_command('add http://lv2plug.in/plugins/eg-amp 999999')
check_mod_host()

print 'test: add effect with invalid instance (negative number)'
send_command('add http://lv2plug.in/plugins/eg-amp -5')
check_mod_host()

print 'test: add effect with valid instance'
send_command('add http://lv2plug.in/plugins/eg-amp 100')
check_mod_host()

print 'test: remove invalid instance (string on instance field)'
send_command('remove XXX')
check_mod_host()

print 'test: remove invalid instance (on valid range but not instantiated)'
send_command('remove 15')
check_mod_host()

print 'test: remove invalid instance (big number)'
send_command('remove 999999')
check_mod_host()

print 'test: remove invalid instance (negative number)'
send_command('remove -10')
check_mod_host()

print 'test: connect invalid effects'
send_command('connect ping pong')
check_mod_host()

print 'test: disconnect invalid effects'
send_command('disconnect ping pong')
check_mod_host()

print 'test: bypass invalid instance'
send_command('bypass XXX 0')
check_mod_host()

print 'test: bypass invalid value'
send_command('bypass 100 XXXX')
check_mod_host()

print 'test: param_set invalid param name'
send_command('param_set 100 XXXX 0.5')
check_mod_host()

print 'test: param_set invalid value (string)'
send_command('param_set 100 gain XXXX')
check_mod_host()

print 'test: param_set invalid value (out of range, above)'
send_command('param_set 100 gain 500.0')
check_mod_host()

print 'test: param_set invalid value (out of range, bellow)'
send_command('param_set 100 gain -500.0')
check_mod_host()

print 'test: param_get invalid param name'
send_command('param_get 100 XXXX')
check_mod_host()

print 'test: remove valid effect'
send_command('remove 100')
check_mod_host()
