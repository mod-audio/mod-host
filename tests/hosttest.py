#!/usr/bin/python

import socket, time, subprocess, os, shutil

global sock
sock = socket.socket()

env = os.environ.copy()
env['LV2_PATH'] = "/usr/lib/lv2:/tmp/lv2path"

egamp_preset_manifest = """
@prefix atom: <http://lv2plug.in/ns/ext/atom#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix state: <http://lv2plug.in/ns/ext/state#> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

<urn:test:Gain3>
    a pset:Preset ;
    rdfs:seeAlso <gain_3.ttl> ;
    lv2:appliesTo <http://lv2plug.in/plugins/eg-amp> .
"""
egamp_preset = """@prefix atom: <http://lv2plug.in/ns/ext/atom#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix state: <http://lv2plug.in/ns/ext/state#> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

<urn:test:Gain3>
    a pset:Preset ;
    lv2:appliesTo <http://lv2plug.in/plugins/eg-amp> ;
    rdfs:label "Gain 3" ;
    lv2:port [
        lv2:symbol "gain" ;
        pset:value 3.0
    ] .
"""

def connect_socket():
    global sock
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
        jack_is_running = os.system("ps auxw | grep -v jackdbus | grep jackd | grep -v grep")
        assert jack_is_running != 0, "jackd is already running, please stop it before running the tests"
        jack = subprocess.Popen(["jackd", "-ddummy"], env=env)
        assert jack.poll() == None

def run_host():
    global host
    if host is None:
        host = subprocess.Popen(["./mod-host", "-v"], env=env)
        assert host.poll() == None

def kill_jack():
    global jack
    if jack:
        jack.terminate()
        jack.wait()
    jack = None

def kill_host():
    global host
    if host:
        host.terminate()
        host.wait()
    host = None

def reset_env():
    if os.path.exists("/tmp/lv2path"):
        shutil.rmtree("/tmp/lv2path")
    os.mkdir("/tmp/lv2path")
    os.mkdir("/tmp/lv2path/presets.lv2")
    f = open("/tmp/lv2path/presets.lv2/manifest.ttl", "w")
    f.write(egamp_preset_manifest)
    f.close()
    f = open("/tmp/lv2path/presets.lv2/gain_3.ttl", "w")
    f.write(egamp_preset)
    f.close()

def reset():
    kill_host()
    kill_jack()
    reset_env()
    run_jack()
    time.sleep(0.5)
    run_host()
    time.sleep(0.5)

def s(msg):
    sock.send(msg + "\0")
    r = sock.recv(1024).strip().replace("\0", "").split()[1:]
    assert host.poll() is None, "mod-host died with msg '%s', exit code: %d" % (msg, host.poll())
    assert jack.poll() is None, "jackd died with msg '%s', exit code: %d" % (msg, jack.poll())
    return r

def load_egamp(instance=0):
    return s("add http://lv2plug.in/plugins/eg-amp %d" % instance)
